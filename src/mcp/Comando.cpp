#include "mcp/Comando.h"

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
#include "mcp/Instalar.h"
#include "mcp/Kit.h"
#include "mcp/Leitura.h"
#include "mcp/Servidor.h"

namespace sigaa::mcp {
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

const Permissao kTodas[] = {Permissao::Leitura, Permissao::Arquivos,
                                 Permissao::Escrita, Permissao::Rede};

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
    Servidor s({c.banco, c.materiais});
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
        std::vector<Permissao> ps;
        if (args[i] == "todas") ps.assign(std::begin(kTodas), std::end(kTodas));
        else if (auto p = permissaoPorNome(args[i])) ps.push_back(*p);
        else {
            std::cerr << "categoria desconhecida: " << args[i] << "\n";
            return 2;
        }
        for (auto p : ps) db.gravarMeta(std::string("mcp.") + nomePermissao(p), liberar ? "1" : "0");
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
        std::cout << "  " << nomePermissao(p) << ": "
                  << (permitido(db, p) ? "liberado" : "bloqueado") << "\n";
    }
    return 0;
}

int listarClientes() {
    const Pastas p = pastasDoSistema();
    for (const auto& c : clientes()) {
        std::cout << "  " << c.id << std::string(16 - std::string(c.id).size(), ' ')
                  << (instalado(c.cliente, p) ? "conectado   "
                      : detectado(c.cliente, p) ? "detectado   "
                                                : "nao achado  ")
                  << arquivoDeConfig(c.cliente, p) << "\n";
    }
    return 0;
}

std::vector<Cliente> alvos(const std::string& id) {
    std::vector<Cliente> v;
    if (id == "todos") {
        const Pastas p = pastasDoSistema();
        for (const auto& c : clientes()) {
            if (detectado(c.cliente, p)) v.push_back(c.cliente);
        }
    } else if (auto c = clientePorId(id)) {
        v.push_back(*c);
    }
    return v;
}

int registrar(const Caminhos& c, const std::vector<std::string>& args, bool instalarAgora) {
    bool soMostrar = false;
    std::string id;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--imprimir") soMostrar = true;
        else id = args[i];
    }
    const auto lista = alvos(id);
    if (lista.empty()) {
        std::cerr << "uso: sigaa-cli mcp " << (instalarAgora ? "instalar" : "remover")
                  << " <cliente>|todos" << (instalarAgora ? " [--imprimir]" : "")
                  << "\nclientes:";
        for (const auto& i : clientes()) std::cerr << " " << i.id;
        std::cerr << "\n";
        return 2;
    }
    if (instalarAgora && !std::filesystem::exists(util::deUtf8(c.banco))) {
        std::cerr << "nao achei o banco em " << c.banco
                  << ". Rode na pasta do sigaa-viewer.db ou passe --banco.\n";
        return 2;
    }
    const Pastas p = pastasDoSistema();
    const Lancamento l = lancamento(c.banco, c.materiais);
    int falhas = 0;
    for (Cliente cli : lista) {
        const Resposta r = instalarAgora ? instalar(cli, l, soMostrar, p) : remover(cli, p);
        std::cout << r.mensagem << "\n";
        if (!r.ok || soMostrar) std::cout << "\n" << r.trecho << "\n";
        if (!r.ok) ++falhas;
    }
    if (instalarAgora && !soMostrar) {
        store::Database db(c.banco, store::Database::Abertura::SoExistente);
        if (db.aberto() && db.migrar() && !permitido(db, Permissao::Leitura)) {
            std::cout << "\nO agente ainda nao pode ler nada: libere com "
                         "`sigaa-cli mcp permitir leitura` (e `arquivos`, para os PDFs).\n";
        }
    }
    return falhas ? 1 : 0;
}

int kit(const Caminhos& c, const std::vector<std::string>& args) {
    std::string destino;
    std::vector<std::string> pos;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--destino" && i + 1 < args.size()) destino = args[++i];
        else pos.push_back(args[i]);
    }
    if (pos.empty()) {
        std::cerr << "uso: sigaa-cli mcp kit <turma> [<prova>] [--destino <pasta>]\n";
        return 2;
    }
    store::Database db(c.banco, store::Database::Abertura::SoExistente);
    if (!db.aberto() || !db.migrar()) {
        std::cerr << "nao achei o banco em " << c.banco << "\n";
        return 2;
    }
    const Kit k = exportarKit(db, c.materiais, pos[0], pos.size() > 1 ? pos[1] : "", destino);
    if (!k.ok) {
        std::cerr << k.erro << "\n";
        return 1;
    }
    std::cout << "kit em " << k.pasta << "\n  " << k.copiados << " arquivo(s) copiado(s)";
    if (k.faltando) std::cout << ", " << k.faltando << " ainda nao baixado(s)";
    std::cout << "\n";
    return 0;
}

} // namespace

int comando(int argc, char** argv) {
    std::vector<std::string> args(argv + 2, argv + argc);
    const Caminhos c = resolver(args);
    if (args.empty() || args[0] == "servir") return servir(c);
    if (args[0] == "permitir") return consentimento(c, args, true);
    if (args[0] == "bloquear") return consentimento(c, args, false);
    if (args[0] == "estado") return estado(c);
    if (args[0] == "clientes") return listarClientes();
    if (args[0] == "instalar") return registrar(c, args, true);
    if (args[0] == "remover") return registrar(c, args, false);
    if (args[0] == "kit") return kit(c, args);
    std::cerr << "subcomando desconhecido: mcp " << args[0] << "\n";
    return 2;
}

} // namespace sigaa::mcp
