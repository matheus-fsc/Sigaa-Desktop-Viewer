#include "app/Mcp.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "core/store/Database.h"
#include "core/util/Caminho.h"
#include "core/util/Pastas.h"
#include "mcp/Leitura.h"
#include "mcp/Servidor.h"

namespace sigaa::app {
namespace {

std::string ambiente(const char* n) {
    const char* v = std::getenv(n);
    return v ? std::string(v) : std::string();
}

struct Caminhos {
    std::string banco;
    std::string materiais;
};

// docs/MCP.md, D3: argumento > ambiente > ./sigaa-viewer.db. O agente lança o
// servidor de uma pasta qualquer; quem registra passa os dois caminhos.
Caminhos resolver(std::vector<std::string>& args) {
    Caminhos c;
    std::vector<std::string> resto;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--banco" && i + 1 < args.size()) c.banco = args[++i];
        else if (args[i] == "--materiais" && i + 1 < args.size()) c.materiais = args[++i];
        else resto.push_back(args[i]);
    }
    args = resto;
    if (c.banco.empty()) c.banco = ambiente("SIGAA_BANCO");
    if (c.banco.empty()) c.banco = "sigaa-viewer.db";
    if (c.materiais.empty()) c.materiais = ambiente("SIGAA_MATERIAIS");
    if (c.materiais.empty()) c.materiais = util::pastaMateriaisPadrao();
    std::error_code ec;
    const auto abs = std::filesystem::absolute(util::deUtf8(c.banco), ec);
    if (!ec) c.banco = util::paraUtf8(abs);
    return c;
}

const mcp::Permissao kTodas[] = {mcp::Permissao::Leitura, mcp::Permissao::Arquivos,
                                 mcp::Permissao::Escrita, mcp::Permissao::Rede};

int servir(const Caminhos& c) {
#ifdef _WIN32
    // Modo texto trocaria cada \n por \r\n e mexeria em bytes do UTF-8 que
    // por acaso valem 0x0A: o protocolo é de linhas, byte a byte.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    // Só para o stderr: o stdout é do protocolo.
    if (!std::filesystem::exists(util::deUtf8(c.banco))) {
        std::cerr << "sigaa-cli mcp: banco nao encontrado em " << c.banco
                  << " (as ferramentas vao responder com esse aviso)\n";
    }
    mcp::Servidor s({c.banco, c.materiais});
    s.rodar(std::cin, std::cout);
    return 0;
}

int consentimento(const Caminhos& c, const std::vector<std::string>& args, bool liberar) {
    store::Database db(c.banco, store::Database::Abertura::SoExistente);
    if (!db.aberto() || !db.migrar()) {
        std::cerr << "nao achei o banco em " << c.banco
                  << ". Rode na pasta do sigaa-viewer.db ou passe --banco.\n";
        return 2;
    }
    if (args.size() < 2) {
        std::cerr << "uso: sigaa-cli mcp " << (liberar ? "permitir" : "bloquear")
                  << " leitura|arquivos|escrita|rede|todas ...\n";
        return 2;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        std::vector<mcp::Permissao> ps;
        if (args[i] == "todas") ps.assign(std::begin(kTodas), std::end(kTodas));
        else if (auto p = mcp::permissaoPorNome(args[i])) ps.push_back(*p);
        else {
            std::cerr << "categoria desconhecida: " << args[i] << "\n";
            return 2;
        }
        for (auto p : ps) db.gravarMeta(std::string("mcp.") + mcp::nomePermissao(p), liberar ? "1" : "0");
    }
    return 0;
}

int estado(const Caminhos& c) {
    store::Database db(c.banco, store::Database::Abertura::SoExistente);
    if (!db.aberto() || !db.migrar()) {
        std::cerr << "nao achei o banco em " << c.banco << "\n";
        return 2;
    }
    std::cout << "banco:     " << c.banco << "\nmateriais: " << c.materiais << "\n\n";
    for (auto p : kTodas) {
        std::cout << "  " << mcp::nomePermissao(p) << ": "
                  << (mcp::permitido(db, p) ? "liberado" : "bloqueado") << "\n";
    }
    return 0;
}

} // namespace

int cmdMcp(int argc, char** argv) {
    std::vector<std::string> args(argv + 2, argv + argc);
    const Caminhos c = resolver(args);
    if (args.empty() || args[0] == "servir") return servir(c);
    if (args[0] == "permitir") return consentimento(c, args, true);
    if (args[0] == "bloquear") return consentimento(c, args, false);
    if (args[0] == "estado") return estado(c);
    std::cerr << "subcomando desconhecido: mcp " << args[0] << "\n";
    return 2;
}

} // namespace sigaa::app
