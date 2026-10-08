#pragma once
// O servidor MCP do SIGAA Viewer: JSON-RPC 2.0 sobre stdio, uma mensagem por
// linha (docs/MCP.md, D1).
//
// REGRA DE OURO: nada além de protocolo no stdout. Quem roda o servidor
// (`sigaa-cli mcp`) manda qualquer log para o stderr; um `std::cout` perdido
// corrompe a sessão do agente sem mensagem de erro que preste.
//
// O servidor é uma função de mensagem em mensagem (`tratar`), separada do laço
// de E/S (`rodar`), para os testes conversarem com ele sem processo nem pipe.

#include <iosfwd>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sigaa::mcp {

struct Config {
    std::string banco;       // caminho absoluto do sigaa-viewer.db
    std::string materiais;   // <Documentos>/SIGAA; vazio = sem caminhos de arquivo
};

// Versões do protocolo que este servidor fala, da mais nova para a mais velha.
// A primeira é a anunciada quando o cliente pede uma que não está aqui.
inline constexpr const char* kVersoesProtocolo[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

class Servidor {
public:
    explicit Servidor(Config c);

    // Uma mensagem JSON-RPC. Devolve a resposta, ou nada para notificação.
    std::optional<nlohmann::json> tratar(const nlohmann::json& msg);

    // O laço de stdio: lê linhas de `in` até o fim, responde em `out`.
    void rodar(std::istream& in, std::ostream& out);

    // O nome que o agente deu no initialize (clientInfo.name): a "origem"
    // de tudo que ele lê e grava.
    const std::string& cliente() const { return cliente_; }

private:
    nlohmann::json inicializar(const nlohmann::json& p);
    nlohmann::json listarFerramentas();
    nlohmann::json chamarFerramenta(const nlohmann::json& p);
    nlohmann::json listarRecursos();
    nlohmann::json listarModelosDeRecurso();
    nlohmann::json lerRecurso(const nlohmann::json& p, nlohmann::json* erro);
    nlohmann::json listarPrompts();
    nlohmann::json pegarPrompt(const nlohmann::json& p, nlohmann::json* erro);

    void auditar(const std::string& oque, const std::string& turma, bool ok,
                 const std::string& motivo = {});

    Config config_;
    std::string cliente_{"desconhecido"};
};

// Base64 padrão (RFC 4648), para o `blob` de um recurso.
std::string base64(const std::string& bytes);

} // namespace sigaa::mcp
