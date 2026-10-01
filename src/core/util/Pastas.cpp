#include "core/util/Pastas.h"

#include <cstdlib>
#include <fstream>

#include "core/util/Caminho.h"

namespace sigaa::util {
namespace {

std::string ambiente(const char* nome) {
    const char* v = std::getenv(nome);
    return v ? std::string(v) : std::string();
}

#if !defined(_WIN32) && !defined(__APPLE__)
// XDG_DOCUMENTS_DIR="$HOME/Documentos" -> /home/x/Documentos
std::string documentosDoUserDirs(const std::string& home) {
    std::string cfg = ambiente("XDG_CONFIG_HOME");
    if (cfg.empty()) cfg = home + "/.config";
    std::ifstream in(cfg + "/user-dirs.dirs");
    std::string linha;
    while (std::getline(in, linha)) {
        const std::string chave = "XDG_DOCUMENTS_DIR=";
        if (linha.rfind(chave, 0) != 0) continue;
        std::string v = linha.substr(chave.size());
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
        return v;
    }
    return {};
}
#endif

} // namespace

std::string pastaDocumentos() {
#ifdef _WIN32
    const std::string perfil = ambiente("USERPROFILE");
    return perfil.empty() ? std::string() : perfil + "\\Documents";
#else
    const std::string home = ambiente("HOME");
    if (home.empty()) return {};
#if !defined(__APPLE__)
    if (std::string x = ambiente("XDG_DOCUMENTS_DIR"); !x.empty()) return x;
    if (std::string x = documentosDoUserDirs(home); !x.empty()) return x;
#endif
    return home + "/Documents";
#endif
}

std::string pastaMateriaisPadrao() {
    const std::string docs = pastaDocumentos();
    if (docs.empty()) return {};
    return paraUtf8(deUtf8(docs) / "SIGAA");
}

} // namespace sigaa::util
