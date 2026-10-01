#include "mcp/Rede.h"

#include <ctime>
#include <filesystem>
#include <mutex>

#include "core/http/SigaaSession.h"
#include "core/parse/Html.h"
#include "core/parse/PortalParser.h"
#include "core/servico/Servico.h"
#include "core/sync/Baixador.h"
#include "core/sync/Materiais.h"
#include "core/util/Caminho.h"

namespace sigaa::mcp {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::mutex mutexRede;          // uma operação de rede por vez
ProvedorCredenciais provedor;
int gasto = 0;
std::time_t ultima = 0;

// Reserva o custo antes de começar; devolve a recusa pronta, ou nada.
std::optional<std::string> reservar(int custo) {
    const std::time_t agora = std::time(nullptr);
    if (ultima && agora - ultima < kIntervaloRede) {
        return "Espere " + std::to_string(kIntervaloRede - (agora - ultima)) +
               " s antes de outra busca no SIGAA: o servidor da universidade bloqueia conta "
               "que consulta rápido demais.";
    }
    if (gasto + custo > kOrcamentoRede) {
        return "O orçamento de buscas no SIGAA desta conversa acabou (" +
               std::to_string(gasto) + " de " + std::to_string(kOrcamentoRede) +
               " requisições). Peça ao aluno para usar Atualizar no app, ou comece uma "
               "conversa nova.";
    }
    gasto += custo;
    ultima = agora;
    return std::nullopt;
}

int restante() { return kOrcamentoRede - gasto; }

std::optional<Credenciais> credenciais() {
    if (!provedor) return std::nullopt;
    auto c = provedor();
    if (!c || c->login.empty() || c->senha.empty()) return std::nullopt;
    return c;
}

const char* kSemCredenciais =
    "Não há senha do SIGAA guardada no cofre deste computador. O aluno precisa entrar uma vez "
    "no app marcando \"Guardar neste computador\".";

void apagar(std::string& s) {
    for (char& ch : s) ch = '\0';
    s.clear();
}

const Turma* turmaPorId(const Snapshot& s, const std::string& id) {
    for (const auto& t : s.turmas) {
        if (t.idTurma == id) return &t;
    }
    return nullptr;
}

// --- baixar_arquivo ----------------------------------------------------------

Resultado baixarArquivo(Contexto& c, const json& a) {
    if (!a.contains("id") || !a["id"].is_string()) {
        return Resultado::falha("Informe `id` do arquivo (de listar_arquivos ou materia_da_prova).");
    }
    const std::string id = a["id"];
    const ArquivoTurma* arq = nullptr;
    for (const auto& x : c.snapshot().arquivos) {
        if (x.idArquivo == id) arq = &x;
    }
    if (!arq) return Resultado::falha("Arquivo desconhecido: " + id + ".");
    c.turmaUsada = arq->idTurma;
    const Turma* t = turmaPorId(c.snapshot(), arq->idTurma);
    if (!t) return Resultado::falha("A turma deste arquivo não está mais no banco.");
    if (c.materiais().empty()) return Resultado::falha("Sem pasta de materiais configurada.");

    const bool caminhos = c.permite(Permissao::Arquivos);
    auto pronto = [&](const std::string& caminho, bool baixouAgora) {
        Resultado r;
        r.dados = {{"id", id}, {"titulo", arq->titulo}, {"baixado_agora", baixouAgora},
                   {"orcamento_restante", restante()}};
        if (caminhos) r.dados["caminho"] = caminho;
        r.texto = std::string(baixouAgora ? "Baixado" : "Já estava no disco") + ": " + arq->titulo +
                  (caminhos ? " — " + caminho
                            : ". O caminho não foi incluído: o aluno não liberou o acesso aos "
                              "materiais.");
        if (caminhos) {
            r.extras.push_back({{"type", "resource_link"},
                                {"uri", "sigaa://arquivo/" + id},
                                {"name", arq->titulo},
                                {"mimeType", tipoMime(caminho)}});
        }
        return r;
    };

    // Já baixado: nada de rede, nada do orçamento.
    if (const std::string cam = caminhoDoArquivo(c, *arq); !cam.empty()) return pronto(cam, false);

    std::lock_guard<std::mutex> g(mutexRede);
    auto cred = credenciais();
    if (!cred) return Resultado::falha(kSemCredenciais);
    if (auto recusa = reservar(kCustoBaixar)) return Resultado::falha(*recusa);

    http::SigaaSession sess;
    std::string erro;
    const bool entrou = sess.login(cred->login, cred->senha, &erro);
    apagar(cred->senha);
    if (!entrou) return Resultado::falha("O login no SIGAA falhou: " + erro);

    // O portal de novo, e não o frontEndId guardado: o ViewState vale para a
    // página em que foi lido, e a turma se abre a partir do portal desta sessão.
    const auto rp = sess.irParaPortal();
    html::Document portal;
    if (!rp.ok() || !portal.parse(rp.body)) return Resultado::falha("O portal do SIGAA não abriu.");
    const auto snap = parse::parsePortal(portal);
    const Turma* alvo = nullptr;
    for (const auto& x : snap.turmas) {
        if (x.idTurma == t->idTurma || (!x.frontEndId.empty() && x.nome == t->nome)) alvo = &x;
    }
    if (!alvo) return Resultado::falha("A turma " + t->nome + " não apareceu no portal.");

    sync::SessaoTurma turma(sess);
    if (!turma.entrar(*alvo, &erro) || !turma.abrirArquivos(&erro)) {
        return Resultado::falha("Não consegui abrir a aba Arquivos de " + t->nome + ": " + erro);
    }
    const std::string pasta = sync::pastaDaTurma(c.materiais(), t->nome);
    std::error_code ec;
    fs::create_directories(util::deUtf8(pasta), ec);
    const auto baixado = turma.baixar(id, pasta, &erro);
    if (!baixado) return Resultado::falha("O download falhou: " + erro);
    const std::string definitivo = sync::CacheLocal(pasta).registrar(id, *baixado);
    return pronto(definitivo, true);
}

// --- atualizar_turma ---------------------------------------------------------

Resultado atualizarTurma(Contexto& c, const json& a) {
    if (!a.contains("turma") || !a["turma"].is_string()) return Resultado::falha("Informe a turma.");
    const auto e = acharTurma(c.snapshot(), a["turma"].get<std::string>());
    if (!e.turma) return Resultado::falha(e.erro);
    const Turma t = *e.turma;
    c.turmaUsada = t.idTurma;
    if (c.caminhoBanco().empty()) return Resultado::falha("Sem caminho do banco.");

    std::lock_guard<std::mutex> g(mutexRede);
    auto cred = credenciais();
    if (!cred) return Resultado::falha(kSemCredenciais);
    if (auto recusa = reservar(kCustoAtualizar)) return Resultado::falha(*recusa);

    servico::Opcoes op;
    op.login = cred->login;
    op.senha = cred->senha;
    apagar(cred->senha);
    op.incluirTurmas = true;
    op.apenasTurmas = {t.idTurma};
    op.participantesSeBancoVazio = false;   // dado de colega: o agente não pediu
    op.caminhoBanco = c.caminhoBanco();
    // Ao lado do banco, como faz o app; nunca na pasta de onde o agente rodou.
    const fs::path dir = util::deUtf8(c.caminhoBanco()).parent_path();
    op.caminhoRelatorio = util::paraUtf8(dir / "relatorio.html");
    // Sem baixar o material da turma inteira: baixar_arquivo existe para o
    // arquivo que interessa, e um download em massa estouraria o orçamento.
    op.pastaMateriais.clear();

    const auto r = servico::executar(std::move(op));
    if (!r.ok()) return Resultado::falha("A atualização de " + t.nome + " falhou: " + r.erro);

    json novidades = json::array();
    for (const auto& ev : r.diff.eventos) {
        if (ev.idTurma == t.idTurma) novidades.push_back({{"titulo", ev.titulo}, {"detalhe", ev.detalhe}});
    }
    Resultado res;
    res.dados = {{"turma", t.nome}, {"novidades", novidades},
                 {"orcamento_restante", restante()}};
    res.texto = t.nome + " atualizada no SIGAA. " +
                (novidades.empty() ? std::string("Nada novo desde a última coleta.")
                                   : std::to_string(novidades.size()) + " novidade(s):");
    for (const auto& n : novidades) res.texto += "\n- " + n["titulo"].get<std::string>();
    return res;
}

} // namespace

