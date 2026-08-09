#pragma once
// Onboarding: a primeira coisa que o usuário vê quando não há credencial.
//
// A regra que molda esta tela: NUNCA GUARDAR SEM VERIFICAR. Uma senha errada
// guardada no cofre não fica parada — o ciclo automático a tentaria a cada 20
// minutos, e o SIGAA bloqueia a conta depois de algumas tentativas erradas. O
// usuário descobriria o bloqueio dias depois, sem nenhuma pista de que veio
// daqui. Então: login de verdade primeiro, cofre só depois do "ok" do servidor.
//
// Pelo mesmo motivo o diálogo conta as falhas e para de encorajar tentativa
// atrás de tentativa: quem errou duas vezes provavelmente não vai acertar na
// terceira chutando, e a terceira é cara.
//
// O layout vive em forms/DialogoLogin.ui (Qt Designer). Aqui fica só o que o
// Designer não sabe expressar: o que depende de plat::cofreDisponivel() em
// tempo de execução, o olho de revelar senha dentro do campo, e a verificação
// de rede fora da thread da GUI.

#include <QDialog>
#include <QString>

#include <memory>

class QPushButton;

namespace Ui {
class DialogoLogin;
}

namespace sigaa::ui {

class DialogoLogin : public QDialog {
    Q_OBJECT

public:
    explicit DialogoLogin(QWidget* pai = nullptr);
    ~DialogoLogin() override;   // fora de linha: Ui::DialogoLogin é incompleto aqui

    // Válidos após exec() == Accepted. A senha é zerada quando o diálogo morre.
    QString login() const;
    QString senha() const;

private:
    void montarCampoSenha();
    void montarTextosDoCofre();
    void tentar();
    void aoVerificar();
    void ocupado(bool sim);
    void dizer(const QString& html);

    std::unique_ptr<Ui::DialogoLogin> formulario_;

    // Criado em código: o Designer só monta os botões padrão do
    // QDialogButtonBox, e "Entrar" não é um deles.
    QPushButton* botaoEntrar_{nullptr};

    // Escritos na thread de verificação, lidos na da GUI depois de finished() —
    // a entrega em fila do signal sincroniza os dois lados.
    bool loginOk_{false};
    QString erroLogin_;

    int falhas_{0};
    QString loginTentado_;
    QString senhaTentada_;
};

} // namespace sigaa::ui
