// sigaa-cli — entregável da Fase 1.
//
// Hoje só faz o passo offline: ler um HTML salvo do SIGAA e mostrar o que o
// motor JSF entende dele. Isso permite validar o parser contra fixtures reais
// ANTES de escrever qualquer código de rede — que é a parte cara de depurar.
//
//   sigaa-cli forms   <arquivo.html>   lista os <form> e seus campos
//   sigaa-cli links   <arquivo.html>   lista os "cliques" jsfcljs
//   sigaa-cli post    <arquivo.html> <label>
//                                     monta o corpo do POST que o browser
//                                     enviaria ao clicar naquele link

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/config/DotEnv.h"
#include "app/Prompt.h"
#include "core/http/SigaaSession.h"
#include "core/jsf/JsfForm.h"
#include "core/notify/Aviso.h"
#include "core/parse/Html.h"
#include "core/parse/PortalParser.h"
#include "core/parse/TurmaParser.h"
#include "core/report/HtmlReport.h"
#include "core/calendar/Calendario.h"
#include "core/servico/Servico.h"
#include "core/sync/DiffEngine.h"
#include "core/sync/Materiais.h"
#include "platform/Credenciais.h"
#include "platform/Notify.h"

namespace {

struct OpcoesSync {
    std::string saida = "relatorio.html";
    bool comTurmas = false;
    bool quieto = false;
    // nullopt = decide pelo modo: notifica em --quiet, cala em interativo.
    std::optional<bool> notificar;
    std::string log;
};

std::string agoraIso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// Some com o console na execução agendada. Sem isto, uma janela pisca na cara
// do usuário a cada 20 minutos — o jeito mais rápido de ele desligar a tarefa.
void esconderConsole() {
#ifdef _WIN32
    HWND h = GetConsoleWindow();
    if (!h) return;
    // Só esconde o console se ele for NOSSO. Rodando `--quiet` dentro de um
    // terminal aberto, a janela é do shell do usuário — escondê-la faria o
    // terminal dele desaparecer do nada. O agendador dá um console exclusivo,
    // e aí a lista tem um processo só.
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) == 1) ShowWindow(h, SW_HIDE);
#endif
}

