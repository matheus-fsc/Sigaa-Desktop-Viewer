#include "core/sync/Baixador.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "core/config/Instituicao.h"
#include "core/http/SigaaSession.h"
#include "core/sync/Materiais.h"
#include "core/util/Caminho.h"

namespace sigaa::sync {
namespace {

// Um TSV de duas colunas, não JSON: não há dependência de JSON no core, o
// formato é legível por quem abrir a pasta, e nome de arquivo não pode conter
// TAB em nenhum dos sistemas que suportamos — então não há o que escapar.
constexpr const char* kManifesto = ".sigaa-offline.tsv";

std::vector<std::string> partirLinha(const std::string& linha) {
    std::vector<std::string> campos;
    size_t ini = 0;
    while (true) {
        const size_t tab = linha.find('\t', ini);
        if (tab == std::string::npos) {
            campos.push_back(linha.substr(ini));
            break;
        }
        campos.push_back(linha.substr(ini, tab - ini));
        ini = tab + 1;
    }
    return campos;
}

}  // namespace

std::string pastaDaTurma(const std::string& base, const std::string& nomeTurma) {
    if (base.empty()) return {};
    const std::string nome = util::nomeSeguro(nomeTurma);
    return util::paraUtf8(util::deUtf8(base) /
                          util::deUtf8(nome.empty() ? "turma" : nome));
}

// ---------------------------------------------------------------------------
// CacheLocal
// ---------------------------------------------------------------------------

const char* CacheLocal::nomeArquivo() { return kManifesto; }

CacheLocal::CacheLocal(std::string diretorio) : diretorio_(std::move(diretorio)) {
    std::ifstream f(util::deUtf8(diretorio_) / kManifesto);
    if (!f) return;

    std::string linha;
    while (std::getline(f, linha)) {
        if (!linha.empty() && linha.back() == '\r') linha.pop_back();
        if (linha.empty()) continue;
        const auto campos = partirLinha(linha);
        if (campos.size() < 2 || campos[0].empty() || campos[1].empty()) continue;
        itens_.emplace_back(campos[0], campos[1]);
    }
}

std::string CacheLocal::caminho(const std::string& idArquivo) const {
    for (const auto& [id, nome] : itens_) {
        if (id != idArquivo) continue;
        // Confere o disco, não o manifesto: o aluno apaga e move arquivo, e um
        // "já está offline" mentiroso o faria fechar o app sem o material.
        const auto p = util::deUtf8(diretorio_) / util::deUtf8(nome);
        if (std::filesystem::exists(p)) return util::paraUtf8(p);
        return {};
    }
    return {};
}

void CacheLocal::registrar(const std::string& idArquivo, const std::string& caminho) {
    const std::string nome = util::paraUtf8(util::deUtf8(caminho).filename());
    if (idArquivo.empty() || nome.empty()) return;

    bool trocou = false;
    for (auto& [id, n] : itens_) {
        if (id == idArquivo) {
            n = nome;
            trocou = true;
            break;
        }
    }
    if (!trocou) itens_.emplace_back(idArquivo, nome);

    std::error_code ec;
    std::filesystem::create_directories(util::deUtf8(diretorio_), ec);
    std::ofstream f(util::deUtf8(diretorio_) / kManifesto, std::ios::trunc | std::ios::binary);
    if (!f) return;
    f << "# sigaa-viewer: material ja baixado desta turma. id<TAB>arquivo\n";
    for (const auto& [id, n] : itens_) f << id << '\t' << n << '\n';
}

int CacheLocal::quantosNoDisco() const {
    int n = 0;
    for (const auto& [id, _] : itens_) {
        if (!caminho(id).empty()) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Baixador
// ---------------------------------------------------------------------------

int canaisRecomendados(int n) {
    if (n <= 2) return 1;          // o login custaria mais que o download
    if (n <= 6) return 2;
    return 3;                      // teto: são logins simultâneos na conta do aluno
}

Baixador::Baixador(AbrirCanal abrir, std::string diretorio, int canais)
    : abrir_(std::move(abrir)),
      diretorio_(std::move(diretorio)),
      canais_((std::max)(1, canais)) {}

std::vector<ItemBaixado> Baixador::baixar(const std::vector<PedidoDownload>& pedidos,
                                          const Progresso& progresso) {
    std::vector<ItemBaixado> saida(pedidos.size());
    for (size_t i = 0; i < pedidos.size(); ++i) saida[i].idArquivo = pedidos[i].idArquivo;
    if (pedidos.empty()) return saida;

    CacheLocal cache(diretorio_);
    // Protege só o `cache` — cada thread escreve num índice distinto de
    // `saida`, e escrever em elementos distintos de um vector que não
    // realoca não precisa de trava.
    std::mutex mutexCache;

    // Fila: o índice do próximo pedido. Um contador atômico basta — não há
    // ordem a preservar aqui, e a saída é indexada, não anexada.
    std::atomic<size_t> proximo{0};
    std::atomic<int> prontos{0};
    const int total = static_cast<int>(pedidos.size());

    std::mutex mutexProgresso;
    auto avisar = [&] {
        if (!progresso) return;
        const int p = prontos.fetch_add(1) + 1;
        std::lock_guard<std::mutex> g(mutexProgresso);
        progresso(p, total);
    };

    const int quantos = (std::min)(canais_, total);

    auto corpo = [&](int indiceCanal) {
        // O canal só é aberto quando há trabalho: se o cache já cobrir tudo,
        // ninguém faz login. Escalonado para não disparar N logins no mesmo
        // milissegundo, o que parece ataque para qualquer WAF.
        std::unique_ptr<Canal> canal;
        std::string erroCanal;
        bool tentouAbrir = false;

        while (true) {
            const size_t i = proximo.fetch_add(1);
            if (i >= pedidos.size()) return;

            if (!forcar_) {
                std::lock_guard<std::mutex> g(mutexCache);
                if (const std::string p = cache.caminho(pedidos[i].idArquivo); !p.empty()) {
                    saida[i].caminho = p;
                    saida[i].reaproveitado = true;
                    avisar();
                    continue;
                }
            }

            if (!tentouAbrir) {
                tentouAbrir = true;
                if (indiceCanal > 0 && atrasoCanal_.count() > 0) {
                    std::this_thread::sleep_for(atrasoCanal_ * indiceCanal);
                }
                canal = abrir_(&erroCanal);
            }
            if (!canal) {
                saida[i].erro = erroCanal.empty() ? "nao consegui abrir a turma" : erroCanal;
                avisar();
                continue;
            }

            std::string erro;
            if (auto p = canal->baixar(pedidos[i].idArquivo, diretorio_, &erro)) {
                std::lock_guard<std::mutex> g(mutexCache);
                saida[i].caminho = *p;
                cache.registrar(pedidos[i].idArquivo, *p);
            } else {
                saida[i].erro = erro.empty() ? "falhou" : erro;
            }
            avisar();
        }
    };

    if (quantos <= 1) {
        corpo(0);
        return saida;
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(quantos));
    for (int i = 0; i < quantos; ++i) threads.emplace_back(corpo, i);
    for (auto& t : threads) t.join();
    return saida;
}

// ---------------------------------------------------------------------------
// O canal de verdade
// ---------------------------------------------------------------------------

namespace {

class CanalSigaa : public Baixador::Canal {
public:
    CanalSigaa(std::unique_ptr<http::SigaaSession> sessao, std::unique_ptr<SessaoTurma> turma)
        : sessao_(std::move(sessao)), turma_(std::move(turma)) {}

    std::optional<std::string> baixar(const std::string& idArquivo,
                                      const std::string& diretorio,
                                      std::string* erro) override {
        return turma_->baixar(idArquivo, diretorio, erro);
    }

private:
    // Ordem importa: `turma_` referencia `sessao_`, então precisa morrer antes.
    // Declarar ao contrário destruiria a sessão sob os pés da SessaoTurma.
    std::unique_ptr<http::SigaaSession> sessao_;
    std::unique_ptr<SessaoTurma> turma_;
};

}  // namespace

Baixador::AbrirCanal canalSigaa(std::string baseUrl, std::string login, std::string senha,
                                Turma turma) {
    return [baseUrl = std::move(baseUrl), login = std::move(login),
            senha = std::move(senha),
            turma = std::move(turma)](std::string* erro) -> std::unique_ptr<Baixador::Canal> {
        auto sessao = std::make_unique<http::SigaaSession>(
            baseUrl.empty() ? config::selecionada().baseUrl : baseUrl);
        if (!sessao->login(login, senha, erro)) return nullptr;

        auto st = std::make_unique<SessaoTurma>(*sessao);
        if (!st->entrar(turma, erro)) return nullptr;
        if (!st->abrirArquivos(erro)) return nullptr;
        return std::make_unique<CanalSigaa>(std::move(sessao), std::move(st));
    };
}

}  // namespace sigaa::sync
