// sigaa-ui — entregável da Fase 3.
//
// Toda a lógica vive em core/: esta camada só desenha. Se algum dia trocarmos o
// Qt por outra coisa, o que se perde são os arquivos de src/ui/ — foi para isso
// que core/ ficou sem saber que UI existe.

#include <QApplication>
#include <QFile>
#include <QSettings>

#include "core/config/Instituicao.h"
#include "ui/Icones.h"
#include "ui/JanelaPrincipal.h"

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

// A folha de estilo vem do .qrc, não do disco: um arquivo solto ao lado do .exe
// seria mais fácil de ajustar, mas viraria "o app abriu sem estilo" na primeira
// vez que alguém copiasse só o executável.
void aplicarEstilo(QApplication& app) {
    QFile f(QStringLiteral(":/estilo/estilo.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    app.setStyleSheet(QString::fromUtf8(f.readAll()));
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

    aplicarEstilo(app);

    // O trabalho roda no diretório de onde o app foi aberto: é lá que estão o
    // .env e o sigaa-viewer.db que o sigaa-cli também usa. Compartilhar o banco
    // é o que faz a janela abrir já sabendo o que a última execução achou.
    sigaa::ui::JanelaPrincipal janela;
    janela.show();
    return app.exec();
}
