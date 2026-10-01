// sigaa-ui — entregável da Fase 3.
//
// Toda a lógica vive em core/: esta camada só desenha. Se algum dia trocarmos o
// Qt por outra coisa, o que se perde são os arquivos de src/ui/ — foi para isso
// que core/ ficou sem saber que UI existe.

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <string_view>

#include "core/config/Instituicao.h"
#include "mcp/Comando.h"
#include "ui/Icones.h"
#include "ui/InstanciaUnica.h"
#include "ui/JanelaPrincipal.h"
#include "ui/Tema.h"

namespace {

// Antes de qualquer coisa que fale com o SIGAA — e antes de ler o cofre, cuja
// chave sai do host da instituição. Ler isto tarde faria a primeira execução
// procurar a senha no lugar errado e abrir o diálogo de login sem motivo.
void restaurarInstituicao() {
    const QSettings cfg;
    const QString id = cfg.value(QStringLiteral("instituicao/id")).toString();
    const QString url = cfg.value(QStringLiteral("instituicao/url")).toString();

    if (auto i = sigaa::config::porId(id.toStdString())) {
        sigaa::config::selecionar(*i);
        return;
    }
    if (!url.isEmpty()) {
        sigaa::config::selecionar(sigaa::config::personalizada(url.toStdString()));
    }
    // Sem nada guardado: fica o padrão do catálogo (UNIFEI), que é o que este
    // app sempre fez.
}

#ifdef Q_OS_MACOS
// Onde o app grava o banco e procura o .env, no macOS.
//
// O PROBLEMA QUE ISTO RESOLVE: um .app aberto pelo Finder nasce com o
// diretório de trabalho em "/". O app gravaria o sigaa-viewer.db na raiz do
// disco, que é somente leitura desde o Catalina — ou seja, a versão do Mac
// falharia na primeira coleta, e só na do usuário, porque quem desenvolve abre
// pelo terminal e nunca vê isso.
//
// A REGRA: se o diretório atual já tem um banco, ele manda. É o caso de quem
// abre pelo terminal dentro de uma pasta de trabalho, e é o que mantém a
// promessa de o sigaa-cli e a interface compartilharem a coleta. Caso
// contrário vale ~/Library/Application Support/SIGAA Viewer, que é onde o
// macOS espera que um app guarde os dados dele.
void escolherPastaDeTrabalho() {
    if (QFileInfo::exists(QStringLiteral("sigaa-viewer.db"))) return;

    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return;
    QDir().mkpath(base);
    QDir::setCurrent(base);
}
#endif

} // namespace

int main(int argc, char** argv) {
    // `SIGAA-Viewer.AppImage mcp ...`: o agente de IA iniciando o servidor
    // MCP. O AppImage só expõe este executável, então é ele que atende — e
    // ANTES de criar a QApplication: nada de janela, de instância única nem
    // de tema, e o stdout é só do protocolo (docs/MCP.md).
    if (argc > 1 && std::string_view(argv[1]) == "mcp") return sigaa::mcp::comando(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SIGAA Viewer"));
    QApplication::setOrganizationName(QStringLiteral("sigaa-viewer"));
    QApplication::setWindowIcon(sigaa::ui::iconeApp());

#ifdef Q_OS_MACOS
    // Antes de tudo que toca disco, e depois de setApplicationName, de que o
    // caminho do Application Support depende.
    escolherPastaDeTrabalho();
#endif

    // Depois de setOrganizationName/setApplicationName: sem eles o QSettings
    // grava num lugar diferente do que a próxima execução leria.
    restaurarInstituicao();

    // Antes de construir qualquer janela: trocar de estilo depois faria o Qt
    // reconstruir os widgets já criados, e alguns não sobrevivem a isso com o
    // estado intacto.
    sigaa::ui::tema::aplicar(app);

    // Uma instância por usuário. Duas seriam duas sessões abertas na mesma
    // conta do SIGAA — e é com uma sessão viva que o login da outra fica sem
    // resposta (docs/RECON.md §6.0) — além de duas telas escrevendo no mesmo
    // banco e mostrando estados diferentes do mesmo semestre.
    //
    // ANTES da janela: abrir e fechar a janela seria um piscar na tela de quem
    // clicou no lançador duas vezes.
    sigaa::ui::InstanciaUnica instancia;
    if (!instancia.assumir()) {
        // Já havia uma, e ela foi avisada para aparecer. Sair em silêncio é a
        // resposta certa: o usuário pediu o app, e o app está na frente dele.
        return 0;
    }

    // O trabalho roda no diretório de onde o app foi aberto: é lá que estão o
    // .env e o sigaa-viewer.db que o sigaa-cli também usa. Compartilhar o banco
    // é o que faz a janela abrir já sabendo o que a última execução achou.
    sigaa::ui::JanelaPrincipal janela;
    // Maximizada: a aba Provas divide a largura entre calendário, lista e o
    // painel de resumo, e a Agenda empilha aulas e prazos — no tamanho padrão
    // do .ui as duas abriam cortadas, e o primeiro gesto de todo mundo era
    // maximizar. Quem preferir menor redimensiona; nada é guardado.
    janela.showMaximized();

    // A segunda instância pediu para esta aparecer. Tirar SÓ o minimizado, e
    // não `showNormal`: esse desfaria também o maximizado, e reabrir o app
    // pelo lançador encolheria a janela que já estava aberta. `activateWindow`
    // é o que tira o app de trás das outras janelas — `raise()` sozinho não
    // rouba o foco em todos os gerenciadores.
    QObject::connect(&instancia, &sigaa::ui::InstanciaUnica::pediramParaMostrar,
                     &janela, [&janela] {
                         janela.setWindowState(janela.windowState() & ~Qt::WindowMinimized);
                         janela.show();
                         janela.raise();
                         janela.activateWindow();
                     });

    return app.exec();
}
