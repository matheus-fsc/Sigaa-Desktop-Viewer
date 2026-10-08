#pragma once
// Controles pequenos que o Qt não tem prontos e a aba Estudo usa em toda
// página: o interruptor (liga/desliga com efeito imediato), o seletor
// segmentado (uma escolha entre poucas, todas à vista) e o passo − / +.
//
// POR QUE NÃO QCheckBox, QComboBox E QDoubleSpinBox: os três funcionam, mas
// escondem o que importa. Um combo esconde as outras opções atrás de um
// clique, e "Fácil / Média / Difícil" é decisão de olhar e tocar. O spin de
// 0,1 h com setinhas de 12 px é ruim de acertar; meia hora por clique, com o
// valor grande no meio, é o passo que o aluno pensa. E o checkbox não diz
// "vale já" como um interruptor diz.
//
// As cores vêm da folha de estilo (`#segmentado`, `#passo`) ou dos tokens
// (o interruptor, que é pintado).

#include <QAbstractButton>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;

namespace sigaa::ui {

// Liga/desliga, 44×26. Checkable; o sinal é o `toggled` de sempre.
class Interruptor : public QAbstractButton {
public:
    explicit Interruptor(QWidget* pai = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
};

// Uma escolha entre poucas opções, lado a lado.
class Segmentado : public QWidget {
public:
    Segmentado(const QStringList& rotulos, QWidget* pai = nullptr);

    int indice() const { return indice_; }
    // Sem chamar `aoMudar`.
    void definir(int i);

    std::function<void(int)> aoMudar;

private:
    std::vector<QPushButton*> botoes_;
    int indice_{-1};
};

// − valor +. Inteiro, de `passo` em `passo`, entre `minimo` e `maximo`.
class Passo : public QWidget {
public:
    Passo(int minimo, int maximo, int passo, std::function<QString(int)> formato,
          QWidget* pai = nullptr);

    int valor() const { return valor_; }
    // Sem chamar `aoMudar`.
    void definir(int v);
    // O nome para leitor de tela: "Segunda: horas disponíveis".
    void definirNome(const QString& nome);

    std::function<void(int)> aoMudar;

private:
    void mostrar();

    int minimo_, maximo_, passo_, valor_;
    std::function<QString(int)> formato_;
    QPushButton* menos_{nullptr};
    QPushButton* mais_{nullptr};
    QLabel* rotulo_{nullptr};
};

// Botão com cara de link ("Desfazer", "Ver proposta").
QPushButton* botaoLink(const QString& texto, QWidget* pai);

// Pílula de destaque ("↑ pelo agente"). `tom`: "acento", "aviso", "ok".
QLabel* pilula(const QString& texto, const char* tom, QWidget* pai);

} // namespace sigaa::ui
