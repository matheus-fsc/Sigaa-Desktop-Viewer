#pragma once
// "Onde o semestre aperta": horas de estudo planejadas por semana e por
// matéria, contra o tempo livre do aluno (já sem as aulas).
//
// Uma coluna por semana. A barra empilha as matérias; a linha tracejada é o
// tempo livre; o que passa dele fica hachurado, com "+3h" em cima. O que já
// foi feito é verde: na semana atual, embaixo da pilha; nas que passaram, a
// barra inteira, com o contorno tracejado do que estava planejado. Semana
// crítica (80%+ do livre, ou 3+ provas) tem fundo laranja e "! crítica".
//
// Sob cada coluna, sete fatias, uma por dia: o losango da prova no dia exato
// (vazado quando a data foi deduzida) e uma faixa de calor com o estudo do
// dia. A semana atual tem moldura; a escolhida, moldura azul.
//
// Clicar numa semana a escolhe (o "O que fazer" filtra por ela); clicar de
// novo desfaz. Passar o mouse mostra o resumo da semana.

#include <QColor>
#include <QDate>
#include <QString>
#include <QWidget>

#include <array>
#include <functional>
#include <vector>

namespace sigaa::ui {

struct MateriaCarga {
    QString nome;    // "Compiladores"
    QString curto;   // "COMP"
    QColor cor;
};

struct SemanaCarga {
    QDate inicio;                  // segunda-feira
    int livre{0};                  // minutos de estudo disponíveis
    std::vector<int> porMateria;   // minutos planejados, na ordem das matérias
    int feito{0};                  // minutos feitos (sessões + agentes)
    int viaAgente{0};              // dos feitos, quanto veio de agentes
    std::array<int, 7> dia{};      // minutos planejados por dia
    struct Prova {
        int dia{0};                // 0 = segunda
        int materia{0};
        QString texto;             // "ter 06/10 07:55 · IA · Avaliação 1"
        bool deduzida{false};
    };
    std::vector<Prova> provas;

    int total() const;
    bool critica() const;
};

class GraficoCarga : public QWidget {
public:
    explicit GraficoCarga(QWidget* pai = nullptr);

    void definir(std::vector<MateriaCarga> materias, std::vector<SemanaCarga> semanas, QDate hoje);
    // -1 = nenhuma.
    void escolher(int semana);
    int escolhida() const { return escolhida_; }
    // Destaca uma matéria e apaga as outras; -1 volta todas.
    void isolar(int materia);
    int isolada() const { return isolada_; }

    std::function<void(int)> aoEscolher;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent* e) override;

private:
    QRect coluna(int i) const;
    int colunaEm(QPoint p) const;
    int escala() const;          // minutos no topo do eixo
    QString dica(int i) const;

    std::vector<MateriaCarga> materias_;
    std::vector<SemanaCarga> semanas_;
    QDate hoje_;
    int escolhida_{-1};
    int sobMouse_{-1};
    int isolada_{-1};
};

} // namespace sigaa::ui
