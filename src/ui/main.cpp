// sigaa-ui — entregável da Fase 3.
//
// Toda a lógica vive em core/: esta camada só desenha. Se algum dia trocarmos o
// Qt por outra coisa, o que se perde são os arquivos de src/ui/ — foi para isso
// que core/ ficou sem saber que UI existe.

#include <QApplication>
#include <QSettings>

#include "core/config/Instituicao.h"
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

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SIGAA Viewer"));
    QApplication::setOrganizationName(QStringLiteral("sigaa-viewer"));
    QApplication::setWindowIcon(sigaa::ui::iconeApp());

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
    janela.show();

    // A segunda instância pediu para esta aparecer. `showNormal` desfaz a
    // minimização, e `activateWindow` é o que tira o app de trás das outras
    // janelas — `raise()` sozinho não rouba o foco em todos os gerenciadores.
    QObject::connect(&instancia, &sigaa::ui::InstanciaUnica::pediramParaMostrar,
                     &janela, [&janela] {
                         janela.showNormal();
                         janela.raise();
                         janela.activateWindow();
                     });

    return app.exec();
}
