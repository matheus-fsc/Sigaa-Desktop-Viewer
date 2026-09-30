#include "ui/EntrarNoSigaa.h"

#include <QDesktopServices>
#include <QHostAddress>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include "core/http/EntradaNoNavegador.h"

namespace sigaa::ui {
namespace {

// Uma entrada por vez, no app inteiro.
QPointer<EntrarNoSigaa> emCurso;

// Aleatório de verdade (do sistema), e não o gerador padrão semeado: o
// segredo é o que separa o navegador de qualquer outro programa que ache a
// porta.
QByteArray aleatorioHex(int bytes) {
    QByteArray b(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i) {
        b[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return b.toHex();
}

// Apaga antes de soltar. Não é garantia absoluta — o Qt pode ter copiado o
// buffer no caminho —, mas é o que está ao nosso alcance, e custa nada.
void apagar(QByteArray& b) {
    std::fill(b.begin(), b.end(), '\0');
    b.clear();
}

} // namespace

bool EntrarNoSigaa::emAndamento() { return !emCurso.isNull(); }

EntrarNoSigaa::EntrarNoSigaa(QObject* pai) : QObject(pai) {}

EntrarNoSigaa::~EntrarNoSigaa() { apagar(resposta_); }

EntrarNoSigaa* EntrarNoSigaa::abrir(const std::string& baseUrl, const std::string& login,
                                   const std::string& senha, QObject* pai,
                                   QString* erro) {
    if (emAndamento()) {
        if (erro) *erro = QStringLiteral("O navegador já está abrindo o SIGAA.");
        return nullptr;
    }

    auto* e = new EntrarNoSigaa(pai);
    e->servidor_ = new QTcpServer(e);

    // LocalHost e não Any: a porta não pode existir para a rede. Em Any, quem
    // estivesse no mesmo Wi-Fi da biblioteca veria uma porta aberta com a
    // senha do aluno atrás.
    if (!e->servidor_->listen(QHostAddress::LocalHost, 0)) {
        if (erro) *erro = QStringLiteral("Não consegui abrir uma porta local: %1")
                              .arg(e->servidor_->errorString());
        delete e;
        return nullptr;
    }

    const QByteArray segredo = aleatorioHex(16);
    const QByteArray nonce = aleatorioHex(16);
    e->caminho_ = "/" + segredo;

    const std::string corpo = http::paginaDeEntrada(baseUrl, login, senha,
                                                    nonce.toStdString());
    const std::string cab = http::cabecalhosDaEntrada(nonce.toStdString(), corpo.size());
    e->resposta_ = QByteArray::fromStdString(cab + corpo);

    connect(e->servidor_, &QTcpServer::newConnection, e, [e] {
        while (QTcpSocket* s = e->servidor_->nextPendingConnection()) e->atender(s);
    });

    e->prazo_ = new QTimer(e);
    e->prazo_->setSingleShot(true);
    connect(e->prazo_, &QTimer::timeout, e, &EntrarNoSigaa::encerrar);
    e->prazo_->start(60'000);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1%2")
                       .arg(e->servidor_->serverPort())
                       .arg(QString::fromLatin1(e->caminho_)));
    if (!QDesktopServices::openUrl(url)) {
        if (erro) *erro = QStringLiteral("Não consegui abrir o navegador padrão.");
        delete e;
        return nullptr;
    }

    emCurso = e;
    return e;
}

void EntrarNoSigaa::atender(QTcpSocket* s) {
    s->setParent(this);

    // Lê só até o fim da primeira linha. O resto da requisição não interessa,
    // e um limite de tamanho impede que uma conexão local qualquer faça o app
    // acumular memória sem fim.
    connect(s, &QTcpSocket::readyRead, this, [this, s] {
        // UMA resposta por conexão. Os cabeçalhos da requisição chegam depois
        // da primeira linha e disparam este sinal de novo; sem a guarda, cada
        // um seria lido como uma requisição nova e ganharia um 404 escrito no
        // meio da página.
        if (s->property("atendido").toBool()) {
            s->readAll();
            return;
        }
        if (!s->canReadLine()) {
            if (s->bytesAvailable() > 8192) s->abort();
            return;
        }
        const QByteArray linha = s->readLine(8192).trimmed();   // "GET /x HTTP/1.1"
        s->setProperty("atendido", true);
        const QList<QByteArray> partes = linha.split(' ');
        const bool certo = partes.size() >= 2 && partes[0] == "GET" &&
                           partes[1] == caminho_ && !entregue_;

        if (!certo) {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                     "Connection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }

        entregue_ = true;
        s->write(resposta_);
        // Apagada já: a resposta está no buffer do socket, e não haverá uma
        // segunda entrega para a qual guardar a senha.
        apagar(resposta_);
        // Fecha a porta para novas conexões, mas espera ESTA terminar de
        // escrever — fechar o servidor antes cortaria a página no meio.
        servidor_->close();
        connect(s, &QTcpSocket::disconnected, this, &EntrarNoSigaa::encerrar);
        s->disconnectFromHost();
    });
}

void EntrarNoSigaa::encerrar() {
    // Pode chegar duas vezes: pela conexão que terminou de entregar e pelo
    // prazo de 60 s, que continua armado até o objeto morrer.
    if (terminado_) return;
    terminado_ = true;
    if (prazo_) prazo_->stop();
    Q_EMIT terminou(entregue_);
    if (servidor_) servidor_->close();
    apagar(resposta_);
    deleteLater();
}

} // namespace sigaa::ui
