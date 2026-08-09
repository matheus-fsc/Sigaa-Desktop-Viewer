#include "core/config/DotEnv.h"

#include <cstdlib>
#include <fstream>

namespace sigaa::config {
namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string tirarAspas(const std::string& s) {
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string doAmbiente(const std::string& nome) {
#ifdef _MSC_VER
    char* buf = nullptr;
    size_t n = 0;
    if (_dupenv_s(&buf, &n, nome.c_str()) == 0 && buf) {
        std::string v(buf);
        free(buf);
        return v;
    }
    return {};
#else
    const char* v = std::getenv(nome.c_str());
    return v ? std::string(v) : std::string{};
#endif
}

} // namespace

std::map<std::string, std::string> carregarDotEnv(const std::string& caminho) {
    std::map<std::string, std::string> out;
    std::ifstream f(caminho);
    if (!f) return out;   // ausência de .env é o caso normal, não erro

    std::string linha;
    while (std::getline(f, linha)) {
        const std::string s = trim(linha);
        if (s.empty() || s[0] == '#') continue;

        const auto eq = s.find('=');
        if (eq == std::string::npos) continue;

        const std::string k = trim(s.substr(0, eq));
        if (k.empty()) continue;
        out[k] = tirarAspas(trim(s.substr(eq + 1)));
    }
    return out;
}

std::string credencial(const std::string& chave,
                       const std::map<std::string, std::string>& dotenv) {
    if (auto v = doAmbiente(chave); !v.empty()) return v;
    if (auto it = dotenv.find(chave); it != dotenv.end()) return it->second;
    return {};
}

} // namespace sigaa::config
