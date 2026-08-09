#pragma once
// Calendário mensal com as provas marcadas.
//
// Existe porque uma lista ordenada responde "qual é a próxima?" mas não
// responde "como está a minha semana de 20 a 27?". Três provas em quatro dias
// só aparecem como aglomeração quando se vê o mês inteiro; na lista são três
// linhas iguais a quaisquer outras.
//
// A marcação é um PONTO abaixo do número, não um fundo pintado. Pintar o fundo
// cobriria as duas marcas que o estilo nativo desenha — "hoje" e a seleção —
// que são justamente as que o usuário usa para se localizar no mês.
//
// Ponto CHEIO = prova cadastrada pelo professor.
// Ponto VAZADO = data deduzida de tópico de aula (RECON §1.6). A diferença
// sobrevive até o desenho de propósito: quem estuda para a data errada perde
// a prova, e a legenda ao lado explica o que cada ponto quer dizer.

#include <QCalendarWidget>
#include <QDate>
#include <QMap>

#include "ui/Modelos.h"

namespace sigaa::ui {

class CalendarioProvas : public QCalendarWidget {
    Q_OBJECT

public:
    explicit CalendarioProvas(QWidget* pai = nullptr);

    void definirProvas(const QMap<QDate, DiaComProva>& provas);

    // Quantas provas há no dia (0 se nenhuma). A janela usa para decidir se
    // vale filtrar a lista pelo dia clicado.
    int provasEm(QDate d) const;

protected:
    void paintCell(QPainter* p, const QRect& r, QDate d) const override;

private:
    QMap<QDate, DiaComProva> provas_;
    // O mês exibido só salta para a próxima prova UMA vez, na primeira carga.
    // Sem isto, o sync automático arrastaria o calendário de volta a cada 20
    // minutos enquanto o usuário estivesse folheando outro mês.
    bool jaPosicionou_{false};
};

} // namespace sigaa::ui
