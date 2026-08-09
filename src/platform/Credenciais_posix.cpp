// Cofre em Linux: libsecret via `secret-tool` (GNOME Keyring, KWallet e afins).
//
// Usamos fork/execvp e passamos o segredo por STDIN, nunca por argv — argv é
// legível por qualquer processo em /proc/<pid>/cmdline, e "a senha nunca vai
// por argumento" é regra do projeto, não preferência (ver app/Prompt.h).
//
// macOS: NÃO IMPLEMENTADO, de propósito e não por esquecimento. O caminho por
// linha de comando (`security add-generic-password -w <senha>`) colocaria a
// senha em argv, que é justamente o que estamos evitando; e o caminho certo
// (Security.framework / SecItemAdd) é código que eu não consigo compilar nem
// testar aqui. Preferi degradar de forma visível — `cofreDisponivel()` devolve
// false, o app avisa e cai para o .env — a fingir que funciona.

#include "platform/Credenciais.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace sigaa::plat {
namespace {

#ifdef __linux__
constexpr bool kTemBackend = true;
#else
constexpr bool kTemBackend = false;
#endif

// Atributos que identificam a entrada. Fixos: o login vai DENTRO do segredo,
// junto da senha, porque CPF é dado pessoal e atributo de chaveiro é legível.
const char* kServico = "sigaa-viewer";
const char* kConta = "sigaa.unifei.edu.br";

bool existeNoPath(const char* prog) {
    // Sem shell: monta o teste em cima do próprio execvp, num filho descartável.
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (const int nulo = ::open("/dev/null", O_RDWR); nulo >= 0) {
            dup2(nulo, STDIN_FILENO);
            dup2(nulo, STDOUT_FILENO);
            dup2(nulo, STDERR_FILENO);
            if (nulo > STDERR_FILENO) close(nulo);
        }
        execlp(prog, prog, "--help", static_cast<char*>(nullptr));
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return false;
    return WIFEXITED(st) && WEXITSTATUS(st) != 127;
}

// Roda `args`, escreve `entrada` no stdin do filho e coleta o stdout em `saida`.
// Devolve false se não executou ou saiu != 0.
bool rodar(const std::vector<std::string>& args, const std::string& entrada,
           std::string* saida, std::string* erro) {
    int paraFilho[2] = {-1, -1};
    int doFilho[2] = {-1, -1};
    if (pipe(paraFilho) != 0) { if (erro) *erro = "pipe() falhou"; return false; }
    if (pipe(doFilho) != 0) {
        close(paraFilho[0]); close(paraFilho[1]);
        if (erro) *erro = "pipe() falhou";
        return false;
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(paraFilho[0]); close(paraFilho[1]);
        close(doFilho[0]); close(doFilho[1]);
        if (erro) *erro = "fork() falhou";
        return false;
    }

    if (pid == 0) {
        dup2(paraFilho[0], STDIN_FILENO);
        dup2(doFilho[1], STDOUT_FILENO);
        close(paraFilho[0]); close(paraFilho[1]);
        close(doFilho[0]); close(doFilho[1]);
        if (const int nulo = ::open("/dev/null", O_WRONLY); nulo >= 0) {
            dup2(nulo, STDERR_FILENO);
            if (nulo > STDERR_FILENO) close(nulo);
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(paraFilho[0]);
    close(doFilho[1]);

    for (size_t escrito = 0; escrito < entrada.size();) {
        const ssize_t n = write(paraFilho[1], entrada.data() + escrito,
                                entrada.size() - escrito);
        if (n <= 0) break;
        escrito += static_cast<size_t>(n);
    }
    close(paraFilho[1]);   // EOF: sem isto o secret-tool espera para sempre

    if (saida) {
        char buf[512];
        for (ssize_t n; (n = read(doFilho[0], buf, sizeof buf)) > 0;) {
            saida->append(buf, static_cast<size_t>(n));
        }
    }
    close(doFilho[0]);

    int st = 0;
    if (waitpid(pid, &st, 0) < 0) { if (erro) *erro = "waitpid falhou"; return false; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        if (erro) *erro = std::string(args[0]) + " saiu com erro";
        return false;
    }
    return true;
}

} // namespace

std::string backendCofre() {
    if (!kTemBackend) return "nenhum (cofre nao implementado neste sistema)";
    return cofreDisponivel() ? "libsecret (secret-tool)"
                             : "nenhum (instale libsecret-tools)";
}

bool cofreDisponivel() {
    if (!kTemBackend) return false;
    static const bool tem = existeNoPath("secret-tool");
    return tem;
}

bool guardarNoCofre(const std::string& login, const std::string& senha,
                    std::string* erro) {
    if (!cofreDisponivel()) {
        if (erro) *erro = "nenhum cofre disponivel neste sistema";
        return false;
    }
    std::string segredo = login + "\n" + senha;
    const bool ok = rodar({"secret-tool", "store", "--label=SIGAA (sigaa-viewer)",
                           "service", kServico, "account", kConta},
                          segredo, nullptr, erro);
    limparSegredo(segredo);
    return ok;
}

std::optional<Credenciais> lerDoCofre(std::string* erro) {
    if (!cofreDisponivel()) return std::nullopt;

    std::string saida;
    if (!rodar({"secret-tool", "lookup", "service", kServico, "account", kConta}, {},
               &saida, erro)) {
        // secret-tool sai != 0 quando não acha; não é erro para nós.
        if (erro) erro->clear();
        return std::nullopt;
    }
    // Alguns backends devolvem \n final; o segredo real não tem.
    while (!saida.empty() && (saida.back() == '\n' || saida.back() == '\r')) {
        saida.pop_back();
    }

    Credenciais c;
    if (const auto q = saida.find('\n'); q != std::string::npos) {
        c.login = saida.substr(0, q);
        c.senha = saida.substr(q + 1);
    }
    limparSegredo(saida);

    if (c.login.empty() || c.senha.empty()) return std::nullopt;
    return c;
}

bool apagarDoCofre(std::string* erro) {
    if (!cofreDisponivel()) return true;   // não há o que apagar
    std::string ignorado;
    rodar({"secret-tool", "clear", "service", kServico, "account", kConta}, {},
          nullptr, &ignorado);
    // `clear` sai != 0 se não havia nada. A pós-condição desejada — não existe
    // credencial guardada — vale nos dois casos.
    (void)erro;
    return true;
}

} // namespace sigaa::plat