int cmdSync(const OpcoesSync& op) {
    using namespace sigaa;

    const bool querNotificar = op.notificar.value_or(op.quieto);

    // Em modo quieto ninguém está olhando o terminal: o log é a única forma de
    // descobrir por que a tarefa agendada parou de funcionar.
    std::ofstream logFile;
    std::streambuf* cerrOriginal = nullptr;
    if (op.quieto) {
        esconderConsole();
        logFile.open(op.log, std::ios::app);
        if (logFile) {
            logFile << "\n=== " << agoraIso() << " ===\n";
            cerrOriginal = std::cerr.rdbuf(logFile.rdbuf());
        }
    }
    struct RestauraCerr {
        std::streambuf* buf;
        ~RestauraCerr() { if (buf) std::cerr.rdbuf(buf); }
    } restaura{cerrOriginal};

    // Precedência: ambiente > cofre do SO > .env > prompt. Nunca argv
    // (ver app/Prompt.h e platform/Credenciais.h).
    auto res = plat::resolverCredenciais();

    servico::Opcoes so;
    so.incluirTurmas = op.comTurmas;
    so.caminhoRelatorio = op.saida;
    so.dumpHtml = config::credencial("SIGAA_DUMP", config::carregarDotEnv());
    so.login = std::move(res.cred.login);
    so.senha = std::move(res.cred.senha);

    // Um prompt em modo quieto travaria a tarefa agendada para sempre, num
    // console invisível. Melhor falhar alto e deixar rastro no log.
    if (!op.quieto) {
        if (so.login.empty()) so.login = app::lerLinha("CPF (so digitos): ");
        if (so.senha.empty()) so.senha = app::lerSenha("Senha: ");
    }

    if (so.login.empty() || so.senha.empty()) {
        std::cerr << "erro: credenciais vazias"
                  << (op.quieto ? " — em --quiet elas precisam vir do ambiente, do"
                                  " cofre (sigaa-cli login) ou do .env\n"
                                : "\n");
        return 1;
    }

    if (res.origem == plat::Origem::DotEnv && !op.quieto) {
        std::cerr << "dica: suas credenciais estao em texto puro no .env. "
                     "Rode `sigaa-cli login` para move-las ao "
                  << plat::backendCofre() << ".\n";
    }

    const auto r = servico::executar(
        std::move(so), [](servico::Nivel n, std::string_view m) {
            std::cerr << (n == servico::Nivel::Aviso ? "aviso: " : "  ") << m << "\n";
        });

    const auto avisarUsuario = [&] {
        if (!querNotificar || !r.aviso) return;
        plat::Notificacao n;
        n.titulo = r.aviso->titulo;
        n.corpo = r.aviso->corpo;
        n.urgente = r.aviso->urgente;
        // Vem vazio em coleta suspeita: não houve relatório nesta rodada, e
        // mandar o usuário ao antigo o faria confiar em dado vencido.
        n.aoClicar = r.relatorio;
        std::string err;
        if (!plat::notificar(n, &err)) {
            std::cerr << "aviso: nao consegui notificar (" << err << ")\n";
        }
    };

    if (r.falha == servico::Falha::ColetaSuspeita) {
        std::cerr << "\n!! COLETA SUSPEITA — nada foi gravado\n";
        for (const auto& e : r.diff.eventos) std::cerr << "   " << e.detalhe << "\n";
        avisarUsuario();
        return 1;
    }
    if (!r.ok()) {
        std::cerr << "erro: " << r.erro << "\n";
        return 1;
    }

    if (op.comTurmas) {
        std::cerr << "turmas visitadas: " << r.turmasVisitadas;
        if (r.turmasComFalha) std::cerr << " (" << r.turmasComFalha << " falharam)";
        std::cerr << "\n";
    }

    if (r.bancoDisponivel) {
        if (r.diff.primeiraExecucao) {
            std::cerr << "primeira coleta: linha de base registrada, sem alertas\n";
        } else if (r.diff.eventos.empty()) {
            std::cerr << "nada novo desde a ultima verificacao\n";
        } else {
            std::cerr << "\n" << r.diff.eventos.size() << " novidade(s):\n";
            for (const auto& e : r.diff.eventos) {
                std::cerr << "  [" << sync::toString(e.tipo) << "] " << e.titulo;
                if (!e.turmaNome.empty()) std::cerr << "  (" << e.turmaNome << ")";
                std::cerr << "\n";
                if (!e.detalhe.empty()) std::cerr << "      " << e.detalhe << "\n";
            }
        }
    }

    if (!r.ics.empty()) std::cerr << "calendario: " << r.ics << "\n";

    const auto& snap = r.snapshot;
    std::cerr << "ok: " << snap.turmas.size() << " turmas, "
              << snap.atividades.size() << " atividades, "
              << snap.atualizacoes.size() << " atualizacoes";
    if (!snap.avaliacoes.empty()) {
        std::cerr << ", " << calendario::mesclarAvaliacoes(snap.avaliacoes).size()
                  << " avaliacoes";
    }
    if (snap.minutosSessaoRestantes) {
        std::cerr << " (sessao expira em " << *snap.minutosSessaoRestantes << " min)";
    }
    std::cerr << "\n";

    // Por último: só interrompe o usuário depois que o relatório existe no
    // disco, para o clique na notificação sempre ter para onde levar.
    avisarUsuario();

    std::cout << r.relatorio << "\n";
    return 0;
}

