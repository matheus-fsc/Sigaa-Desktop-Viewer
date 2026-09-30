#include "ui/CargaSemanal.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

// Medidas do protótipo: blocos de 10 px de altura com 3 de vão, coluna com
// largura dividida igualmente. Acima de kMaxBlocos a coluna para de crescer e
// o número passa a dizer o resto — uma semana de 12 entregas não pode empurrar
// o painel para fora da tela.
constexpr int kAltBloco = 10;
constexpr int kVaoBloco = 3;
constexpr int kMaxBlocos = 7;
constexpr int kLargBloco = 44;

QString mesCurto(QDate d) {
    QString m = QLocale(QLocale::Portuguese, QLocale::Brazil).toString(d, QStringLiteral("MMM"));
    m.remove(QLatin1Char('.'));
    return QString::number(d.day()) + QLatin1Char(' ') + m.toLower();
}

QString resumo(const CargaSemana& w) {
    if (w.provas == 0 && w.entregas == 0) return QStringLiteral("livre");
    QStringList partes;
    if (w.provas > 0) {
        partes << (w.provas == 1 ? QStringLiteral("1 prova")
                                 : QStringLiteral("%1 provas").arg(w.provas));
    }
    if (w.entregas > 0) {
        partes << (w.entregas == 1 ? QStringLiteral("1 entrega")
                                   : QStringLiteral("%1 entregas").arg(w.entregas));
    }
    return partes.join(QLatin1Char('\n'));
}

} // namespace

QColor CargaSemanal::corPassado() {
    // Tracejado seria o óbvio, mas em blocos de 10 px não se lê. Uma tinta
    // própria, fria e apagada, que não é nenhuma das outras três: nem laranja
    // (prova), nem o cinza-superfície (entrega pendente), nem o accent.
    //
    // Opaca (a mistura de text-3 com a superfície), e não com alfa: a legenda
    // é rich text, que descarta o alfa, e o quadradinho sairia de outra cor.
    const QColor a = tema::token("text-3");
    const QColor b = tema::token("surface");
    constexpr double t = 0.45;
    return QColor::fromRgbF(a.redF() * t + b.redF() * (1 - t), a.greenF() * t + b.greenF() * (1 - t),
                            a.blueF() * t + b.blueF() * (1 - t));
}

