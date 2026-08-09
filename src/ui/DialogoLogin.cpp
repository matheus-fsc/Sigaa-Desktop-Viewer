#include "ui/DialogoLogin.h"

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QThread>

#include "core/http/SigaaSession.h"
#include "platform/Credenciais.h"
#include "ui/Icones.h"
#include "ui_DialogoLogin.h"

namespace sigaa::ui {
namespace {

// Aceita "123.456.789-01" colado do navegador. Guardamos só os dígitos, que é o
// que o SIGAA espera no campo de login (docs/RECON.md §1.8).
QString soDigitos(const QString& s) {
    QString o;
    for (const QChar c : s) {
        if (c.isDigit()) o.append(c);
    }
    return o;
}

} // namespace

DialogoLogin::DialogoLogin(QWidget* pai)
    : QDialog(pai), formulario_(std::make_unique<Ui::DialogoLogin>()) {
    formulario_->setupUi(this);

    // Aparência das notas (o texto secundário do diálogo). Nada disto dá para
    // deixar no .qss:
    //
    //   - tamanho: o Qt Stylesheet só aceita font-size absoluto, e cravar "8pt"
    //     quebra em tela 4K e ignora a fonte grande de quem enxerga mal;
    //   - cor: o palette() do QSS não conhece o papel placeholder-text, que é o
    //     único que significa "cinza legível" tanto no tema claro quanto no
    //     escuro. Aqui o papel existe.
    //
    // O código antigo usava setEnabled(false) para conseguir o cinza. Funcionava
    // pela aparência errada: rótulo desabilitado sai da ordem de leitura dos
    // leitores de tela, e este texto é exatamente o que explica ao usuário para
    // onde vai a senha dele.
    for (QLabel* l : findChildren<QLabel*>()) {
        if (l->property("classe").toString() != QLatin1String("nota")) continue;

        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        l->setFont(f);

        QPalette p = l->palette();
        p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
        l->setPalette(p);
    }

    montarCampoSenha();
    montarTextosDoCofre();

    botaoEntrar_ = formulario_->botoes->addButton(QStringLiteral("Entrar"),
                                                  QDialogButtonBox::AcceptRole);
    botaoEntrar_->setDefault(true);
    connect(formulario_->botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(botaoEntrar_, &QPushButton::clicked, this, &DialogoLogin::tentar);
}

DialogoLogin::~DialogoLogin() = default;

void DialogoLogin::montarCampoSenha() {
    // Olhinho de revelar: sem ele o usuário com senha longa erra, não sabe que
    // errou, e gasta tentativa — que é justamente o recurso caro aqui. Vive
    // dentro do QLineEdit, coisa que o Designer não sabe montar.
    auto* olho = formulario_->campoSenha->addAction(
        icone(QStringLiteral("olho-fechado"), palette().color(QPalette::Text)),
        QLineEdit::TrailingPosition);
    olho->setToolTip(QStringLiteral("Mostrar a senha"));

    connect(olho, &QAction::triggered, this, [this, olho] {
        const bool oculta =
            formulario_->campoSenha->echoMode() == QLineEdit::Password;
        formulario_->campoSenha->setEchoMode(oculta ? QLineEdit::Normal
                                                    : QLineEdit::Password);
        olho->setIcon(icone(oculta ? QStringLiteral("olho")
                                   : QStringLiteral("olho-fechado"),
                            palette().color(QPalette::Text)));
        olho->setToolTip(oculta ? QStringLiteral("Ocultar a senha")
                                : QStringLiteral("Mostrar a senha"));
    });
}

void DialogoLogin::montarTextosDoCofre() {
    const bool temCofre = plat::cofreDisponivel();

    formulario_->guardar->setText(
        QStringLiteral("Guardar neste computador (%1)")
            .arg(QString::fromStdString(plat::backendCofre())));
    formulario_->guardar->setChecked(temCofre);
    formulario_->guardar->setEnabled(temCofre);

    // O texto abaixo é deliberadamente honesto sobre o limite. Prometer
    // "criptografado, você está seguro" faria o usuário baixar a guarda em
    // relação ao que a criptografia local não resolve.
    formulario_->notaCofre->setText(
        temCofre ? QStringLiteral(
                       "A senha vai cifrada para o cofre do sistema, com chave "
                       "derivada do seu login do Windows. Isso impede que ela "
                       "vaze num arquivo — backup, pasta sincronizada, "
                       "repositório. Não impede um programa malicioso que já "
                       "esteja rodando na sua conta: ele pede a decifragem ao "
                       "sistema como o app pede.")
                 : QStringLiteral(
                       "Não há cofre disponível neste sistema, então a senha "
                       "não será guardada — você vai precisar digitá-la de "
                       "novo."));
}

QString DialogoLogin::login() const { return loginTentado_; }
QString DialogoLogin::senha() const { return senhaTentada_; }

// O rótulo de recado é rich text (definido no .ui), então quebra de linha é
// <br> e QUALQUER texto vindo de fora precisa ser escapado antes de entrar —
// ver os toHtmlEscaped() abaixo.
void DialogoLogin::dizer(const QString& html) {
    formulario_->recado->setText(html);
    formulario_->recado->setVisible(true);
}

void DialogoLogin::tentar() {
    const QString login = soDigitos(formulario_->campoLogin->text());
    const QString senha = formulario_->campoSenha->text();

    if (login.isEmpty() || senha.isEmpty()) {
        dizer(QStringLiteral("Preencha CPF e senha."));
        return;
    }
    if (login.size() != 11) {
        dizer(QStringLiteral(
                  "CPF tem 11 dígitos; você digitou %1. Confira antes de tentar "
                  "— tentativa errada conta para o bloqueio da conta.")
                  .arg(login.size()));
        return;
    }

    loginTentado_ = login;
    senhaTentada_ = senha;
    ocupado(true);
    dizer(QStringLiteral("Verificando no SIGAA…"));

    // Verificar fora da thread da GUI: o login é uma ida à rede e travaria a
    // janela. Cópias por valor — a thread não pode olhar widgets.
    const std::string l = login.toStdString();
    const std::string s = senha.toStdString();
    auto* th = QThread::create([this, l, s] {
        http::SigaaSession sess;
        std::string e;
        loginOk_ = sess.login(l, s, &e);
        erroLogin_ = QString::fromStdString(e);
    });
    connect(th, &QThread::finished, this, [this, th] {
        th->deleteLater();
        aoVerificar();
    });
    th->start();
}

void DialogoLogin::aoVerificar() {
    ocupado(false);

    if (!loginOk_) {
        ++falhas_;
        senhaTentada_.clear();

        // erroLogin_ vem do servidor: escapar, senão um "<" na mensagem some no
        // rótulo rich text e o usuário lê um erro pela metade.
        QString msg = erroLogin_.isEmpty()
                          ? QStringLiteral("Não consegui entrar.")
                          : QStringLiteral("Não consegui entrar: %1")
                                .arg(erroLogin_.toHtmlEscaped());
        if (falhas_ >= 2) {
            // Parar de encorajar a próxima tentativa é a parte útil aqui.
            msg += QStringLiteral(
                "<br><br><b>Duas tentativas já falharam.</b> O SIGAA bloqueia a "
                "conta após algumas senhas erradas. Confirme a senha entrando "
                "pelo navegador antes de tentar de novo.");
        }
        dizer(msg);
        formulario_->campoSenha->selectAll();
        formulario_->campoSenha->setFocus();
        return;
    }

    // Só agora, com o "ok" do servidor na mão.
    if (formulario_->guardar->isChecked() && plat::cofreDisponivel()) {
        std::string erro;
        if (!plat::guardarNoCofre(loginTentado_.toStdString(),
                                  senhaTentada_.toStdString(), &erro)) {
            // Não é motivo para barrar a entrada: a sessão desta execução vale.
            dizer(QStringLiteral(
                      "Entrei, mas não consegui guardar no cofre (%1). Você vai "
                      "precisar digitar de novo na próxima vez.")
                      .arg(QString::fromStdString(erro).toHtmlEscaped()));
        }
    }
    accept();
}

// Só "Entrar" trava durante a verificação. Cancelar continua vivo de
// propósito: a checagem é uma ida à rede que pode demorar, e prender o usuário
// numa janela modal esperando o SIGAA responder é o tipo de coisa que faz
// alguém matar o processo pelo gerenciador de tarefas.
void DialogoLogin::ocupado(bool sim) {
    botaoEntrar_->setEnabled(!sim);
    formulario_->campoLogin->setEnabled(!sim);
    formulario_->campoSenha->setEnabled(!sim);
    formulario_->guardar->setEnabled(!sim && plat::cofreDisponivel());
}

} // namespace sigaa::ui
