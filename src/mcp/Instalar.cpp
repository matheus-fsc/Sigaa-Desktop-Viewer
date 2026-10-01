#include "mcp/Instalar.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/config/Instituicao.h"
#include "core/util/Caminho.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdint>
#include <mach-o/dyld.h>
#endif

namespace sigaa::mcp {

// `ordered_json`: o arquivo do aluno sai com as chaves na ordem em que
// entraram. O `json` comum ordena alfabeticamente, e regravar a configuração
// do agente embaralhada — mesmo com o mesmo conteúdo — é mexer no que não é
// nosso.
using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {

std::string ambiente(const char* n) {
    const char* v = std::getenv(n);
    return v ? std::string(v) : std::string();
}

std::string executavelAtual() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return util::paraUtf8(fs::path(buf));
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t tam = sizeof buf;
    if (_NSGetExecutablePath(buf, &tam) != 0) return {};
    std::error_code ec;
    const auto p = fs::canonical(buf, ec);
    return ec ? std::string(buf) : p.string();
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.string();
#endif
}

std::string absoluto(const std::string& s) {
    if (s.empty()) return s;
    std::error_code ec;
    const auto p = fs::absolute(util::deUtf8(s), ec);
    return ec ? s : util::paraUtf8(p.lexically_normal());
}

// Qual chave raiz e qual formato de entrada cada cliente JSON usa.
struct FormatoJson {
    const char* raiz;    // "mcpServers" ou "servers"
    bool comTipo;        // "type": "stdio"
};

FormatoJson formato(Cliente c) {
    switch (c) {
        case Cliente::VSCode:     return {"servers", true};
        case Cliente::ClaudeCode: return {"mcpServers", true};
        default:                  return {"mcpServers", false};
    }
}

json entradaJson(Cliente c, const Lancamento& l) {
    json e = {{"command", l.comando}, {"args", l.args}};
    if (formato(c).comTipo) e["type"] = "stdio";
    return e;
}

// TOML: string básica, com \ e " escapados (caminho do Windows tem \).
std::string tomlString(const std::string& s) {
    std::string o = "\"";
    for (char ch : s) {
        if (ch == '\\' || ch == '"') o += '\\';
        o += ch;
    }
    return o + "\"";
}

std::string trechoToml(const Lancamento& l) {
    std::string args;
    for (size_t i = 0; i < l.args.size(); ++i) args += (i ? ", " : "") + tomlString(l.args[i]);
    return "[mcp_servers.sigaa]\ncommand = " + tomlString(l.comando) + "\nargs = [" + args + "]\n";
}

std::optional<std::string> lerArquivo(const std::string& caminho) {
    std::ifstream f(util::deUtf8(caminho), std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Grava por arquivo temporário + rename: uma queda no meio deixa o arquivo
// velho inteiro, nunca metade de um JSON.
bool gravarArquivo(const std::string& caminho, const std::string& conteudo, std::string* erro) {
    const fs::path destino = util::deUtf8(caminho);
    std::error_code ec;
    fs::create_directories(destino.parent_path(), ec);
    if (fs::exists(destino)) {
        fs::copy_file(destino, fs::path(destino) += ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            *erro = "não consegui fazer a cópia de segurança: " + ec.message();
            return false;
        }
    }
    const fs::path tmp = fs::path(destino) += ".sigaa-tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            *erro = "não consegui escrever em " + util::paraUtf8(tmp.parent_path());
            return false;
        }
        f << conteudo;
    }
    fs::rename(tmp, destino, ec);
    if (ec) {
        *erro = "não consegui gravar " + caminho + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// Lê a configuração JSON do cliente. nullopt = existe mas não dá para
// reescrever sem perder algo (comentários, sintaxe inválida).
std::optional<json> lerJson(const std::string& caminho, std::string* motivo) {
    const auto txt = lerArquivo(caminho);
    if (!txt || txt->find_first_not_of(" \t\r\n") == std::string::npos) return json::object();
    json j = json::parse(*txt, nullptr, false, /*ignore_comments=*/false);
    if (j.is_discarded()) {
        const json comComentarios = json::parse(*txt, nullptr, false, true);
        *motivo = comComentarios.is_discarded()
                      ? "o arquivo não é um JSON válido"
                      : "o arquivo tem comentários, que se perderiam ao regravar";
        return std::nullopt;
    }
    if (!j.is_object()) {
        *motivo = "o arquivo não é um objeto JSON";
        return std::nullopt;
    }
    return j;
}

bool cabecalhoSigaa(const std::string& linha) {
    const auto i = linha.find_first_not_of(" \t");
    if (i == std::string::npos) return false;
    const std::string l = linha.substr(i);
    return l.rfind("[mcp_servers.sigaa]", 0) == 0 || l.rfind("[mcp_servers.sigaa.", 0) == 0;
}

bool cabecalho(const std::string& linha) {
    const auto i = linha.find_first_not_of(" \t");
    return i != std::string::npos && linha[i] == '[';
}

// O TOML sem as tabelas [mcp_servers.sigaa] e [mcp_servers.sigaa.*].
std::string semSigaa(const std::string& toml, bool* tinha) {
    std::istringstream in(toml);
    std::string linha, out;
    bool dentro = false;
    *tinha = false;
    while (std::getline(in, linha)) {
        if (cabecalho(linha)) dentro = cabecalhoSigaa(linha);
        if (dentro) {
            *tinha = true;
            continue;
        }
        out += linha + "\n";
    }
    // Sem a linha em branco que sobrava no fim depois de tirar a seção.
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

} // namespace

const std::vector<InfoCliente>& clientes() {
    static const std::vector<InfoCliente> v = {
        {Cliente::ClaudeCode, "claude-code", "Claude Code"},
        {Cliente::ClaudeDesktop, "claude-desktop", "Claude Desktop"},
        {Cliente::Codex, "codex", "Codex CLI"},
        {Cliente::Gemini, "gemini", "Gemini CLI"},
        {Cliente::Antigravity, "antigravity", "Antigravity"},
        {Cliente::Cursor, "cursor", "Cursor"},
        {Cliente::VSCode, "vscode", "VS Code"},
    };
    return v;
}

std::optional<Cliente> clientePorId(const std::string& id) {
    for (const auto& c : clientes()) {
        if (id == c.id) return c.cliente;
    }
    return std::nullopt;
}

const InfoCliente& info(Cliente c) {
    for (const auto& i : clientes()) {
        if (i.cliente == c) return i;
    }
    return clientes().front();
}

Lancamento lancamento(const std::string& banco, const std::string& materiais) {
    Lancamento l;
    const std::string appimage = ambiente("APPIMAGE");
    if (!appimage.empty()) {
        // /proc/self/exe apontaria para /tmp/.mount_*, que some quando o app
        // fecha. O .AppImage é o que fica no disco.
        l.comando = appimage;
    } else {
        const fs::path atual = util::deUtf8(executavelAtual());
#ifdef _WIN32
        const fs::path cli = atual.parent_path() / "sigaa-cli.exe";
#else
        const fs::path cli = atual.parent_path() / "sigaa-cli";
#endif
        std::error_code ec;
        l.comando = util::paraUtf8(fs::exists(cli, ec) ? cli : atual);
    }
    // A instituição vai explícita: a chave da senha no cofre sai do host, e o
    // servidor iniciado pelo agente não sabe qual SIGAA o app está usando.
    l.args = {"mcp", "--url", config::selecionada().baseUrl, "--banco", absoluto(banco)};
    if (!materiais.empty()) {
        l.args.push_back("--materiais");
        l.args.push_back(absoluto(materiais));
    }
    return l;
}

Pastas pastasDoSistema() {
    Pastas p;
#ifdef _WIN32
    p.home = ambiente("USERPROFILE");
    p.appdata = ambiente("APPDATA");
#else
    p.home = ambiente("HOME");
#endif
    return p;
}

std::string arquivoDeConfig(Cliente c, const Pastas& p) {
    const fs::path home = util::deUtf8(p.home);
#ifdef _WIN32
    const fs::path appdata = p.appdata.empty() ? home / "AppData" / "Roaming" : util::deUtf8(p.appdata);
#endif
    fs::path r;
    switch (c) {
        case Cliente::ClaudeCode: r = home / ".claude.json"; break;
        case Cliente::ClaudeDesktop:
#ifdef _WIN32
            r = appdata / "Claude" / "claude_desktop_config.json";
#elif defined(__APPLE__)
            r = home / "Library" / "Application Support" / "Claude" / "claude_desktop_config.json";
#else
            r = home / ".config" / "Claude" / "claude_desktop_config.json";
#endif
            break;
        case Cliente::Codex: r = home / ".codex" / "config.toml"; break;
        case Cliente::Gemini: r = home / ".gemini" / "settings.json"; break;
        case Cliente::Antigravity: r = home / ".gemini" / "antigravity" / "mcp_config.json"; break;
        case Cliente::Cursor: r = home / ".cursor" / "mcp.json"; break;
        case Cliente::VSCode:
#ifdef _WIN32
            r = appdata / "Code" / "User" / "mcp.json";
#elif defined(__APPLE__)
            r = home / "Library" / "Application Support" / "Code" / "User" / "mcp.json";
#else
            r = home / ".config" / "Code" / "User" / "mcp.json";
#endif
            break;
    }
    return util::paraUtf8(r);
}

bool detectado(Cliente c, const Pastas& p) {
    std::error_code ec;
    const fs::path arq = util::deUtf8(arquivoDeConfig(c, p));
    if (fs::exists(arq, ec)) return true;
    // Claude Code guarda o resto em ~/.claude; os outros, na pasta do arquivo.
    if (c == Cliente::ClaudeCode) return fs::exists(util::deUtf8(p.home) / ".claude", ec);
    return fs::is_directory(arq.parent_path(), ec);
}

bool instalado(Cliente c, const Pastas& p) {
    const std::string arq = arquivoDeConfig(c, p);
    const auto txt = lerArquivo(arq);
    if (!txt) return false;
    if (c == Cliente::Codex) {
        bool tinha = false;
        semSigaa(*txt, &tinha);
        return tinha;
    }
    const json j = json::parse(*txt, nullptr, false, true);
    const char* raiz = formato(c).raiz;
    return j.is_object() && j.contains(raiz) && j[raiz].is_object() && j[raiz].contains("sigaa");
}

Resposta instalar(Cliente c, const Lancamento& l, bool soMostrar, const Pastas& p) {
    Resposta r;
    r.arquivo = arquivoDeConfig(c, p);
    const std::string nome = info(c).nome;

    if (c == Cliente::Codex) {
        r.trecho = trechoToml(l);
        if (soMostrar) {
            r.ok = true;
            r.mensagem = "Acrescente ao " + r.arquivo + ":";
            return r;
        }
        bool tinha = false;
        std::string toml = semSigaa(lerArquivo(r.arquivo).value_or(""), &tinha);
        if (!toml.empty() && toml.back() != '\n') toml += '\n';
        if (!toml.empty()) toml += '\n';
        toml += r.trecho;
        std::string erro;
        if (!gravarArquivo(r.arquivo, toml, &erro)) {
            r.mensagem = erro + ". Acrescente à mão:";
            return r;
        }
        r.ok = true;
        r.mensagem = std::string(tinha ? "Registro atualizado" : "Conectado") + " no " + nome +
                     ". Reinicie o agente para ele ver o servidor.";
        return r;
    }

    const FormatoJson f = formato(c);
    const json entrada = entradaJson(c, l);
    r.trecho = json{{f.raiz, {{"sigaa", entrada}}}}.dump(2);
    if (soMostrar) {
        r.ok = true;
        r.mensagem = "Junte ao " + r.arquivo + ":";
        return r;
    }
    std::string motivo;
    auto cfg = lerJson(r.arquivo, &motivo);
    if (!cfg) {
        r.mensagem = "Não alterei " + r.arquivo + ": " + motivo + ". Junte à mão:";
        return r;
    }
    if (!cfg->contains(f.raiz) || !(*cfg)[f.raiz].is_object()) (*cfg)[f.raiz] = json::object();
    const bool tinha = (*cfg)[f.raiz].contains("sigaa");
    (*cfg)[f.raiz]["sigaa"] = entrada;
    std::string erro;
    if (!gravarArquivo(r.arquivo, cfg->dump(2) + "\n", &erro)) {
        r.mensagem = erro + ". Junte à mão:";
        return r;
    }
    r.ok = true;
    r.mensagem = std::string(tinha ? "Registro atualizado" : "Conectado") + " no " + nome +
                 ". Reinicie o agente para ele ver o servidor.";
    if (c == Cliente::ClaudeCode) {
        // O Claude Code regrava o ~/.claude.json enquanto roda e pode
        // desfazer a mudança; o comando dele é o caminho garantido.
        r.mensagem += " Se ele estava aberto e o servidor não aparecer, rode no terminal: "
                      "claude mcp add-json -s user sigaa '" + entrada.dump() + "'";
    }
    return r;
}

Resposta remover(Cliente c, const Pastas& p) {
    Resposta r;
    r.arquivo = arquivoDeConfig(c, p);
    const std::string nome = info(c).nome;
    const auto txt = lerArquivo(r.arquivo);
    if (!txt) {
        r.ok = true;
        r.mensagem = "O " + nome + " não tinha o servidor registrado.";
        return r;
    }
    std::string erro;
    if (c == Cliente::Codex) {
        bool tinha = false;
        const std::string toml = semSigaa(*txt, &tinha);
        if (!tinha) {
            r.ok = true;
            r.mensagem = "O " + nome + " não tinha o servidor registrado.";
            return r;
        }
        r.ok = gravarArquivo(r.arquivo, toml, &erro);
    } else {
        std::string motivo;
        auto cfg = lerJson(r.arquivo, &motivo);
        if (!cfg) {
            r.mensagem = "Não alterei " + r.arquivo + ": " + motivo +
                         ". Apague a entrada \"sigaa\" à mão.";
            return r;
        }
        const char* raiz = formato(c).raiz;
        if (!cfg->contains(raiz) || !(*cfg)[raiz].is_object() || !(*cfg)[raiz].contains("sigaa")) {
            r.ok = true;
            r.mensagem = "O " + nome + " não tinha o servidor registrado.";
            return r;
        }
        (*cfg)[raiz].erase("sigaa");
        r.ok = gravarArquivo(r.arquivo, cfg->dump(2) + "\n", &erro);
    }
    r.mensagem = r.ok ? "Desconectado do " + nome + "." : erro;
    return r;
}

} // namespace sigaa::mcp
