#include "ui/CalendarioProvas.h"

#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QTextCharFormat>

#include "ui/Tema.h"

#include <algorithm>

namespace sigaa::ui {
namespace {

constexpr qreal kDiametro = 5.0;
constexpr qreal kEspaco = 3.0;
constexpr int kMaxPontos = 3;   // acima disso o dia vira uma fileira ilegível

// Cor do dia, pela urgência. Prova que já passou fica apagada em vez de sumir:
// o aluno usa o mês anterior para lembrar quando foi a primeira avaliação.
QColor corDoDia(QDate d) {
    const int dias = static_cast<int>(QDate::currentDate().daysTo(d));
    if (dias < 0) return cor::apagado();
    if (dias <= 7) return cor::urgente();
    // Azul de destaque, como na legenda do design system ("depois"). Antes era
    // o vermelho de atrasado clareado — e vermelho, mesmo rosado, lê como
    // problema numa prova que ainda está a semanas de distância.
    return tema::cor::acento();
}

} // namespace

CalendarioProvas::CalendarioProvas(QWidget* pai) : QCalendarWidget(pai) {
    setGridVisible(false);
    // Número da semana é ruído aqui: ninguém marca prova por semana ISO.
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
    setSelectionMode(QCalendarWidget::SingleSelection);
    setNavigationBarVisible(true);
    pintarFimDeSemana();
}

void CalendarioProvas::pintarFimDeSemana() {
    // Fim de semana em cinza, não no vermelho padrão do Qt: vermelho neste app
    // quer dizer "atrasado", e um sábado não está atrasado. A cor sai do tema
    // em vigor, então isto roda de novo a cada troca de paleta.
    QTextCharFormat f;
    f.setForeground(cor::apagado());
    setWeekdayTextFormat(Qt::Saturday, f);
    setWeekdayTextFormat(Qt::Sunday, f);
}

void CalendarioProvas::changeEvent(QEvent* ev) {
    QCalendarWidget::changeEvent(ev);
    if (ev->type() == QEvent::PaletteChange) pintarFimDeSemana();
}

int CalendarioProvas::provasEm(QDate d) const {
    const auto it = provas_.constFind(d);
    return it == provas_.constEnd() ? 0 : it->total();
}

void CalendarioProvas::definirProvas(const QMap<QDate, DiaComProva>& provas) {
    provas_ = provas;

    // QDate() como chave limpa TODOS os formatos. Sem isto, um dia que deixou
    // de ter prova continuaria em negrito para sempre.
    setDateTextFormat(QDate(), QTextCharFormat());

    QDate proxima;
    const QDate hoje = QDate::currentDate();
    for (auto it = provas_.constBegin(); it != provas_.constEnd(); ++it) {
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        setDateTextFormat(it.key(), f);

        if (it.key() >= hoje && (!proxima.isValid() || it.key() < proxima)) {
            proxima = it.key();
        }
    }

    if (!jaPosicionou_ && proxima.isValid()) {
        jaPosicionou_ = true;
        setCurrentPage(proxima.year(), proxima.month());
    }
    updateCells();
}

void CalendarioProvas::mostrarSelecao(bool sim) {
    if (mostrarSelecao_ == sim) return;
    mostrarSelecao_ = sim;
    updateCells();
}

void CalendarioProvas::paintCell(QPainter* p, const QRect& r, QDate d) const {
    if (!mostrarSelecao_ && d == selectedDate()) {
        // A célula como se não estivesse selecionada: fundo da grade e o
        // número na cor que o dia teria — apagado fora do mês, cinza no fim de
        // semana, texto comum no resto. Pintado aqui, e não por folha de
        // estilo: trocar a folha do widget em tempo de execução faz o Qt
        // reposicionar o mês exibido.
        p->save();
        p->fillRect(r, palette().color(QPalette::Base));
        const bool foraDoMes = d.month() != monthShown();
        const bool fimDeSemana = d.dayOfWeek() >= 6;
        p->setPen(foraDoMes     ? palette().color(QPalette::Disabled, QPalette::Text)
                  : fimDeSemana ? cor::apagado()
                                : palette().color(QPalette::Text));
        p->drawText(r, Qt::AlignCenter, QString::number(d.day()));
        p->restore();
    } else {
        QCalendarWidget::paintCell(p, r, d);
    }

    const auto it = provas_.constFind(d);
    if (it == provas_.constEnd() || it->total() == 0) return;

    // Parênteses em volta de std::min: no Windows, <windows.h> define `min` e
    // `max` como MACRO, e sem NOMINMAX (que este alvo não define) a chamada
    // vira um erro de compilação difícil de ler assim que alguém incluir um
    // header do SO nesta cadeia.
    const int cheios = (std::min)(it->confirmadas, kMaxPontos);
    const int vazados = (std::min)(it->inferidas, kMaxPontos - cheios);
    const int n = cheios + vazados;
    if (n == 0) return;

    const qreal largura = n * kDiametro + (n - 1) * kEspaco;
    qreal x = r.center().x() - largura / 2.0;
    const qreal y = r.bottom() - kDiametro - 2.0;
    const QColor c = corDoDia(d);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < n; ++i) {
        // Vazado precisa de meio pixel de recuo: o contorno é desenhado sobre a
        // borda do retângulo, e sem o ajuste os dois tipos de ponto saem com
        // diâmetros visivelmente diferentes lado a lado.
        if (i < cheios) {
            p->setPen(Qt::NoPen);
            p->setBrush(c);
            p->drawEllipse(QRectF(x, y, kDiametro, kDiametro));
        } else {
            QPen caneta(c);
            caneta.setWidthF(1.2);
            p->setPen(caneta);
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(QRectF(x + 0.6, y + 0.6, kDiametro - 1.2, kDiametro - 1.2));
        }
        x += kDiametro + kEspaco;
    }
    p->restore();
}

} // namespace sigaa::ui
