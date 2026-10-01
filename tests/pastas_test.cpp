// A pasta de materiais que o servidor MCP usa quando ninguém passa
// `--materiais`: tem de ser a mesma <Documentos>/SIGAA que a UI usa.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/util/Pastas.h"

#if !defined(_WIN32) && !defined(__APPLE__)

namespace {

// Troca uma variável de ambiente e devolve a antiga no fim do escopo.
struct Ambiente {
    std::string nome;
    std::string antigo;
    bool tinha;
    Ambiente(const char* n, const std::string& valor) : nome(n) {
        const char* v = std::getenv(n);
        tinha = v != nullptr;
        if (tinha) antigo = v;
        if (valor.empty()) unsetenv(n);
        else setenv(n, valor.c_str(), 1);
    }
    ~Ambiente() {
        if (tinha) setenv(nome.c_str(), antigo.c_str(), 1);
        else unsetenv(nome.c_str());
    }
};

} // namespace

TEST_CASE("pastas: documentos vem do user-dirs.dirs, com $HOME expandido", "[pastas]") {
    const auto base = std::filesystem::temp_directory_path() / "sigaa-teste-pastas";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base / ".config");
    {
        std::ofstream f(base / ".config" / "user-dirs.dirs");
        f << "# comentario\nXDG_DESKTOP_DIR=\"$HOME/Area de trabalho\"\n"
             "XDG_DOCUMENTS_DIR=\"$HOME/Documentos\"\n";
    }
    Ambiente home("HOME", base.string());
    Ambiente cfg("XDG_CONFIG_HOME", "");
    Ambiente docs("XDG_DOCUMENTS_DIR", "");

    CHECK(sigaa::util::pastaDocumentos() == base.string() + "/Documentos");
    CHECK(sigaa::util::pastaMateriaisPadrao() == base.string() + "/Documentos/SIGAA");

    {
        Ambiente direto("XDG_DOCUMENTS_DIR", "/outra/pasta");
        CHECK(sigaa::util::pastaDocumentos() == "/outra/pasta");
    }

    std::filesystem::remove(base / ".config" / "user-dirs.dirs");
    CHECK(sigaa::util::pastaDocumentos() == base.string() + "/Documents");
    std::filesystem::remove_all(base);
}

#endif