void definirProvedorDeCredenciais(ProvedorCredenciais p) { provedor = std::move(p); }

void zerarOrcamentoDeRede() {
    std::lock_guard<std::mutex> g(mutexRede);
    gasto = 0;
    ultima = 0;
}

const std::vector<Ferramenta>& ferramentasDeRede() {
    static const std::vector<Ferramenta> fs = [] {
        std::vector<Ferramenta> v;
        Ferramenta f;
        f = {"baixar_arquivo", "Baixar arquivo do SIGAA",
             "Baixa do SIGAA um material da turma que ainda não está no disco (o `id` vem de "
             "listar_arquivos ou materia_da_prova, onde `baixado` é false). Se já estiver "
             "baixado, só devolve o caminho. Gasta parte de um orçamento pequeno por conversa: "
             "baixe só o que vai ler.",
             {{"type", "object"},
              {"properties", {{"id", {{"type", "string"}, {"description", "id do arquivo"}}}}},
              {"required", {"id"}}},
             baixarArquivo, Permissao::Rede, false};
        v.push_back(f);
        f = {"atualizar_turma", "Atualizar turma no SIGAA",
             "Busca no SIGAA as novidades de UMA turma (tópicos, arquivos, notícias, "
             "frequência, provas) e grava no app. Use quando o aluno disser que o professor "
             "publicou algo novo. Custa várias requisições do orçamento da conversa.",
             {{"type", "object"},
              {"properties", {{"turma", {{"type", "string"}, {"description", "nome, sigla ou código"}}}}},
              {"required", {"turma"}}},
             atualizarTurma, Permissao::Rede, false};
        v.push_back(f);
        return v;
    }();
    return fs;
}

} // namespace sigaa::mcp