// Guarda as credenciais no cofre do SO, mas só depois de PROVAR que funcionam.
//
// Salvar sem validar seria ativamente perigoso: a tarefa agendada passaria a
// tentar a senha errada a cada 20 minutos, e o SIGAA bloqueia a conta depois de
// algumas tentativas. O usuário descobriria o bloqueio sem nenhuma pista de que
// veio daqui.
int cmdLogin() {
    using namespace sigaa;

    if (!plat::cofreDisponivel()) {
        std::cerr << "erro: nenhum cofre disponivel (" << plat::backendCofre() << ")\n";
        return 1;
    }

    std::cout << "Guardando no cofre: " << plat::backendCofre() << "\n"
              << "Isso protege contra a senha vazar em arquivo (backup, nuvem,\n"
              << "git). NAO protege contra programa malicioso rodando na sua\n"
              << "propria conta — esse pede a decifragem como nos pedimos.\n\n";

    std::string login = app::lerLinha("CPF (so digitos): ");
    std::string senha = app::lerSenha("Senha: ");
    if (login.empty() || senha.empty()) {
        std::cerr << "erro: nada digitado\n";
        app::limpar(senha);
        return 1;
    }

    std::cerr << "verificando no SIGAA antes de guardar...\n";
    http::SigaaSession sess;
    std::string erro;
    const bool ok = sess.login(login, senha, &erro);

    if (!ok) {
        app::limpar(senha);
        std::cerr << "falha no login: " << erro << "\n"
                  << "nada foi guardado. Confira a senha no navegador antes de\n"
                  << "tentar de novo — o SIGAA bloqueia a conta apos algumas\n"
                  << "tentativas erradas.\n";
        return 1;
    }

    const bool gravou = plat::guardarNoCofre(login, senha, &erro);
    app::limpar(senha);
    if (!gravou) {
        std::cerr << "login ok, mas nao consegui guardar: " << erro << "\n";
        return 1;
    }

    std::cout << "ok: credenciais verificadas e guardadas.\n";
    if (std::filesystem::exists(".env")) {
        std::cout << "\nAtencao: o .env continua no disco com a senha em texto\n"
                     "puro. Apague as linhas SIGAA_LOGIN/SIGAA_SENHA dele — o\n"
                     "cofre so ajuda se a copia em claro sumir.\n";
    }
    return 0;
}

int cmdLogout() {
    std::string erro;
    if (!sigaa::plat::apagarDoCofre(&erro)) {
        std::cerr << "erro: " << erro << "\n";
        return 1;
    }
    std::cout << "credenciais removidas do cofre.\n";
    return 0;
}

int usage() {
    std::cerr <<
        "uso:\n"
        "  sigaa-cli forms  <arquivo.html>\n"
        "  sigaa-cli links  <arquivo.html>\n"
        "  sigaa-cli post   <arquivo.html> <trecho-do-label>\n"
        "  sigaa-cli parse  <arquivo.html>              resumo em texto\n"
        "  sigaa-cli report <arquivo.html> [saida.html] relatorio HTML\n"
        "  sigaa-cli sync [saida.html] [opcoes]         busca no SIGAA, gera relatorio + .ics\n"
        "  sigaa-cli login                              guarda as credenciais no cofre do SO\n"
        "  sigaa-cli logout                             remove as credenciais do cofre\n"
        "  sigaa-cli doctor [url]                       diagnostico de rede/TLS\n"
        "  sigaa-cli doctor --notificar                 dispara uma notificacao de teste\n"
        "  sigaa-cli arquivos <turma>                   lista o material publicado na turma\n"
        "  sigaa-cli baixar <turma> <id> [<dir>]        baixa um arquivo (dir: materiais)\n"
        "  sigaa-cli explorar <turma> [<aba>] [<dir>]   recon: entra na turma e grava o\n"
        "                                               HTML cru da aba (padrao dir=recon)\n"
        "\n"
        "opcoes do sync:\n"
        "  --turmas          entra em cada turma (mais lento, traz provas e topicos)\n"
        "  --quiet           modo agendado: sem prompt, console oculto, log em arquivo,\n"
        "                    notificacao ligada. Exige credenciais no ambiente/.env.\n"
        "  --log <arquivo>   para onde vai o log em --quiet (padrao sigaa-viewer.log)\n"
        "  --notificar       forca notificacao nativa mesmo em modo interativo\n"
        "  --sem-notificar   desliga a notificacao mesmo em --quiet\n"
        "\n"
        "credenciais: ambiente (SIGAA_LOGIN/SIGAA_SENHA) > cofre do SO > .env > prompt.\n"
        "nunca passe a senha por argumento — ela fica no historico do shell.\n";
    return 2;
}

