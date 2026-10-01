// O instalador do servidor MCP e o kit de estudo (src/mcp/Instalar, Kit).
//
// O instalador mexe em arquivo de OUTRO programa — a configuração do agente
// do aluno, que pode ter outros servidores e preferências. Os testes são
// sobre o que não pode acontecer: perder ou reordenar o que já estava lá,
// regravar um arquivo que não entendemos, e deixar lixo ao desinstalar.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/store/Database.h"
#include "core/sync/Baixador.h"
#include "mcp/Instalar.h"
#include "mcp/Kit.h"

using namespace sigaa;
using namespace sigaa::mcp;
namespace fs = std::filesystem;

namespace {

struct Casa {
    fs::path dir;
    Pastas pastas;
    Casa() {
        static int n = 0;
        dir = fs::temp_directory_path() / ("sigaa-teste-casa-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir);
        pastas.home = dir.string();
        pastas.appdata = (dir / "AppData").string();
    }
    ~Casa() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string ler(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void escrever(const std::string& p, const std::string& s) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

Lancamento exemplo() {
    return {"/opt/SIGAA Viewer/sigaa-cli", {"mcp", "--banco", "/home/a/sigaa-viewer.db"}};
}

} // namespace

TEST_CASE("instalar: JSON preserva os outros servidores e a ordem das chaves", "[instalar]") {
    Casa casa;
    const std::string arq = arquivoDeConfig(Cliente::Gemini, casa.pastas);
    escrever(arq, R"({"theme": "dark", "mcpServers": {"outro": {"command": "x"}}, "auth": 1})");

    const Resposta r = instalar(Cliente::Gemini, exemplo(), false, casa.pastas);
    REQUIRE(r.ok);
    const std::string depois = ler(arq);
    const auto j = nlohmann::ordered_json::parse(depois);
    CHECK(j["mcpServers"]["outro"]["command"] == "x");
    CHECK(j["mcpServers"]["sigaa"]["command"] == "/opt/SIGAA Viewer/sigaa-cli");
    CHECK(j["mcpServers"]["sigaa"]["args"][0] == "mcp");
    CHECK_FALSE(j["mcpServers"]["sigaa"].contains("type"));
    // A ordem de antes: theme, mcpServers, auth.
    CHECK(depois.find("\"theme\"") < depois.find("\"mcpServers\""));
    CHECK(depois.find("\"mcpServers\"") < depois.find("\"auth\""));
    CHECK(fs::exists(arq + ".bak"));
    CHECK(instalado(Cliente::Gemini, casa.pastas));

    // Reinstalar não duplica; remover tira só o sigaa.
    REQUIRE(instalar(Cliente::Gemini, exemplo(), false, casa.pastas).ok);
    REQUIRE(remover(Cliente::Gemini, casa.pastas).ok);
    const auto k = nlohmann::json::parse(ler(arq));
    CHECK_FALSE(k["mcpServers"].contains("sigaa"));
    CHECK(k["mcpServers"].contains("outro"));
    CHECK_FALSE(instalado(Cliente::Gemini, casa.pastas));
}

TEST_CASE("instalar: VS Code usa servers e type stdio; Claude Code, mcpServers com type",
          "[instalar]") {
    Casa casa;
    REQUIRE(instalar(Cliente::VSCode, exemplo(), false, casa.pastas).ok);
    auto j = nlohmann::json::parse(ler(arquivoDeConfig(Cliente::VSCode, casa.pastas)));
    CHECK(j["servers"]["sigaa"]["type"] == "stdio");

    REQUIRE(instalar(Cliente::ClaudeCode, exemplo(), false, casa.pastas).ok);
    j = nlohmann::json::parse(ler(arquivoDeConfig(Cliente::ClaudeCode, casa.pastas)));
    CHECK(j["mcpServers"]["sigaa"]["type"] == "stdio");
}

TEST_CASE("instalar: JSON com comentario nao e regravado", "[instalar]") {
    Casa casa;
    const std::string arq = arquivoDeConfig(Cliente::VSCode, casa.pastas);
    const std::string original = "{\n  // meu servidor\n  \"servers\": {}\n}\n";
    escrever(arq, original);
    const Resposta r = instalar(Cliente::VSCode, exemplo(), false, casa.pastas);
    CHECK_FALSE(r.ok);
    CHECK(r.mensagem.find("comentários") != std::string::npos);
    CHECK(r.trecho.find("\"sigaa\"") != std::string::npos);   // para colar à mão
    CHECK(ler(arq) == original);
    CHECK_FALSE(fs::exists(arq + ".bak"));
}

TEST_CASE("instalar: imprimir nao grava nada", "[instalar]") {
    Casa casa;
    const Resposta r = instalar(Cliente::Cursor, exemplo(), true, casa.pastas);
    CHECK(r.ok);
    CHECK_FALSE(fs::exists(arquivoDeConfig(Cliente::Cursor, casa.pastas)));
    CHECK(r.trecho.find("sigaa-cli") != std::string::npos);
}

TEST_CASE("instalar: Codex em TOML, com caminho do Windows escapado", "[instalar]") {
    Casa casa;
    const std::string arq = arquivoDeConfig(Cliente::Codex, casa.pastas);
    escrever(arq,
             "model = \"gpt-5\"\n\n[mcp_servers.outro]\ncommand = \"x\"\n\n"
             "[mcp_servers.sigaa]\ncommand = \"velho\"\n\n[mcp_servers.sigaa.env]\nA = \"1\"\n\n"
             "[profiles.rapido]\nmodel = \"o4\"\n");

    const Lancamento win{"C:\\Programas\\SIGAA\\sigaa-cli.exe", {"mcp", "--banco", "C:\\x\\a \"b\".db"}};
    REQUIRE(instalar(Cliente::Codex, win, false, casa.pastas).ok);
    const std::string t = ler(arq);
    CHECK(t.find("model = \"gpt-5\"") != std::string::npos);
    CHECK(t.find("[mcp_servers.outro]") != std::string::npos);
    CHECK(t.find("[profiles.rapido]") != std::string::npos);
    CHECK(t.find("velho") == std::string::npos);
    CHECK(t.find("[mcp_servers.sigaa.env]") == std::string::npos);
    CHECK(t.find(R"(command = "C:\\Programas\\SIGAA\\sigaa-cli.exe")") != std::string::npos);
    CHECK(t.find(R"("C:\\x\\a \"b\".db")") != std::string::npos);
    // Uma seção sigaa só.
    CHECK(t.find("[mcp_servers.sigaa]") == t.rfind("[mcp_servers.sigaa]"));

    REQUIRE(remover(Cliente::Codex, casa.pastas).ok);
    const std::string u = ler(arq);
    CHECK(u.find("mcp_servers.sigaa") == std::string::npos);
    CHECK(u.find("[profiles.rapido]") != std::string::npos);
    CHECK(u.find("[mcp_servers.outro]") != std::string::npos);
}

TEST_CASE("instalar: detectado pela pasta do agente", "[instalar]") {
    Casa casa;
    CHECK_FALSE(detectado(Cliente::Codex, casa.pastas));
    fs::create_directories(casa.dir / ".codex");
    CHECK(detectado(Cliente::Codex, casa.pastas));
    CHECK_FALSE(detectado(Cliente::ClaudeCode, casa.pastas));
    fs::create_directories(casa.dir / ".claude");
    CHECK(detectado(Cliente::ClaudeCode, casa.pastas));
}

TEST_CASE("instalar: lancamento leva caminhos absolutos e prefere o AppImage", "[instalar]") {
    const auto l = lancamento("sigaa-viewer.db", "materiais");
    REQUIRE(l.args.size() == 5);
    CHECK(fs::path(l.args[2]).is_absolute());
    CHECK(fs::path(l.args[4]).is_absolute());
    CHECK_FALSE(l.comando.empty());
}

TEST_CASE("kit: pasta com turma.md, materia, leia-me e os arquivos baixados", "[kit]") {
    Casa casa;
    const std::string banco = (casa.dir / "sigaa-viewer.db").string();
    const std::string materiais = (casa.dir / "SIGAA").string();
    {
        Snapshot s;
        Turma t;
        t.idTurma = "102";
        t.nome = "COMPILADORES";
        s.turmas = {t};
        TopicoAula tp;
        tp.idTurma = "102";
        tp.titulo = "Análise LL(1)";
        tp.inicio = {2026, 9, 21};
        s.topicos = {tp};
        Avaliacao a;
        a.idTurma = "102";
        a.turmaNome = "COMPILADORES";
        a.descricao = "Prova 1";
        a.quando = {2026, 10, 8};
        s.avaliacoes = {a};
        ArquivoTurma ar;
        ar.idTurma = "102";
        ar.idArquivo = "9002";
        ar.titulo = "Lista.pdf";
        ar.topico = "Análise LL(1)";
        ArquivoTurma falta = ar;
        falta.idArquivo = "9003";
        falta.titulo = "Slides.pdf";
        s.arquivos = {ar, falta};
        store::Database db(banco);
        REQUIRE(db.migrar());
        REQUIRE(db.gravar(s, 1));
    }
    const auto pasta = fs::path(sync::pastaDaTurma(materiais, "COMPILADORES"));
    fs::create_directories(pasta);
    std::ofstream(pasta / "Lista.pdf") << "pdf";
    sync::CacheLocal(pasta.string()).registrar("9002", (pasta / "Lista.pdf").string());

    store::Database db(banco);
    const Kit k = exportarKit(db, materiais, "compiladores", "P1");
    REQUIRE(k.ok);
    CHECK(k.copiados == 1);
    CHECK(k.faltando == 1);
    const fs::path p = k.pasta;
    CHECK(p.filename() == "kit-para-ia-Prova 1");
    CHECK(fs::exists(p / "turma.md"));
    CHECK(fs::exists(p / "LEIA-ME-AGENTE.md"));
    CHECK(fs::exists(p / "arquivos" / "Lista.pdf"));
    CHECK(ler((p / "materia-da-prova.md").string()).find("Análise LL(1)") != std::string::npos);

    const Kit ruim = exportarKit(db, materiais, "quimica", "");
    CHECK_FALSE(ruim.ok);
}
