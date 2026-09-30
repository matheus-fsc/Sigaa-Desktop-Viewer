#pragma once
// "Carga por semana" da aba Provas: uma coluna por semana, com um bloco por
// prova (laranja; vazado quando deduzida) e um por entrega pendente (cinza).
//
// Responde a pergunta de domingo à noite — "quando aperta?" — que a tabela de
// provas não responde de relance: ela lista, mas não mostra acúmulo. Clicar
// numa semana filtra a tabela para ela; clicar de novo desfaz.

#include <QDate>
#include <QWidget>

#include <vector>

#include "ui/Modelos.h"

namespace sigaa::ui {

class CargaSemanal : public QWidget {
    Q_OBJECT
public:
    explicit CargaSemanal(QWidget* pai = nullptr);

    void definir(std::vector<CargaSemana> semanas);

    // A semana em destaque (a filtrada na tabela), ou inválida para nenhuma.
    void destacar(QDate inicio);

    // Marca a semana de hoje com "▼ hoje" no topo da coluna. Para a visão do
    // período inteiro, onde a semana atual está no meio de vinte; no painel
    // ela já é a primeira coluna e dispensa a marca.
    void marcarSemanaAtual(bool sim);

    // A cor dos blocos do que já passou. Pública para a legenda usar a mesma.
    static QColor corPassado();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void semanaClicada(QDate inicio);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    int colunaEm(QPoint p) const;
    QRect areaDaColuna(int i) const;

    std::vector<CargaSemana> semanas_;
    QDate destaque_;
    int sobMouse_{-1};
    bool marcarHoje_{false};
};

} // namespace sigaa::ui
