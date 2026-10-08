// O acesso mobile (src/web, docs/WEB.md).
//
// Três camadas, como o código: o pareamento (funções puras), a API (caminho
// entra, JSON sai, sem socket) e o servidor de verdade numa porta de
// 127.0.0.1 — é só nele que dá para provar que a porta recusa quem não tem o
// código, e que nenhuma rota escapa dessa checagem.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/store/Database.h"
#include "core/sync/Baixador.h"
#include "web/Api.h"
#include "web/Paginas.h"
#include "web/Pareamento.h"
#include "web/Servidor.h"

using namespace sigaa;
using nlohmann::json;

namespace {

DateTime emDias(int n, int hora = -1) {
    const std::time_t t = std::time(nullptr) + static_cast<std::time_t>(n) * 86400;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    DateTime d;
    d.year = tm.tm_year + 1900;
    d.month = tm.tm_mon + 1;
    d.day = tm.tm_mday;
    if (hora >= 0) {
        d.hour = hora;
        d.hasTime = true;
    }
    return d;
}

struct Ambiente {
    std::filesystem::path dir;
    web::Fonte fonte;

    Ambiente() {
        static int n = 0;
        dir = std::filesystem::temp_directory_path() / ("sigaa-teste-web-" + std::to_string(++n));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        fonte.banco = (dir / "sigaa-viewer.db").string();
        fonte.materiais = (dir / "SIGAA").string();

        Snapshot s;
        Turma comp;
        comp.idTurma = "102";
        comp.codigo = "ECO2207";
        comp.nome = "COMPILADORES";
        comp.periodo = "2026.2";
        // Aula todos os dias úteis: a agenda de qualquer semana tem o que mostrar.
        comp.horario = "23456T34";
        comp.local = "Laboratório 2";
        s.turmas = {comp};

        TopicoAula tp;
        tp.idTurma = "102";
        tp.titulo = "Análise LL(1)";
        tp.inicio = tp.fim = emDias(0);
        s.topicos = {tp};

        Avaliacao p;
        p.idTurma = "102";
        p.turmaNome = "COMPILADORES";
        p.descricao = "Prova 2";
        p.quando = emDias(10, 15);
        s.avaliacoes = {p};

        Atividade at;
        at.atividadeId = "1";
        at.idTurma = "102";
        at.turmaNome = "COMPILADORES";
        at.tipo = "Tarefa";
        at.titulo = "Lista 3";
        at.prazo = emDias(2, 23);
        at.status = StatusAtividade::Pendente;
        s.atividades = {at};

        ArquivoTurma a;
        a.idTurma = "102";
        a.turmaNome = "COMPILADORES";
        a.idArquivo = "9002";
        a.titulo = "Lista LL(1).pdf";
        ArquivoTurma b = a;
        b.idArquivo = "9003";
        b.titulo = "Nao baixado.pdf";
        s.arquivos = {a, b};

        Noticia nt;
        nt.idTurma = "102";
        nt.turmaNome = "COMPILADORES";
        nt.idNoticia = "77";
        nt.titulo = "Aviso";
        nt.data = emDias(-1, 14);
        nt.conteudoHtml = "<p>Texto <script>alert(1)</script>do professor</p>";
        s.noticias = {nt};

        Participante colega;
        colega.idTurma = "102";
        colega.turmaNome = "COMPILADORES";
        colega.nome = "FULANA DE TAL COLEGA";
        s.participantes = {colega};

        store::Database db(fonte.banco);
        REQUIRE(db.migrar());
        REQUIRE(db.gravar(s, 1000));

        const auto pasta = std::filesystem::path(sync::pastaDaTurma(fonte.materiais, "COMPILADORES"));
        std::filesystem::create_directories(pasta);
        const auto pdf = (pasta / "Lista LL(1).pdf").string();
        std::ofstream(pdf, std::ios::binary) << "%PDF-1.4 conteudo";
        sync::CacheLocal(pasta.string()).registrar("9002", pdf);
    }
    ~Ambiente() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    json get(const std::string& caminho, int status = 200) {
        const web::Resposta r = web::responder(fonte, caminho, {});
        REQUIRE(r.status == status);
        return json::parse(r.corpo);
    }
};

// Uma porta por teste, longe das comuns, para dois testes não brigarem.
int portaLivre() {
    static int n = 0;
    return 38000 + static_cast<int>(std::time(nullptr) % 1000) + (++n) * 7;
}

} // namespace

