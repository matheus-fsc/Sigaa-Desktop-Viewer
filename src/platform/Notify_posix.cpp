// Notificação em Linux (notify-send) e macOS (osascript).
//
// Usamos fork/execvp e NUNCA system(): o texto vem do SIGAA, e passá-lo por um
// shell transformaria um título de tópico com aspas ou `$(...)` em execução de
// comando. Com execvp o argumento chega ao programa como um argumento, ponto.

#include "platform/Notify.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

namespace sigaa::plat {
namespace {

// Roda argv e espera. Devolve false se não deu para executar ou saiu != 0.
bool rodar(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Silencia o filho: notify-send reclamando no stderr poluiria o log
        // do sync sem informar nada acionável.
        if (const int nulo = ::open("/dev/null", O_WRONLY); nulo >= 0) {
            dup2(nulo, STDOUT_FILENO);
            dup2(nulo, STDERR_FILENO);
            if (nulo > STDERR_FILENO) close(nulo);
        }
        execvp(argv[0], argv.data());
        _exit(127);   // execvp só volta em erro
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#ifdef __APPLE__
// Escapa para dentro de uma string literal do AppleScript.
std::string escaparAppleScript(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        if (c == '\\' || c == '"') o.push_back('\\');
        if (c == '\n') { o += "\\n"; continue; }
        o.push_back(c);
    }
    return o;
}
#endif

} // namespace

std::string backendNotificacao() {
#ifdef __APPLE__
    return "osascript (display notification)";
#else
    return "notify-send (libnotify)";
#endif
}

bool notificar(const Notificacao& n, std::string* erro) {
#ifdef __APPLE__
    // O clique não é configurável por esta via; o relatório fica no disco.
    const std::string script = "display notification \"" +
                               escaparAppleScript(n.corpo) + "\" with title \"" +
                               escaparAppleScript(n.titulo) + "\"";
    if (rodar({"osascript", "-e", script})) return true;
    if (erro) *erro = "osascript falhou ou nao esta disponivel";
    return false;
#else
    std::vector<std::string> args{"notify-send"};
    args.push_back("--app-name=sigaa-viewer");
    args.push_back(n.urgente ? "--urgency=critical" : "--urgency=normal");
    args.push_back(n.urgente ? "--icon=dialog-warning" : "--icon=dialog-information");
    // "--" impede que um título começando com '-' vire flag.
    args.push_back("--");
    args.push_back(n.titulo);
    args.push_back(n.corpo);

    if (rodar(args)) return true;
    if (erro) *erro = "notify-send nao disponivel (instale libnotify-bin)";
    return false;
#endif
}

} // namespace sigaa::plat
