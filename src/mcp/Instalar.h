#pragma once
// Registra o servidor MCP na configuração de cada agente (docs/MCP.md §6),
// para o aluno não editar JSON nem TOML na mão.
//
// Regras, para todos os clientes:
//   - só a entrada "sigaa" é tocada; o resto do arquivo sai como entrou;
//   - antes de gravar, uma cópia `<arquivo>.bak`;
//   - arquivo que não dá para ler com segurança (JSON com comentários, TOML
//     que não reconheço) NÃO é reescrito: a resposta traz o trecho para
//     colar à mão. Perder a configuração de outro servidor do aluno seria
//     pior que pedir um copiar-e-colar;
//   - os caminhos vão absolutos: o agente lança o servidor de qualquer pasta.

#include <optional>
#include <string>
#include <vector>

namespace sigaa::mcp {

enum class Cliente { ClaudeCode, ClaudeDesktop, Codex, Gemini, Antigravity, Cursor, VSCode };

struct InfoCliente {
    Cliente cliente;
    const char* id;     // o que se digita: "claude-code", "codex"...
    const char* nome;   // o que se mostra: "Claude Code"
};

const std::vector<InfoCliente>& clientes();
std::optional<Cliente> clientePorId(const std::string& id);
const InfoCliente& info(Cliente c);

// Como o agente deve iniciar o servidor.
struct Lancamento {
    std::string comando;
    std::vector<std::string> args;
};

// O executável certo para ESTA instalação do app:
//   - AppImage: o próprio .AppImage ($APPIMAGE), que atende `mcp`;
//   - caso contrário, o sigaa-cli ao lado do executável atual (no Windows o
//     sigaa-ui é programa gráfico, sem stdio, e não serviria);
//   - por fim, o executável atual.
// `banco` e `materiais` viram --banco/--materiais, absolutos.
Lancamento lancamento(const std::string& banco, const std::string& materiais);

// Onde fica a pasta pessoal e a de configuração do sistema. Vazio = do
// ambiente (HOME/USERPROFILE, APPDATA). Os testes passam uma pasta própria.
struct Pastas {
    std::string home;
    std::string appdata;   // Windows: %APPDATA%; vazio fora dele
};
Pastas pastasDoSistema();

// O arquivo de configuração do cliente.
std::string arquivoDeConfig(Cliente c, const Pastas& p);

// O agente parece instalado (a pasta ou o arquivo de configuração existe).
bool detectado(Cliente c, const Pastas& p);

struct Resposta {
    bool ok{false};
    std::string mensagem;   // para mostrar ao aluno
    std::string arquivo;    // o que foi (ou seria) alterado
    std::string trecho;     // a entrada "sigaa", no formato do cliente
};

// `soMostrar`: não grava, só devolve o trecho (o `--imprimir`).
Resposta instalar(Cliente c, const Lancamento& l, bool soMostrar, const Pastas& p);
Resposta remover(Cliente c, const Pastas& p);
// Já registrado?
bool instalado(Cliente c, const Pastas& p);

} // namespace sigaa::mcp