TEST_CASE("web: token tem 43 caracteres base64url e nao se repete", "[web]") {
    const std::string a = web::gerarToken();
    const std::string b = web::gerarToken();
    CHECK(a.size() == 43);
    CHECK(a != b);
    CHECK(a.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
          std::string::npos);
}

TEST_CASE("web: PIN so de digitos, de 6 a 12", "[web]") {
    CHECK(web::pinValido("123456"));
    CHECK(web::pinValido("123456789012"));
    CHECK_FALSE(web::pinValido("12345"));
    CHECK_FALSE(web::pinValido("1234567890123"));
    CHECK_FALSE(web::pinValido("12345a"));
    CHECK_FALSE(web::pinValido(""));
}

TEST_CASE("web: PIN guardado com hash e conferido", "[web]") {
    Ambiente amb;
    store::Database db(amb.fonte.banco);
    CHECK_FALSE(web::temPin(db));
    CHECK_FALSE(web::pinConfere(db, "123456"));
    CHECK_FALSE(web::definirPin(db, "123"));
    REQUIRE(web::definirPin(db, "246810"));
    CHECK(web::temPin(db));
    CHECK(web::pinConfere(db, "246810"));
    CHECK_FALSE(web::pinConfere(db, "246811"));
    // O PIN não fica legível no banco.
    CHECK(db.lerMeta("web.pin").value_or("").find("246810") == std::string::npos);
    REQUIRE(web::removerPin(db));
    CHECK_FALSE(web::pinConfere(db, "246810"));
}

TEST_CASE("web: parear cria um aparelho com token proprio", "[web]") {
    Ambiente amb;
    store::Database db(amb.fonte.banco);
    const std::string codigo = web::tokenOuNovo(db);
    REQUIRE(web::definirPin(db, "135790"));

    CHECK(web::parear(db, {"errado", "", "x", "1.2.3.4"}, 10).empty());
    CHECK(web::parear(db, {"", "000000", "x", "1.2.3.4"}, 10).empty());
    CHECK(db.dispositivosWeb().empty());

    const std::string t1 = web::parear(db, {codigo, "", "Android · Chrome", "100.1.1.1"}, 10);
    const std::string t2 = web::parear(db, {"", "135790", std::string(200, 'x'), "100.1.1.2"}, 20);
    REQUIRE(t1.size() == 43);
    REQUIRE(t2.size() == 43);
    CHECK(t1 != t2);
    CHECK(t1 != codigo);

    const auto lista = db.dispositivosWeb();
    REQUIRE(lista.size() == 2);
    CHECK(lista[0].via == "pin");
    CHECK(lista[0].nome.size() <= 60);
    CHECK(lista[1].nome == "Android · Chrome");
    CHECK(lista[1].via == "qr");

    // O token não é guardado, só o hash; e é ele (não o código) que autoriza.
    CHECK(web::aparelhoDoCabecalho(db, "Bearer " + t1)->id == lista[1].id);
    CHECK_FALSE(web::aparelhoDoCabecalho(db, "Bearer " + codigo));
    CHECK_FALSE(web::aparelhoDoCabecalho(db, t1));
    CHECK_FALSE(web::aparelhoDoCabecalho(db, "Bearer "));

    // Desconectar um não mexe no outro.
    CHECK(db.removerDispositivosWeb(lista[1].id) == 1);
    CHECK_FALSE(web::aparelhoDoCabecalho(db, "Bearer " + t1));
    CHECK(web::aparelhoDoCabecalho(db, "Bearer " + t2));
}

TEST_CASE("web: todas as interfaces sao recusadas", "[web]") {
    CHECK(web::enderecoAberto("0.0.0.0"));
    CHECK(web::enderecoAberto("::"));
    CHECK(web::enderecoAberto(""));
    CHECK_FALSE(web::enderecoAberto("127.0.0.1"));
    CHECK_FALSE(web::enderecoAberto("100.101.2.3"));
}

TEST_CASE("web: o token vai no fragmento da URL", "[web]") {
    CHECK(web::urlDePareamento("100.64.1.2", 8765, "XYZ") == "http://100.64.1.2:8765/#t=XYZ");
    CHECK(web::urlDePareamento("fd7a::1", 8765, "XYZ") == "http://[fd7a::1]:8765/#t=XYZ");
}

