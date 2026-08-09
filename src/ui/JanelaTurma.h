#pragma once
// "Entrar na turma", como no SIGAA web: os arquivos que o professor publicou,
// com download de verdade.
//
// Esta janela tem uma SESSÃO PRÓPRIA com o SIGAA, separada da sincronização.
// Não é desperdício: a navegação do SIGAA é uma máquina de estados no servidor
// (o ViewState vale para a view corrente), então um sync automático disparando
// no meio de um download invalidaria a view e o arquivo viria como página de
// erro. Duas sessões custam um login a mais e removem a classe inteira de bug.
//
// Uma operação de rede por vez, sempre fora da thread da GUI. Enquanto uma
// roda, os botões ficam desligados — é isso que torna seguro o `SessaoTurma`
// ser tocado por threads diferentes ao longo do tempo: nunca por duas ao mesmo
// tempo, e a entrega em fila do sinal é a barreira de sincronização.

#include <QDialog>
#include <QString>

#include <memory>
#include <string>
#include <vector>

#include "core/model/Models.h"

class QThread;

namespace Ui {
class JanelaTurma;
}

namespace sigaa {
namespace http { class SigaaSession; }
namespace sync { class SessaoTurma; }
}

namespace sigaa::ui {

class JanelaTurma : public QDialog {
    Q_OBJECT

public:
    // `login`/`senha` são cópias desta execução: a janela abre a própria
    // sessão. Não guarda nada no disco.
    JanelaTurma(Turma turma, std::string login, std::string senha,
                QWidget* pai = nullptr);
    ~JanelaTurma() override;

protected:
    // Tanto accept() quanto reject() passam por aqui — inclusive o X da barra
    // de título e o Esc. É o único ponto que pega TODAS as formas de fechar.
    void done(int r) override;

private:
    void abrir();                 // login + entrar na turma + listar arquivos
    void baixarSelecionados();
    void mostrarArquivos();
    void ocupado(bool sim);
    void status(const QString& msg);

    // Pasta de destino: Documentos/SIGAA/<turma>. Fixa de propósito — um
    // seletor de pasta a cada arquivo transformaria "baixar tudo" em suplício,
    // e o botão "Abrir pasta" resolve o "onde foi parar".
    QString pastaDestino() const;

    std::unique_ptr<Ui::JanelaTurma> formulario_;

    Turma turma_;
    std::string login_;
    std::string senha_;

    // Vivem enquanto a janela viver: a sessão precisa sobreviver entre um
    // download e o próximo, senão cada arquivo custaria um login novo.
    std::unique_ptr<http::SigaaSession> sessao_;
    std::unique_ptr<sync::SessaoTurma> turmaRemota_;

    std::vector<ArquivoTurma> arquivos_;
    bool ocupado_{false};

    // A thread em andamento, ou nullptr.
    //
    // Ela ESCREVE em sessao_/turmaRemota_, que são membros desta janela. Se a
    // janela morresse antes dela, o corpo da thread continuaria mexendo em
    // memória liberada — e um PDF de 11 MB demora o bastante para alguém
    // clicar no X no meio. Por isso `done()` recusa fechar enquanto há
    // trabalho, e o destrutor espera como última linha de defesa.
    QThread* trabalho_{nullptr};
};

} // namespace sigaa::ui
