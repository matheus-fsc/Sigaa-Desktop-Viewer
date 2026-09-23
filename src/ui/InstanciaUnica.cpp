#include "ui/InstanciaUnica.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace sigaa::ui {
namespace {

// Nome do soquete local, por usuário.
//
// Do NOME DE LOGIN e não do uid: o mesmo nome funciona no Windows, onde não há
// uid. Passa por hash porque o login pode ter espaço, acento ou barra — e no
// Linux este nome vira arquivo em /tmp.
QString nomeDoSoquete() {
    const QString usuario = qEnvironmentVariable("USER", qEnvironmentVariable(
        "USERNAME", QStringLiteral("anon")));
    const QByteArray h =
        QCryptographicHash::hash(usuario.toUtf8(), QCryptographicHash::Sha1)
            .toHex()
            .left(12);
    return QStringLiteral("sigaa-viewer-") + QString::fromLatin1(h);
}

} // namespace

InstanciaUnica::InstanciaUnica(QObject* pai)
    : QObject(pai), nome_(nomeDoSoquete()) {}

bool InstanciaUnica::assumir() {
    // Tenta falar com uma instância que já esteja de pé.
    {
        QLocalSocket teste;
        teste.connectToServer(nome_);
        if (teste.waitForConnected(300)) {
            // Há outra. Um byte basta como pedido — não passamos argumentos
            // porque o app não aceita nenhum; quando aceitar, é aqui que eles
            // viajam.
            teste.write("mostrar");
            teste.waitForBytesWritten(300);
            teste.disconnectFromServer();
            return false;
        }
    }

    // Não respondeu. Pode ser que não haja ninguém, ou que uma instância
    // anterior tenha morrido sem limpar o soquete — acontece num kill -9 ou
    // num crash, e sem esta remoção o app NUNCA MAIS abriria nesta máquina,
    // porque o `listen` falharia para sempre num arquivo órfão.
    QLocalServer::removeServer(nome_);

    servidor_ = new QLocalServer(this);
    if (!servidor_->listen(nome_)) {
        // Não conseguimos nem ouvir. Melhor abrir a janela do que recusar o
        // app inteiro por causa de uma limitação do sistema de arquivos: o
        // pior caso é o que já existia antes deste arquivo.
        delete servidor_;
        servidor_ = nullptr;
        return true;
    }

    connect(servidor_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* c = servidor_->nextPendingConnection()) {
            // O conteúdo não é lido: a conexão EM SI é o pedido. Ler exigiria
            // esperar os bytes chegarem, e a segunda instância já pode ter
            // saído — o que deixaria a janela sem subir por causa de uma
            // corrida que não precisa existir.
            c->deleteLater();
        }
        Q_EMIT pediramParaMostrar();
    });
    return true;
}

} // namespace sigaa::ui
