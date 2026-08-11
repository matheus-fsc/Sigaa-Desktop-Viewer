// Fila de download em lote e cache offline.
//
// Estes testes NAO tocam a rede: o Baixador recebe uma fabrica de canal, e aqui
// ela devolve um canal falso. E de proposito — a parte que erra em silencio e a
// concorrencia (ordem trocada, contador de progresso errado, dois canais
// gravando o mesmo manifesto), e ela nao pode depender do SIGAA estar no ar.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/sync/Baixador.h"
#include "core/util/Caminho.h"

using namespace sigaa;

namespace {

// Pasta temporaria propria por teste: o cache grava um manifesto de verdade,
// e testes que compartilham diretorio passam ou falham conforme a ordem.
class PastaTemp {
public:
    explicit PastaTemp(const std::string& marca) {
        p_ = std::filesystem::temp_directory_path() /
             ("sigaa-teste-" + marca + "-" +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(p_);
    }
    ~PastaTemp() {
        std::error_code ec;
        std::filesystem::remove_all(p_, ec);
    }
    PastaTemp(const PastaTemp&) = delete;
    PastaTemp& operator=(const PastaTemp&) = delete;

    std::string utf8() const { return util::paraUtf8(p_); }
    const std::filesystem::path& path() const { return p_; }

private:
    std::filesystem::path p_;
};

// Escreve um arquivo com o nome derivado do id e devolve o caminho, como o
// canal de verdade faria depois de gravar os bytes do PDF.
class CanalFalso : public sync::Baixador::Canal {
public:
    struct Compartilhado {
        std::atomic<int> emVoo{0};
        std::atomic<int> picoEmVoo{0};
        std::atomic<int> chamadas{0};
        std::vector<std::string> falharEstes;
        std::chrono::milliseconds atraso{0};
    };

    explicit CanalFalso(Compartilhado& c) : c_(c) {}