TEST_CASE("web: o codigo novo do QR substitui o anterior", "[web]") {
    Ambiente amb;
    store::Database db(amb.fonte.banco);
    CHECK(web::tokenAtual(db).empty());
    const std::string t1 = web::tokenOuNovo(db);
    CHECK(web::tokenOuNovo(db) == t1);
    const std::string t2 = web::novoToken(db);
    CHECK(t2 != t1);
    CHECK(web::tokenAtual(db) == t2);
}

TEST_CASE("web: paginas embutidas", "[web]") {
    const web::Pagina* raiz = web::acharPagina("/");
    REQUIRE(raiz != nullptr);
    CHECK(std::string(raiz->caminho) == "/index.html");
    CHECK(std::string(raiz->tipo).rfind("text/html", 0) == 0);
    CHECK(web::acharPagina("/app.js") != nullptr);
    CHECK(web::acharPagina("/app.css") != nullptr);
    CHECK(web::acharPagina("/../CMakeLists.txt") == nullptr);
    CHECK(web::acharPagina("/nada.html") == nullptr);
}

TEST_CASE("web: turmas sem participantes e com arquivos baixados", "[web]") {
    Ambiente amb;
    const json t = amb.get("/api/turmas");
    REQUIRE(t["turmas"].size() == 1);
    CHECK(t["turmas"][0]["arquivos"] == 2);
    CHECK(t["turmas"][0]["baixados"] == 1);
    CHECK(t["turmas"][0]["proxima_prova"]["descricao"] == "Prova 2");

    const json d = amb.get("/api/turmas/102");
    CHECK(d.dump().find("FULANA") == std::string::npos);
    CHECK(d["arquivos"].size() == 2);
    REQUIRE(d["noticias"].size() == 1);
    // Texto puro: nenhuma tag do professor chega ao celular.
    const std::string txt = d["noticias"][0]["texto"];
    CHECK(txt.find('<') == std::string::npos);
}

TEST_CASE("web: agenda tem sete dias e a aula de hoje com o topico", "[web]") {
    Ambiente amb;
    const json a = amb.get("/api/agenda");
    REQUIRE(a["dias"].size() == 7);
    CHECK(a["semana"]["deslocamento"] == 0);
    CHECK(a["prazos"].size() == 1);
    bool achou = false;
    for (const auto& d : a["dias"]) {
        if (!d["hoje"].get<bool>()) continue;
        for (const auto& au : d["aulas"]) {
            if (au["topico"] == "Análise LL(1)") achou = true;
        }
    }
    CHECK(achou);

    // Outra semana: a mesma grade, deslocada.
    const DateTime prox = emDias(7);
    const web::Resposta r = web::responder(amb.fonte, "/api/agenda", {{"semana", prox.toIso()}});
    CHECK(json::parse(r.corpo)["semana"]["deslocamento"] == 1);
}

TEST_CASE("web: rotas invalidas e ids que nao sao numeros", "[web]") {
    Ambiente amb;
    amb.get("/api/nada", 404);
    amb.get("/api/turmas/abc", 404);
    amb.get("/api/turmas/999", 404);
    amb.get("/api/arquivos/102/..", 404);
    amb.get("/api/arquivos/102/9003", 404);   // conhecido, mas não baixado

    const web::Resposta r = web::responder(amb.fonte, "/api/arquivos/102/9002", {});
    CHECK(r.status == 200);
    CHECK(r.tipo == "application/pdf");
    CHECK(std::filesystem::exists(r.arquivo));
}

TEST_CASE("web: servidor recusa 0.0.0.0", "[web]") {
    Ambiente amb;
    web::Config c;
    c.host = "0.0.0.0";
    c.porta = portaLivre();
    c.fonte = amb.fonte;
    web::Servidor s(c);
    std::string erro;
    CHECK_FALSE(s.iniciar(&erro));
    CHECK_FALSE(erro.empty());
}

namespace {

// Pareia pelo servidor de verdade e devolve o cabeçalho do aparelho.
httplib::Headers parearPorHttp(httplib::Client& cli, const json& corpo) {
    auto r = cli.Post("/api/parear", corpo.dump(), "application/json");
    REQUIRE(r);
    REQUIRE(r->status == 200);
    return {{"Authorization", "Bearer " + json::parse(r->body)["token"].get<std::string>()}};
}

} // namespace