CargaSemanal::CargaSemanal(QWidget* pai) : QWidget(pai) {
    setMouseTracking(true);
    // Cresce na vertical: as barras nascem da base e ganham altura quando a
    // coluna tem espaço (ver paintEvent).
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void CargaSemanal::definir(std::vector<CargaSemana> semanas) {
    semanas_ = std::move(semanas);
    // Texto para leitor de tela: o desenho sozinho não diz nada a quem não vê.
    QStringList partes;
    for (const auto& w : semanas_) {
        partes << QStringLiteral("semana de %1: %2")
                      .arg(mesCurto(w.inicio), resumo(w).replace(QLatin1Char('\n'),
                                                                 QStringLiteral(", ")));
    }
    setAccessibleDescription(partes.join(QStringLiteral("; ")));
    update();
}

void CargaSemanal::marcarSemanaAtual(bool sim) {
    marcarHoje_ = sim;
    update();
}

void CargaSemanal::destacar(QDate inicio) {
    destaque_ = inicio;
    update();
}

QSize CargaSemanal::sizeHint() const {
    const QFontMetrics fm(font());
    const int blocos = kMaxBlocos * (kAltBloco + kVaoBloco);
    return {static_cast<int>(std::max<size_t>(semanas_.size(), 6)) * (kLargBloco + 16),
            blocos + tema::esp(2) + 3 * fm.height() + tema::esp(2)};
}

QSize CargaSemanal::minimumSizeHint() const {
    return {6 * (kLargBloco + 4), sizeHint().height()};
}

QRect CargaSemanal::areaDaColuna(int i) const {
    const int n = std::max<int>(1, static_cast<int>(semanas_.size()));
    const int larg = width() / n;
    return {i * larg, 0, larg, height()};
}

int CargaSemanal::colunaEm(QPoint p) const {
    for (int i = 0; i < static_cast<int>(semanas_.size()); ++i) {
        if (areaDaColuna(i).contains(p)) return i;
    }
    return -1;
}

void CargaSemanal::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QFontMetrics fm(font());
    // O chão fica no pé do widget, com o espaço dos rótulos embaixo; os
    // blocos crescem para caber a altura que sobrar, até um teto — acima
    // dele, bloco alto demais parece barra de progresso, não contagem.
    const int alturaRotulos = tema::esp(2) + 3 + 3 * fm.height();
    const int baseBlocos = height() - alturaRotulos;
    // Com a marca de hoje, o alto da coluna fica reservado para ela: a pilha
    // mais alta não pode encostar no "▼ hoje".
    const int topo = marcarHoje_ ? fm.height() + tema::esp(2) : 0;
    const int passo = std::clamp((baseBlocos - topo) / kMaxBlocos, kAltBloco + kVaoBloco, 22);
    const int altBloco = passo - kVaoBloco;
    const QDate hoje = QDate::currentDate();

    for (int i = 0; i < static_cast<int>(semanas_.size()); ++i) {
        const CargaSemana& w = semanas_[i];
        const QRect col = areaDaColuna(i);

        // SEM fundo de coluna no hover nem no destaque. A primeira versão
        // pintava um retângulo da altura inteira — um bloco enorme que cobria
        // o gráfico ao passar o mouse. O retorno agora é tipográfico: a data
        // acende no hover, e a semana filtrada ganha um sublinhado (abaixo).
        const bool filtrada = w.inicio == destaque_;
        const bool sob = i == sobMouse_;
        const bool estaSemana = hoje >= w.inicio && hoje < w.inicio.addDays(7);

        const int larg = std::min(kLargBloco, col.width() - 12);
        const int x = col.center().x() - larg / 2;
        int y = baseBlocos;

        // De baixo para cima: o que JÁ PASSOU na base, depois entregas
        // pendentes, provas no topo — a prova por vir é o que pesa, e fica
        // onde o olho bate. O passado fica no chão da coluna: conta a semana,
        // mas não disputa atenção.
        std::vector<int> blocos;   // 3 = já passou, 0 = entrega, 1 = prova, 2 = inferida
        for (int k = 0; k < w.provasPassadas + w.entregasPassadas; ++k) blocos.push_back(3);
        for (int k = 0; k < w.entregas - w.entregasPassadas; ++k) blocos.push_back(0);
        for (int k = 0; k < w.provas - w.provasPassadas - w.inferidas; ++k) blocos.push_back(1);
        for (int k = 0; k < w.inferidas; ++k) blocos.push_back(2);
        // Acima do teto, corta-se de baixo: primeiro o que já passou, depois
        // entregas. Uma prova por vir nunca some.
        while (static_cast<int>(blocos.size()) > kMaxBlocos &&
               (blocos.front() == 3 || blocos.front() == 0)) {
            blocos.erase(blocos.begin());
        }
        if (static_cast<int>(blocos.size()) > kMaxBlocos) blocos.resize(kMaxBlocos);

        for (const int tipo : blocos) {
            y -= altBloco;
            const QRectF r(x, y, larg, altBloco);
            if (tipo == 3) {
                p.setPen(Qt::NoPen);
                p.setBrush(corPassado());
                p.drawRoundedRect(r, 3, 3);
            } else if (tipo == 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(tema::token("surface-3"));
                p.drawRoundedRect(r, 3, 3);
            } else if (tipo == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(tema::cor::urgente());
                p.drawRoundedRect(r, 3, 3);
            } else {
                QPen caneta(tema::cor::urgente());
                caneta.setWidthF(1.4);
                p.setPen(caneta);
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(0.7, 0.7, -0.7, -0.7), 3, 3);
            }
            y -= kVaoBloco;
        }

        if (marcarHoje_ && estaSemana) {
            // Pílula accent logo acima da pilha: acha-se a semana atual de
            // longe, mesmo no meio de um semestre inteiro de colunas, e ela
            // fica colada à coluna a que se refere.
            QFont fm2 = font();
            fm2.setWeight(QFont::Bold);
            fm2.setPointSizeF(fm2.pointSizeF() * 0.85);
            const QString marca = QStringLiteral("▼ hoje");
            const QFontMetrics mm(fm2);
            const int lw = mm.horizontalAdvance(marca) + tema::esp(3);
            const qreal alt = mm.height() + 4;
            const QRectF pil(col.center().x() - lw / 2.0, std::max<qreal>(1, y - alt - 2), lw, alt);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(tema::token("accent-soft"));
            p.drawRoundedRect(pil, pil.height() / 2, pil.height() / 2);
            p.setFont(fm2);
            p.setPen(tema::token("accent"));
            p.drawText(pil, Qt::AlignCenter, marca);
            p.restore();
        }

        // Rótulos: "28 set" (negrito; accent na semana de hoje) e o resumo.
        int ty = baseBlocos + tema::esp(2);
        QFont negrito = font();
        negrito.setWeight(QFont::Bold);
        p.setFont(negrito);

        p.setPen(tema::token(filtrada || estaSemana ? "accent"
                             : sob                  ? "text"
                                                    : "text-2"));
        const QString rotulo = mesCurto(w.inicio);
        p.drawText(QRect(col.left(), ty, col.width(), fm.height()), Qt::AlignHCenter, rotulo);
        ty += fm.height();
        // Sublinhado sob a data: accent na semana filtrada, cinza no hover.
        if (filtrada || sob) {
            const int lr = QFontMetrics(negrito).horizontalAdvance(rotulo);
            p.fillRect(QRect(col.center().x() - lr / 2, ty, lr, 2),
                       tema::token(filtrada ? "accent" : "line-strong"));
        }
        ty += 3;

        QFont pequena = font();
        pequena.setPointSizeF(pequena.pointSizeF() * 0.88);
        p.setFont(pequena);
        p.setPen(tema::token("text-3"));
        p.drawText(QRect(col.left(), ty, col.width(), 2 * fm.height()),
                   Qt::AlignHCenter | Qt::AlignTop, resumo(w));
    }
}

void CargaSemanal::mouseMoveEvent(QMouseEvent* e) {
    const int i = colunaEm(e->position().toPoint());
    if (i != sobMouse_) {
        sobMouse_ = i;
        if (i >= 0) {
            setToolTip(QStringLiteral("Semana de %1 — clique para ver só ela na lista")
                           .arg(semanas_[i].inicio.toString(QStringLiteral("dd/MM"))));
        }
        update();
    }
}

void CargaSemanal::mousePressEvent(QMouseEvent* e) {
    const int i = colunaEm(e->position().toPoint());
    if (i >= 0) emit semanaClicada(semanas_[i].inicio);
}

void CargaSemanal::leaveEvent(QEvent*) {
    sobMouse_ = -1;
    update();
}

} // namespace sigaa::ui