    std::optional<std::string> baixar(const std::string& id, const std::string& dir,
                                      std::string* erro) override {
        const int agora = ++c_.emVoo;
        int pico = c_.picoEmVoo.load();
        while (agora > pico && !c_.picoEmVoo.compare_exchange_weak(pico, agora)) {
        }
        ++c_.chamadas;
        if (c_.atraso.count() > 0) std::this_thread::sleep_for(c_.atraso);
        --c_.emVoo;

        for (const auto& f : c_.falharEstes) {
            if (f == id) {
                if (erro) *erro = "falha simulada de " + id;
                return std::nullopt;
            }
        }

        const auto destino = util::deUtf8(dir) / util::deUtf8("material-" + id + ".pdf");
        std::ofstream f(destino, std::ios::binary);
        f << "%PDF-" << id;
        return util::paraUtf8(destino);
    }

private:
    Compartilhado& c_;
};

std::vector<sync::PedidoDownload> pedidos(const std::vector<std::string>& ids) {
    std::vector<sync::PedidoDownload> v;
    for (const auto& i : ids) v.push_back({i, "material " + i});
    return v;
}

} // namespace

TEST_CASE("baixador: a saida sai na ordem dos pedidos, nao na de conclusao",
          "[baixador]") {
    PastaTemp pasta("ordem");
    CanalFalso::Compartilhado est;
    // Atraso para que os canais terminem fora de ordem de verdade.
    est.atraso = std::chrono::milliseconds(5);

    sync::Baixador b([&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         return std::make_unique<CanalFalso>(est);
                     },
                     pasta.utf8(), 3);
    b.setAtrasoEntreCanais(std::chrono::milliseconds(0));

    const auto ids = std::vector<std::string>{"10", "20", "30", "40", "50", "60"};
    const auto r = b.baixar(pedidos(ids));

    REQUIRE(r.size() == ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        CHECK(r[i].idArquivo == ids[i]);
        CHECK(r[i].ok());
        CHECK(r[i].caminho.find("material-" + ids[i]) != std::string::npos);
    }
}

TEST_CASE("baixador: com varios canais os downloads correm de fato juntos",
          "[baixador]") {
    PastaTemp pasta("paralelo");
    CanalFalso::Compartilhado est;
    est.atraso = std::chrono::milliseconds(30);

    sync::Baixador b([&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         return std::make_unique<CanalFalso>(est);
                     },
                     pasta.utf8(), 3);
    // Sem o escalonamento de abertura: aqui o "login" e instantaneo, entao os
    // 400 ms de espera fariam o primeiro canal terminar o lote sozinho antes
    // de os outros acordarem — e o teste mediria o relogio, nao a fila.
    b.setAtrasoEntreCanais(std::chrono::milliseconds(0));

    const auto r = b.baixar(pedidos({"1", "2", "3", "4", "5", "6"}));
    for (const auto& i : r) CHECK(i.ok());

    // Se o pico de downloads simultaneos fosse 1, a fila estaria serializando
    // sem dizer — e a promessa de "baixa tudo junto" seria decorativa.
    CHECK(est.picoEmVoo.load() > 1);
}

TEST_CASE("baixador: um canal so serializa, e e o padrao para poucos arquivos",
          "[baixador]") {
    PastaTemp pasta("serial");
    CanalFalso::Compartilhado est;
    est.atraso = std::chrono::milliseconds(10);

    sync::Baixador b([&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         return std::make_unique<CanalFalso>(est);
                     },
                     pasta.utf8(), 1);

    const auto r = b.baixar(pedidos({"1", "2", "3"}));
    for (const auto& i : r) CHECK(i.ok());
    CHECK(est.picoEmVoo.load() == 1);

    // O login custa mais que baixar um PDF de aula: nao vale abrir sessao
    // extra para dois arquivos.
    CHECK(sync::canaisRecomendados(1) == 1);
    CHECK(sync::canaisRecomendados(2) == 1);
    CHECK(sync::canaisRecomendados(6) > 1);
    // Teto: sao logins simultaneos na conta do aluno.
    CHECK(sync::canaisRecomendados(100) <= 3);
}

TEST_CASE("baixador: o que ja esta no disco nao vai para a rede de novo",
          "[baixador]") {
    PastaTemp pasta("cache");
    CanalFalso::Compartilhado est;

    auto fabrica = [&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
        return std::make_unique<CanalFalso>(est);
    };

    sync::Baixador b1(fabrica, pasta.utf8(), 1);
    const auto r1 = b1.baixar(pedidos({"7", "8"}));
    REQUIRE(r1[0].ok());
    CHECK_FALSE(r1[0].reaproveitado);
    CHECK(est.chamadas.load() == 2);

    // Segunda rodada: o manifesto ja tem os dois, e os arquivos continuam la.
    sync::Baixador b2(fabrica, pasta.utf8(), 1);
    const auto r2 = b2.baixar(pedidos({"7", "8"}));
    CHECK(r2[0].ok());
    CHECK(r2[0].reaproveitado);
    CHECK(r2[1].reaproveitado);
    CHECK(est.chamadas.load() == 2);   // nenhuma chamada nova
}

TEST_CASE("baixador: arquivo apagado pelo usuario volta a ser baixavel",
          "[baixador]") {
    PastaTemp pasta("apagado");
    CanalFalso::Compartilhado est;
    auto fabrica = [&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
        return std::make_unique<CanalFalso>(est);
    };

    sync::Baixador b1(fabrica, pasta.utf8(), 1);
    const auto r1 = b1.baixar(pedidos({"9"}));
    REQUIRE(r1[0].ok());

    // O aluno apaga o PDF, mas o manifesto continua dizendo que ele existe.
    // Confiar no manifesto aqui seria mostrar "offline" para um arquivo que
    // sumiu — e a pessoa fecharia o app achando que tem o material.
    std::filesystem::remove(util::deUtf8(r1[0].caminho));

    sync::Baixador b2(fabrica, pasta.utf8(), 1);
    const auto r2 = b2.baixar(pedidos({"9"}));
    CHECK(r2[0].ok());
    CHECK_FALSE(r2[0].reaproveitado);
    CHECK(est.chamadas.load() == 2);
}

TEST_CASE("baixador: uma falha nao derruba o resto do lote", "[baixador]") {
    PastaTemp pasta("falha");
    CanalFalso::Compartilhado est;
    est.falharEstes = {"2"};

    sync::Baixador b([&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         return std::make_unique<CanalFalso>(est);
                     },
                     pasta.utf8(), 2);

    const auto r = b.baixar(pedidos({"1", "2", "3"}));
    REQUIRE(r.size() == 3);
    CHECK(r[0].ok());
    CHECK_FALSE(r[1].ok());
    CHECK(r[1].erro.find("falha simulada") != std::string::npos);
    CHECK(r[2].ok());
}

TEST_CASE("baixador: canal que nao abre vira erro em todos os pedidos",
          "[baixador]") {
    PastaTemp pasta("semcanal");

    sync::Baixador b([](std::string* erro) -> std::unique_ptr<sync::Baixador::Canal> {
                         if (erro) *erro = "a sessao expirou";
                         return nullptr;
                     },
                     pasta.utf8(), 2);

    const auto r = b.baixar(pedidos({"1", "2"}));
    REQUIRE(r.size() == 2);
    for (const auto& i : r) {
        CHECK_FALSE(i.ok());
        CHECK(i.erro == "a sessao expirou");
    }
}

TEST_CASE("baixador: o progresso conta cada pedido uma vez so", "[baixador]") {
    PastaTemp pasta("progresso");
    CanalFalso::Compartilhado est;

    std::mutex m;
    std::vector<int> vistos;
    int totalVisto = 0;

    sync::Baixador b([&est](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         return std::make_unique<CanalFalso>(est);
                     },
                     pasta.utf8(), 3);

    b.baixar(pedidos({"1", "2", "3", "4"}), [&](int prontos, int total) {
        std::lock_guard<std::mutex> g(m);
        vistos.push_back(prontos);
        totalVisto = total;
    });

    CHECK(totalVisto == 4);
    REQUIRE(vistos.size() == 4);
    std::sort(vistos.begin(), vistos.end());
    CHECK(vistos == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("baixador: lista vazia nao abre canal nenhum", "[baixador]") {
    PastaTemp pasta("vazio");
    std::atomic<int> aberturas{0};

    sync::Baixador b([&aberturas](std::string*) -> std::unique_ptr<sync::Baixador::Canal> {
                         ++aberturas;
                         return nullptr;
                     },
                     pasta.utf8(), 3);

    CHECK(b.baixar({}).empty());
    CHECK(aberturas.load() == 0);
}

TEST_CASE("cache: o manifesto sobrevive a reabertura e conta o que existe",
          "[baixador]") {
    PastaTemp pasta("manifesto");

    const auto arq = pasta.path() / util::deUtf8("apostila.pdf");
    { std::ofstream f(arq, std::ios::binary); f << "%PDF-1.4"; }

    {
        sync::CacheLocal c(pasta.utf8());
        CHECK_FALSE(c.temNoDisco("123"));
        c.registrar("123", util::paraUtf8(arq));
        CHECK(c.temNoDisco("123"));
    }
    {
        sync::CacheLocal c(pasta.utf8());
        CHECK(c.temNoDisco("123"));
        CHECK(c.quantosNoDisco() == 1);
        CHECK(c.caminho("123").find("apostila.pdf") != std::string::npos);
    }
}

TEST_CASE("cache: acento no nome do arquivo sobrevive ao manifesto", "[baixador]") {
    // No Windows, path(std::string) le os bytes na code page ANSI e
    // "ANALISE" com acento vira outro nome. O manifesto guarda o nome; se ele
    // voltar diferente, o arquivo "some" e o app baixa tudo de novo.
    PastaTemp pasta("acento");
    const std::string nome = "Aula 1 - PROJETO E AN\xC3\x81LISE.pdf";   // UTF-8
    const auto arq = pasta.path() / util::deUtf8(nome);
    { std::ofstream f(arq, std::ios::binary); f << "%PDF-1.4"; }

    {
        sync::CacheLocal c(pasta.utf8());
        c.registrar("456", util::paraUtf8(arq));
    }
    sync::CacheLocal c(pasta.utf8());
    CHECK(c.temNoDisco("456"));
    CHECK(c.caminho("456") == util::paraUtf8(arq));
}

// ---------------------------------------------------------------------------
// Layout da pasta. UMA definicao, usada pelo sync e pela janela da turma: se as
// duas discordassem num acento, o sync encheria uma pasta e a janela procuraria
// noutra — o app mostraria "ainda nao baixado" para arquivo que esta no disco e
// baixaria tudo de novo, em silencio.
// ---------------------------------------------------------------------------

TEST_CASE("pasta da turma pendura o nome saneado na base", "[baixador]") {
    const auto p = sync::pastaDaTurma("C:/Docs/SIGAA", "ADMINISTRACAO");
    CHECK(p.find("SIGAA") != std::string::npos);
    CHECK(p.find("ADMINISTRACAO") != std::string::npos);
}

TEST_CASE("barra no nome da turma nao vira subpasta", "[baixador]") {
    // O SIGAA usa "/" em nome de turma ("TOPICOS I/II"). Sem sanear, o material
    // cairia em .../TOPICOS I/II/ — duas pastas, e a janela procuraria numa so.
    const auto p = sync::pastaDaTurma("/base", "TOPICOS I/II");
    CHECK(p.find("TOPICOS I-II") != std::string::npos);
    CHECK(p.find("I/II") == std::string::npos);
}

TEST_CASE("acento no nome da turma sobrevive ao caminho", "[baixador]") {
    // "PROJETO E ANALISE" com acento ja criou uma segunda pasta mojibake ao
    // lado da certa quando o path foi montado pela code page ANSI.
    const std::string nome = "PROJETO E AN\xC3\x81LISE";   // UTF-8
    const auto p = sync::pastaDaTurma("/base", nome);
    CHECK(p.find(nome) != std::string::npos);
}

TEST_CASE("turma sem nome ainda tem uma pasta", "[baixador]") {
    // Melhor uma pasta "turma" que um caminho igual a base, que despejaria o
    // material de uma turma sem nome na raiz junto com todas as outras.
    const auto p = sync::pastaDaTurma("/base", "");
    CHECK(p != "/base");
    CHECK(p.find("turma") != std::string::npos);
    // Base vazia continua vazia: quem nao configurou pasta nao quer download.
    CHECK(sync::pastaDaTurma("", "ADM").empty());
}

TEST_CASE("o cache encontra o que o sync gravou na mesma pasta", "[baixador]") {
    // O contrato entre as duas metades: o sync grava usando pastaDaTurma e a
    // janela le usando pastaDaTurma. Este teste falha se alguem mudar um lado.
    PastaTemp base("layout");
    const std::string pasta = sync::pastaDaTurma(base.utf8(), "ADMINISTRACAO");
    std::filesystem::create_directories(util::deUtf8(pasta));

    const auto arq = util::deUtf8(pasta) / util::deUtf8("apostila.pdf");
    { std::ofstream f(arq, std::ios::binary); f << "%PDF-1.4"; }

    sync::CacheLocal gravador(pasta);
    gravador.registrar("777", util::paraUtf8(arq));

    const sync::CacheLocal leitor(sync::pastaDaTurma(base.utf8(), "ADMINISTRACAO"));
    CHECK(leitor.temNoDisco("777"));
}
