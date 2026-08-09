#include "app/Prompt.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace sigaa::app {
namespace {

bool terminalInterativo() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// RAII: desliga o eco enquanto viver, restaura no destrutor — inclusive se
// uma exceção subir no meio da leitura.
class EcoDesligado {
public:
    EcoDesligado() {
#ifdef _WIN32
        h_ = GetStdHandle(STD_INPUT_HANDLE);
        if (h_ != INVALID_HANDLE_VALUE && GetConsoleMode(h_, &modoAnterior_)) {
            ativo_ = SetConsoleMode(h_, modoAnterior_ & ~ENABLE_ECHO_INPUT) != 0;
        }
#else
        if (tcgetattr(STDIN_FILENO, &anterior_) == 0) {
            termios novo = anterior_;
            novo.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            ativo_ = tcsetattr(STDIN_FILENO, TCSAFLUSH, &novo) == 0;
        }
#endif
    }

    ~EcoDesligado() {
        if (!ativo_) return;
#ifdef _WIN32
        SetConsoleMode(h_, modoAnterior_);
#else
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &anterior_);
#endif
    }

    EcoDesligado(const EcoDesligado&) = delete;
    EcoDesligado& operator=(const EcoDesligado&) = delete;

    bool ativo() const { return ativo_; }

private:
    bool ativo_{false};
#ifdef _WIN32
    HANDLE h_{nullptr};
    DWORD modoAnterior_{0};
#else
    termios anterior_{};
#endif
};

} // namespace

std::string lerLinha(const std::string& rotulo) {
    std::cerr << rotulo;   // stderr: nao suja o stdout, que pode estar em pipe
    std::string s;
    std::getline(std::cin, s);
    return s;
}

std::string lerSenha(const std::string& rotulo) {
    if (!terminalInterativo()) {
        std::cerr << "aviso: stdin nao e um terminal; a senha sera lida sem "
                     "protecao de eco\n";
        return lerLinha(rotulo);
    }

    std::cerr << rotulo;
    std::string s;
    {
        EcoDesligado guarda;
        std::getline(std::cin, s);
        if (!guarda.ativo()) {
            std::cerr << "\naviso: nao consegui desligar o eco do terminal\n";
        }
    }
    std::cerr << "\n";   // o Enter do usuario nao ecoou
    return s;
}

void limpar(std::string& s) {
    if (s.empty()) return;
#ifdef _WIN32
    SecureZeroMemory(s.data(), s.size());
#else
    // volatile impede o compilador de remover a escrita "inutil"
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
#endif
    s.clear();
}

} // namespace sigaa::app
