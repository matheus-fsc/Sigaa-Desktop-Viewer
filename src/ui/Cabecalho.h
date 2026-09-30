#pragma once
// Peças do cabeçalho da janela principal, desenhadas como no protótipo das
// telas (SIGAA Web.dc.html): as abas embutidas na barra com o contador em
// pílula, e o avatar da conta.
//
// POR QUE AS ABAS SAÍRAM DO QTabWidget: a barra de abas do Qt só sabe pintar
// texto, e o desenho pede duas tintas na mesma aba — o nome, e a contagem numa
// pílula com fundo próprio ("Agenda  [1 hoje]"). Além disso ela vivia numa
// segunda linha, abaixo das ações, e o protótipo junta tudo numa só.
//
// O QTabWidget CONTINUA sendo o dono das páginas e da aba atual; ele só perde
// a barra visível. `NavegacaoAbas` é um espelho: lê os títulos dele, e clicar
// num botão troca a aba dele. Assim nada do que já conversa com o QTabWidget
// (a aba lembrada, os `setTabText` com as contagens) precisou mudar.

#include <QAbstractButton>
#include <QIcon>
#include <QWidget>

#include <vector>

class QTabWidget;

namespace sigaa::ui {

// Avatar da conta: círculo em accent-soft com as iniciais em accent. Sem
// iniciais (login vazio ou que não começa por letras, como uma matrícula),
// devolve um ícone nulo e quem chama mantém o desenho de pessoa.
QIcon avatarConta(const QString& login, qreal dpr);

// Um botão de aba: rótulo + pílula de contagem + sublinhado quando ativo.
class BotaoAba : public QAbstractButton {
public:
    explicit BotaoAba(QWidget* pai = nullptr);

    // "Agenda (1 hoje)" vira rótulo "Agenda" e pílula "1 hoje". Sem parênteses
    // no fim, não há pílula.
    void definirTitulo(const QString& titulo);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QFont fonteRotulo() const;
    QFont fontePilula() const;
    int larguraPilula() const;

    QString rotulo_;
    QString contagem_;
};

class NavegacaoAbas : public QWidget {
public:
    NavegacaoAbas(QTabWidget* abas, QWidget* pai = nullptr);

    // Relê os títulos do QTabWidget. Chamar depois de cada `setTabText` — o
    // QTabWidget não avisa quando um título muda.
    void sincronizar();

private:
    QTabWidget* abas_;
    std::vector<BotaoAba*> botoes_;
};

} // namespace sigaa::ui