TEST_CASE("web: servidor exige um aparelho pareado em toda rota de dados", "[web]") {
    Ambiente amb;
    std::string codigo;
    {
        store::Database db(amb.fonte.banco);
        codigo = web::tokenOuNovo(db);
    }
    web::Config c;
    c.host = "127.0.0.1";
    c.porta = portaLivre();
    c.fonte = amb.fonte;
    web::Servidor s(c);
    std::string erro;
    REQUIRE(s.iniciar(&erro));

    httplib::Client cli(c.host, c.porta);

    // A página abre sem token (não tem dado nenhum) e vem com a CSP fechada.
    auto pag = cli.Get("/");
    REQUIRE(pag);
    CHECK(pag->status == 200);
    CHECK(pag->get_header_value("Content-Security-Policy").find("default-src 'none'") !=
          std::string::npos);
    CHECK(pag->get_header_value("X-Content-Type-Options") == "nosniff");

    for (const char* rota : {"/api/resumo", "/api/turmas", "/api/turmas/102", "/api/nada",
                             "/api/arquivos/102/9002"}) {
        auto r = cli.Get(rota);
        REQUIRE(r);
        CHECK(r->status == 401);
    }
    // O código do QR não serve como acesso: só para parear.
    auto cru = cli.Get("/api/resumo", httplib::Headers{{"Authorization", "Bearer " + codigo}});
    REQUIRE(cru);
    CHECK(cru->status == 401);

    const httplib::Headers aparelho = parearPorHttp(cli, {{"codigo", codigo}, {"nome", "Teste"}});
    auto ok = cli.Get("/api/resumo", aparelho);
    REQUIRE(ok);
    CHECK(ok->status == 200);
    CHECK(ok->get_header_value("Cache-Control") == "no-store");
    CHECK(json::parse(ok->body)["contagens"]["turmas"] == 1);

    auto pdf = cli.Get("/api/arquivos/102/9002", aparelho);
    REQUIRE(pdf);
    CHECK(pdf->status == 200);
    CHECK(pdf->body == "%PDF-1.4 conteudo");

    // Somente leitura: nenhum outro método passa, nem pareado.
    auto post = cli.Post("/api/resumo", aparelho, "{}", "application/json");
    REQUIRE(post);
    CHECK(post->status == 405);

    // Trocar o QR não derruba quem já está pareado...
    {
        store::Database db(amb.fonte.banco);
        web::novoToken(db);
    }
    auto ainda = cli.Get("/api/resumo", aparelho);
    REQUIRE(ainda);
    CHECK(ainda->status == 200);
    // ...mas o código velho não pareia mais.
    auto velho = cli.Post("/api/parear", json{{"codigo", codigo}}.dump(), "application/json");
    REQUIRE(velho);
    CHECK(velho->status == 401);

    // Desconectar na janela derruba no pedido seguinte, sem reiniciar.
    {
        store::Database db(amb.fonte.banco);
        db.removerDispositivosWeb(0);
    }
    auto fora = cli.Get("/api/resumo", aparelho);
    REQUIRE(fora);
    CHECK(fora->status == 401);

    s.parar();
    CHECK_FALSE(s.rodando());
}

TEST_CASE("web: PIN pareia, e erros demais o bloqueiam para todos", "[web]") {
    Ambiente amb;
    {
        store::Database db(amb.fonte.banco);
        REQUIRE(web::definirPin(db, "975310"));
    }
    web::Config c;
    c.host = "127.0.0.1";
    c.porta = portaLivre();
    c.fonte = amb.fonte;
    web::Servidor s(c);
    REQUIRE(s.iniciar());
    httplib::Client cli(c.host, c.porta);

    const httplib::Headers aparelho = parearPorHttp(cli, {{"pin", "975310"}, {"nome", "iPhone"}});
    auto ok = cli.Get("/api/turmas", aparelho);
    REQUIRE(ok);
    CHECK(ok->status == 200);

    auto lixo = cli.Post("/api/parear", "não é json", "application/json");
    REQUIRE(lixo);
    CHECK(lixo->status == 400);

    for (int i = 0; i < 5; ++i) {
        auto r = cli.Post("/api/parear", json{{"pin", "000000"}}.dump(), "application/json");
        REQUIRE(r);
        CHECK(r->status == 401);
    }
    // Bloqueado: nem o PIN certo passa até a janela acabar.
    auto certo = cli.Post("/api/parear", json{{"pin", "975310"}}.dump(), "application/json");
    REQUIRE(certo);
    CHECK(certo->status == 429);
    // Quem já estava pareado segue usando.
    auto segue = cli.Get("/api/turmas", aparelho);
    REQUIRE(segue);
    CHECK(segue->status == 200);
}
