#pragma once
// Serve UMA VEZ, em 127.0.0.1, a página que faz o navegador entrar no SIGAA
// (core/http/EntradaNoNavegador.h), e some.
//
// POR QUE UM SERVIDOR, e não um arquivo .html temporário: o arquivo teria a
// senha em texto puro no disco, e "apagar depois" não é garantia — o
// navegador pode demorar a abrir, o app pode fechar antes, e disco com
// journaling ou SSD não esquece o que foi gravado. Pela porta local a senha
// só existe na memória dos dois programas e no loopback.
//
// Por que não na URL: a URL fica no histórico do navegador, que sincroniza
// com a nuvem.
//
// AS TRÊS TRAVAS:
//   - endereço com um segredo aleatório de 128 bits: outro programa ou outra
//     aba que descubra a porta não adivinha o caminho;
//   - a página é entregue UMA vez; a segunda requisição recebe 404, e o
//     servidor fecha em seguida;
//   - 60 segundos sem ninguém buscar e o servidor fecha sozinho, com a senha
//     apagada da memória.
//
// O QUE NÃO PROTEGE: um programa malicioso rodando na sua conta pode ler a
// linha de comando que abre o navegador e pegar o endereço antes dele. É o
// mesmo limite do cofre (README, "Até onde o cofre protege"): esse programa
// já pediria a senha ao cofre do mesmo jeito que o app pede.

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace sigaa::ui {

class EntrarNoSigaa : public QObject {
    Q_OBJECT
public:
    // Abre o navegador padrão já entrando no SIGAA. Devolve false (com o
    // motivo em `erro`) se não deu para abrir a porta local ou o navegador.
    // O objeto se destrói sozinho quando termina.
    static bool abrir(const std::string& baseUrl, const std::string& login,
                      const std::string& senha, QObject* pai, QString* erro);

    // Há uma entrada em andamento? Clicar de novo enquanto o navegador ainda
    // abre faria dois logins seguidos na conta, sem nenhum ganho.
    static bool emAndamento();

    ~EntrarNoSigaa() override;

private:
    explicit EntrarNoSigaa(QObject* pai);
    void atender(QTcpSocket* s);
    void encerrar();

    QTcpServer* servidor_{nullptr};
    QTimer* prazo_{nullptr};
    QByteArray caminho_;    // "/<segredo>"
    QByteArray resposta_;   // cabeçalhos + página, com a senha dentro
    bool entregue_{false};
};

} // namespace sigaa::ui