// --- recon ao vivo ---------------------------------------------------------
//
// `explorar` existe para fechar a pendência §5.4 do RECON: das abas da Turma
// Virtual (Arquivos, Tarefas, Notícias, Fóruns, Ver Notas...) só conhecemos os
// NOMES no menu — nunca vimos o HTML que cada uma devolve, e não dá para
// escrever parser contra página que ninguém leu.
//
// Ele navega de verdade e GRAVA o HTML cru de cada passo. Cru importa: o Ctrl+S
// do browser salva o DOM pós-JavaScript, 2,7× maior e com markup que o servidor
// nunca envia (RECON §1.7) — fixture assim mente para o parser.
//
// Custo: 3 requisições (portal, turma, aba). Roda sob demanda, nunca em ciclo.
int cmdExplorar(const std::string& buscaTurma, const std::string& rotuloMenu,
                const std::string& dir) {
    sigaa::plat::Credenciais cred;
    auto res = sigaa::plat::resolverCredenciais();
    if (res.ok()) {
        cred = std::move(res.cred);
    } else {
        cred.login = sigaa::app::lerLinha("CPF (so digitos): ");
        cred.senha = sigaa::app::lerSenha("Senha: ");
    }
    if (cred.login.empty() || cred.senha.empty()) {
        std::cerr << "erro: sem credenciais\n";
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto gravar = [&dir](const std::string& nome, const std::string& corpo) {
        const auto p = std::filesystem::path(dir) / nome;
        std::ofstream f(p, std::ios::binary);
        // Sem transcodificar: o arquivo tem que ser byte a byte o que veio da
        // rede, senão deixa de servir como fixture.
        f.write(corpo.data(), static_cast<std::streamsize>(corpo.size()));
        std::cout << "  gravado " << p.string() << "  (" << corpo.size() << " bytes)\n";
    };

    sigaa::http::SigaaSession sess;
    std::string erro;
    std::cout << "login...\n";
    if (!sess.login(cred.login, cred.senha, &erro)) {
        std::cerr << "erro: login falhou: " << erro << "\n";
        sigaa::plat::limparSegredo(cred.senha);
        return 1;
    }
    sigaa::plat::limparSegredo(cred.senha);

    auto r = sess.irParaPortal();
    if (!r.ok()) {
        std::cerr << "erro: portal: " << r.error << "\n";
        return 1;
    }
    sigaa::html::Document portal;
    if (!portal.parse(r.body)) {
        std::cerr << "erro: parse do portal falhou\n";
        return 1;
    }
    gravar("portal.html", r.body);

    const auto snap = sigaa::parse::parsePortal(portal);
    const sigaa::Turma* alvo = nullptr;
    for (const auto& t : snap.turmas) {
        if (t.frontEndId.empty()) continue;
        if (buscaTurma.empty() || t.nome.find(buscaTurma) != std::string::npos ||
            t.codigo.find(buscaTurma) != std::string::npos) {
            alvo = &t;
            break;
        }
    }
    if (!alvo) {
        std::cerr << "erro: nenhuma turma casa com \"" << buscaTurma << "\". Tem:\n";
        for (const auto& t : snap.turmas) std::cerr << "  " << t.codigo << "  " << t.nome << "\n";
        return 1;
    }
    std::cout << "turma: " << alvo->codigo << " " << alvo->nome << "\n";

    // Entrar na turma: achar o comando pelo frontEndIdTurma, que é estável entre
    // sessões (RECON §1.2) — ao contrário do texto do link.
    const sigaa::jsf::Command* cmdTurma = nullptr;
    const auto cmds = sigaa::jsf::findCommands(portal);
    for (const auto& c : cmds) {
        for (const auto& [k, v] : c.params) {
            if (k == "frontEndIdTurma" && v == alvo->frontEndId) cmdTurma = &c;
        }
        if (cmdTurma) break;
    }
    if (!cmdTurma) {
        std::cerr << "erro: nao achei o comando de entrar na turma\n";
        return 1;
    }
    auto formPortal = sigaa::jsf::parseForm(portal, cmdTurma->formId);
    if (!formPortal) {
        std::cerr << "erro: form " << cmdTurma->formId << " nao encontrado\n";
        return 1;
    }

    auto rt = sess.postForm(formPortal->action, formPortal->buildPostBody(cmdTurma->params));
    if (!rt.ok()) {
        std::cerr << "erro: entrar na turma: " << rt.error << "\n";
        return 1;
    }
    sigaa::html::Document turma;
    if (!turma.parse(rt.body)) {
        std::cerr << "erro: parse da turma falhou\n";
        return 1;
    }
    gravar("turma.html", rt.body);

    if (rotuloMenu.empty()) {
        std::cout << "\nmenu da turma:\n";
        for (const auto& c : sigaa::jsf::findCommands(turma)) {
            if (c.formId == "formMenu") std::cout << "  " << c.label << "\n";
        }
        std::cout << "\npasse o rotulo como 3o argumento para abrir a aba.\n";
        return 0;
    }

    // Pelo RÓTULO, nunca pelo id do componente: os ids do menu são gerados pelo
    // JSF (`formMenu:j_id_jsp_719010821_123`) e mudam quando a instituição
    // recompila a página. O rótulo é o que o aluno lê e o que sobrevive.
    auto cmdMenu = sigaa::jsf::findCommandByLabel(turma, rotuloMenu);
    if (!cmdMenu) {
        std::cerr << "erro: nenhum item de menu contendo \"" << rotuloMenu << "\"\n";
        return 1;
    }
    auto formMenu = sigaa::jsf::parseForm(turma, cmdMenu->formId);
    if (!formMenu) {
        std::cerr << "erro: form " << cmdMenu->formId << " nao encontrado\n";
        return 1;
    }
    std::cout << "abrindo \"" << cmdMenu->label << "\" (form " << cmdMenu->formId
              << ", action " << formMenu->action << ")\n";

    auto ra = sess.postForm(formMenu->action, formMenu->buildPostBody(cmdMenu->params));
    if (!ra.ok()) {
        std::cerr << "erro: abrir a aba: " << ra.error << "\n";
        return 1;
    }
    std::string nomeArq = "aba.html";
    gravar(nomeArq, ra.body);

    sigaa::html::Document aba;
    if (!aba.parse(ra.body)) {
        std::cerr << "aviso: parse da aba falhou; o HTML cru foi gravado\n";
        return 0;
    }
    std::cout << "\nestrutura da aba:\n"
              << "  status HTTP  " << ra.status << "\n"
              << "  url final    " << ra.finalUrl << "\n"
              << "  <table>      " << aba.select("table").size() << "\n"
              << "  <form>       " << aba.select("form").size() << "\n"
              << "  linhas tr    " << aba.select("tr").size() << "\n";
    std::cout << "\ncomandos jsfcljs na aba (candidatos a download):\n";
    for (const auto& c : sigaa::jsf::findCommands(aba)) {
        if (c.formId == "formMenu") continue;   // é o menu lateral de novo
        std::cout << "  \"" << c.label << "\"  form=" << c.formId << "\n";
        for (const auto& [k, v] : c.params) std::cout << "      " << k << " = " << v << "\n";
    }
    return 0;
}

// Lista (e opcionalmente baixa) o material publicado numa turma. É o mesmo
// caminho que a UI usa — provar aqui primeiro é o que evita depurar rede
// através de uma janela.
int cmdArquivos(const std::string& buscaTurma, const std::string& idBaixar,
                const std::string& dir) {
    sigaa::plat::Credenciais cred;
    auto res = sigaa::plat::resolverCredenciais();
    if (res.ok()) {
        cred = std::move(res.cred);
    } else {
        cred.login = sigaa::app::lerLinha("CPF (so digitos): ");
        cred.senha = sigaa::app::lerSenha("Senha: ");
    }
    if (cred.login.empty() || cred.senha.empty()) {
        std::cerr << "erro: sem credenciais\n";
        return 1;
    }

    sigaa::http::SigaaSession sess;
    std::string erro;
    if (!sess.login(cred.login, cred.senha, &erro)) {
        std::cerr << "erro: login falhou: " << erro << "\n";
        sigaa::plat::limparSegredo(cred.senha);
        return 1;
    }
    sigaa::plat::limparSegredo(cred.senha);

    auto rp = sess.irParaPortal();
    sigaa::html::Document portal;
    if (!rp.ok() || !portal.parse(rp.body)) {
        std::cerr << "erro: portal indisponivel\n";
        return 1;
    }
    const auto snap = sigaa::parse::parsePortal(portal);

    const sigaa::Turma* alvo = nullptr;
    for (const auto& t : snap.turmas) {
        if (!t.frontEndId.empty() && t.nome.find(buscaTurma) != std::string::npos) {
            alvo = &t;
            break;
        }
    }
    if (!alvo) {
        std::cerr << "erro: nenhuma turma casa com \"" << buscaTurma << "\". Tem:\n";
        for (const auto& t : snap.turmas) std::cerr << "  " << t.nome << "\n";
        return 1;
    }

    sigaa::sync::SessaoTurma turma(sess);
    if (!turma.entrar(*alvo, &erro)) {
        std::cerr << "erro: " << erro << "\n";
        return 1;
    }
    if (!turma.abrirArquivos(&erro)) {
        std::cerr << "erro: " << erro << "\n";
        return 1;
    }

    std::cout << alvo->nome << "\n";
    if (turma.arquivos().empty()) {
        std::cout << (turma.semArquivos()
                          ? "  o professor nao publicou nenhum arquivo.\n"
                          : "  nenhum arquivo lido (o HTML pode ter mudado).\n");
        return 0;
    }
    for (const auto& a : turma.arquivos()) {
        std::cout << "  [" << a.idArquivo << "]  " << a.titulo << "\n";
        if (!a.topico.empty()) std::cout << "        topico: " << a.topico << "\n";
    }

    if (idBaixar.empty()) {
        std::cout << "\nbaixe com:  sigaa-cli baixar \"" << buscaTurma << "\" <id> [dir]\n";
        return 0;
    }

    std::cout << "\nbaixando " << idBaixar << "...\n";
    const auto caminho = turma.baixar(idBaixar, dir, &erro);
    if (!caminho) {
        std::cerr << "erro: " << erro << "\n";
        return 1;
    }
    std::cout << "salvo em " << *caminho << "\n";
    return 0;
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();

    if (std::string(argv[1]) == "login") return cmdLogin();
    if (std::string(argv[1]) == "logout") return cmdLogout();

    if (std::string(argv[1]) == "arquivos") {
        if (argc < 3) return usage();
        return cmdArquivos(argv[2], "", "");
    }

    if (std::string(argv[1]) == "baixar") {
        if (argc < 4) return usage();
        return cmdArquivos(argv[2], argv[3], argc >= 5 ? argv[4] : "materiais");
    }

    if (std::string(argv[1]) == "explorar") {
        const std::string turma = argc >= 3 ? argv[2] : "";
        const std::string menu = argc >= 4 ? argv[3] : "";
        const std::string dir = argc >= 5 ? argv[4] : "recon";
        return cmdExplorar(turma, menu, dir);
    }

    if (std::string(argv[1]) == "doctor") {
        if (argc >= 3 && std::string(argv[2]) == "--notificar") {
            sigaa::plat::Notificacao n;
            n.titulo = "SIGAA: teste de notificacao";
            n.corpo = "Se voce esta lendo isto, a tarefa agendada consegue "
                      "avisar sobre prazos e provas.";
            std::string err;
            const bool ok = sigaa::plat::notificar(n, &err);
            std::cout << "backend: " << sigaa::plat::backendNotificacao() << "\n"
                      << "envio  : " << (ok ? "ok" : "falhou: " + err) << "\n";
            return ok ? 0 : 1;
        }

        const auto res = sigaa::plat::resolverCredenciais();
        const char* onde = "nenhuma (rode `sigaa-cli login`)";
        switch (res.origem) {
            case sigaa::plat::Origem::Ambiente: onde = "variaveis de ambiente"; break;
            case sigaa::plat::Origem::Cofre:    onde = "cofre do sistema"; break;
            case sigaa::plat::Origem::DotEnv:   onde = ".env (TEXTO PURO — mova para o cofre)"; break;
            case sigaa::plat::Origem::Nenhuma:  break;
        }
        std::cout << sigaa::http::infoBackend() << "\n"
                  << "notificacao: " << sigaa::plat::backendNotificacao() << "\n"
                  << "cofre      : " << sigaa::plat::backendCofre() << "\n"
                  << "credenciais: " << onde << "\n";
        if (argc >= 3) {
            // Teste de conectividade: isola "nossa build esta quebrada" de
            // "este servidor especifico rejeita a gente".
            sigaa::http::SigaaSession s("");
            const auto r = s.get(argv[2]);
            std::cout << "\nGET " << argv[2] << "\n"
                      << "  http   : " << r.status << "\n"
                      << "  bytes  : " << r.body.size() << "\n"
                      << "  erro   : " << (r.error.empty() ? "(nenhum)" : r.error) << "\n";
            return r.ok() ? 0 : 1;
        }
        return 0;
    }

    // sync fala com a rede e não recebe arquivo de entrada.
    if (std::string(argv[1]) == "sync") {
        OpcoesSync op;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--turmas")             op.comTurmas = true;
            else if (a == "--quiet")         op.quieto = true;
            else if (a == "--notificar")     op.notificar = true;
            else if (a == "--sem-notificar") op.notificar = false;
            else if (a == "--log") {
                if (i + 1 >= argc) {
                    std::cerr << "erro: --log exige um caminho\n";
                    return 2;
                }
                op.log = argv[++i];
            } else if (a.rfind("--", 0) == 0) {
                // Flag desconhecida virava nome de arquivo de saida em
                // silencio — e o relatorio ia parar num arquivo chamado
                // "--turmass".
                std::cerr << "erro: opcao desconhecida " << a << "\n";
                return usage();
            } else {
                op.saida = a;
            }
        }
        if (op.log.empty()) op.log = "sigaa-viewer.log";
        return cmdSync(op);
    }

    if (argc < 3) return usage();

    const std::string cmd = argv[1];
    const std::string path = argv[2];

    auto content = readFile(path);
    if (!content) {
        std::cerr << "erro: nao consegui ler " << path << "\n";
        return 1;
    }

    sigaa::html::Document doc;
    if (!doc.parse(*content)) {
        std::cerr << "erro: parse do HTML falhou\n";
        return 1;
    }

    if (cmd == "forms") {
        for (const auto& node : doc.select("form")) {
            auto f = sigaa::jsf::parseForm(node);
            if (!f) continue;
            std::cout << "form " << f->id << "\n"
                      << "  action    " << f->action << "\n"
                      << "  viewState " << (f->viewState.empty() ? "(nenhum)" : f->viewState)
                      << "\n  campos    " << f->fields.size() << "\n";
            for (const auto& [k, v] : f->fields) {
                std::cout << "    " << k << " = " << v << "\n";
            }
        }
        return 0;
    }

    if (cmd == "links") {
        for (const auto& c : sigaa::jsf::findCommands(doc)) {
            std::cout << "link \"" << c.label << "\"\n"
                      << "  form " << c.formId << "\n";
            for (const auto& [k, v] : c.params) {
                std::cout << "    " << k << " = " << v << "\n";
            }
        }
        return 0;
    }

    if (cmd == "turma") {
        const auto c = sigaa::parse::parseTurmaVirtual(doc, "?", "?", "2026.2");
        std::cout << "semTopicos: " << (c.semTopicos ? "sim" : "nao") << "\n"
                  << "topicos   : " << c.topicos.size() << "\n"
                  << "avaliacoes: " << c.avaliacoes.size() << "\n";
        for (const auto& a : c.avaliacoes) {
            std::cout << "  [" << (a.fonte == sigaa::FonteAvaliacao::PainelAvaliacoes
                                       ? "painel"
                                       : "topico")
                      << "] " << a.quando.toIso() << "  " << a.descricao;
            if (!a.horarioBruto.empty()) std::cout << "  (" << a.horarioBruto << ")";
            std::cout << "\n";
        }
        std::cout << "\npaineis rich-stglpanel encontrados: "
                  << doc.select("div.rich-stglpanel").size() << "\n";
        for (const auto& p : doc.select("div.rich-stglpanel .rich-stglpanel-header")) {
            std::cout << "  - " << p.text() << "\n";
        }
        return 0;
    }

    if (cmd == "parse") {
        const auto snap = sigaa::parse::parsePortal(doc);
        std::cout << "TURMAS (" << snap.turmas.size() << ")\n";
        for (const auto& t : snap.turmas) {
            std::cout << "  " << t.idTurma << "  " << t.nome
                      << "  [" << t.horario << "] " << t.local << "\n";
        }
        std::cout << "\nATIVIDADES (" << snap.atividades.size() << ")\n";
        for (const auto& a : snap.atividades) {
            std::cout << "  " << a.prazo.toIso() << "  " << a.turmaNome
                      << " - " << a.titulo << "\n";
        }
        std::cout << "\nATUALIZACOES (" << snap.atualizacoes.size() << ")\n";
        for (const auto& u : snap.atualizacoes) {
            std::cout << "  " << u.data.toIso() << "  [" << u.hash << "] "
                      << u.texto << "\n";
        }
        return 0;
    }

    if (cmd == "report") {
        const auto snap = sigaa::parse::parsePortal(doc);
        const std::string out = (argc >= 4) ? argv[3] : "relatorio.html";

        std::ofstream f(out, std::ios::binary);
        if (!f) {
            std::cerr << "erro: nao consegui escrever " << out << "\n";
            return 1;
        }
        f << sigaa::report::renderHtml(snap);
        std::cout << "relatorio escrito em " << out << "  ("
                  << snap.atividades.size() << " atividades, "
                  << snap.turmas.size() << " turmas)\n";
        return 0;
    }

    if (cmd == "post") {
        if (argc < 4) return usage();
        const std::string label = argv[3];

        auto c = sigaa::jsf::findCommandByLabel(doc, label);
        if (!c) {
            std::cerr << "erro: nenhum link com label contendo \"" << label << "\"\n";
            return 1;
        }
        auto f = sigaa::jsf::parseForm(doc, c->formId);
        if (!f) {
            std::cerr << "erro: form " << c->formId << " nao encontrado\n";
            return 1;
        }
        std::cout << "POST " << f->action << "\n"
                  << "Content-Type: application/x-www-form-urlencoded\n\n"
                  << f->buildPostBody(c->params) << "\n";
        return 0;
    }

    return usage();
}
