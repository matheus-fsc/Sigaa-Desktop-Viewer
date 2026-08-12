#pragma once
// Janela de diagnóstico: o tráfego com o SIGAA, ao vivo.
//
// A pergunta que ela responde não é "que erro deu?", é "**estou batendo demais
// no servidor da universidade?**". Um laço de navegação que refaz a mesma
// requisição não trava o app nem gera exceção — ele só faz o SIGAA ver um
// cliente comportando-se como robô, e quem paga é a conta do aluno. Sem uma
// tela assim, esse defeito é invisível até o bloqueio.
//
// Três coisas ficam explícitas aqui, e é por isso que a janela existe em vez de
// um simples arquivo de log:
//
//   - o RITMO (requisições por minuto), comparado com o que o app se propõe;
//   - REPETIÇÃO da mesma URL em sequência, que é a assinatura de looping;
//   - o que o SIGAA respondeu (portal, turma, login), que denuncia sessão
//     expirada no meio de um crawl — o modo de falha que devolve coleta vazia
//     sem erro nenhum.
//
// Não é modal e não bloqueia sync: fica aberta enquanto a coleta roda, que é
// justamente quando ela serve.
//
// A regra do que NÃO entra vive em core/http/Trafego.h: sem corpo, sem cookie,
// sem senha. O botão "Copiar" existe para colar num issue público, e é isso que
// torna a regra obrigatória.

#include <QDialog>

#include <memory>

#include "core/http/Trafego.h"

class QStandardItemModel;

namespace Ui {
class JanelaDiagnostico;
}

namespace sigaa::ui {

class JanelaDiagnostico : public QDialog {
    Q_OBJECT

public:
    explicit JanelaDiagnostico(QWidget* pai = nullptr);
    ~JanelaDiagnostico() override;

signals:
    // Ponte entre a thread do sync e a da UI. O observador de core/http roda na
    // thread que fez a requisição; emitir um sinal com argumento copiado é a
    // travessia segura — tocar QWidget de fora da thread de UI é falha dura no
    // Qt, não degradação.
    void chegou(const sigaa::http::EventoRequisicao& ev);

private:
    void acrescentar(const http::EventoRequisicao& ev);
    void atualizarContadores();
    void copiar();
    void limpar();

    std::unique_ptr<Ui::JanelaDiagnostico> formulario_;
    QStandardItemModel* modelo_{nullptr};
    int observador_{0};

    // Quantas vezes a URL da última linha se repetiu em sequência. É o detector
    // de looping, e mora aqui porque depende da ORDEM de chegada — o histórico
    // sozinho não distingue "20 vezes ao longo do dia" de "20 vezes seguidas".
    QString ultimaUrl_;
    int repeticoes_{0};
    int erros_{0};
};

} // namespace sigaa::ui
