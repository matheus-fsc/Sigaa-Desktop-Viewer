#include "ui/CargaSemanal.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

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
// Os quadradinhos dos dias: 7 de 9 px com 3 de vão = 81 px de fileira.
constexpr int kLadoDia = 9;
constexpr int kVaoDia = 3;
constexpr int kFileiraDias = 7 * kLadoDia + 6 * kVaoDia;

QString nomeMes(QDate d) {
    QString m = QLocale(QLocale::Portuguese, QLocale::Brazil).toString(d, QStringLiteral("MMM"));
    m.remove(QLatin1Char('.'));
    return m.toLower();
}

QString mesCurto(QDate d) { return QString::number(d.day()) + QLatin1Char(' ') + nomeMes(d); }

// "6–12 out" ou "29 set – 5 out": o rótulo diz que a coluna é uma SEMANA, de
// segunda a domingo. Só a segunda ("29 set") se lia como a data de alguma
// coisa, e não como o começo de um intervalo.
QString intervalo(QDate inicio) {
    const QDate fim = inicio.addDays(6);
    if (inicio.month() == fim.month()) {
        return QStringLiteral("%1–%2 %3").arg(inicio.day()).arg(fim.day()).arg(nomeMes(fim));
    }
    return QStringLiteral("%1 – %2").arg(mesCurto(inicio), mesCurto(fim));
}

QFont fontePequena(const QFont& base, double fator) {
    QFont f = base;
    f.setPointSizeF(f.pointSizeF() * fator);
    return f;
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

void CargaSemanal::mostrarDias(bool sim) {
    mostrarDias_ = sim;
    updateGeometry();
    update();
}

int CargaSemanal::alturaRotulos() const {
    const QFontMetrics fm(font());
    int h = tema::esp(2) + fm.height() + 3;            // intervalo + sublinhado
    if (mostrarDias_) {
        h += tema::esp(1) + kLadoDia + 1 + QFontMetrics(fontePequena(font(), 0.72)).height();
    }
    return h + tema::esp(1) + 2 * fm.height();         // resumo, até 2 linhas
}

QRect CargaSemanal::quadradoDoDia(int i, int d) const {
    const QRect col = areaDaColuna(i);
    const int y = height() - alturaRotulos() + tema::esp(2) + QFontMetrics(font()).height() + 3 +
                  tema::esp(1);
    const int x = col.center().x() - kFileiraDias / 2 + d * (kLadoDia + kVaoDia);
    return {x, y, kLadoDia, kLadoDia};
}

QString CargaSemanal::dicaDoDia(int i, int d) const {
    const CargaSemana& w = semanas_[static_cast<size_t>(i)];
    const QDate dia = w.inicio.addDays(d);
    const QString data = QLocale(QLocale::Portuguese, QLocale::Brazil)
                             .toString(dia, QStringLiteral("ddd dd/MM"))
                             .remove(QLatin1Char('.'));
    const CargaDia& c = w.dias[static_cast<size_t>(d)];
    if (c.itens.isEmpty()) return QStringLiteral("%1 — livre").arg(data);
    return QStringLiteral("%1\n%2").arg(data, c.itens.join(QLatin1Char('\n')));
}

void CargaSemanal::destacar(QDate inicio) {
    destaque_ = inicio;
    update();
}

QSize CargaSemanal::sizeHint() const {
    const QFontMetrics fm(font());
    const int blocos = kMaxBlocos * (kAltBloco + kVaoBloco);
    const int larg = mostrarDias_ ? kFileiraDias + 16 : kLargBloco + 16;
    return {static_cast<int>(std::max<size_t>(semanas_.size(), 6)) * larg,
            blocos + alturaRotulos() + (marcarHoje_ ? fm.height() + tema::esp(2) : 0)};
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
    const int baseBlocos = height() - alturaRotulos();
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

        // Rótulos: o intervalo da semana (negrito; accent na de hoje), os
        // dias e o resumo.
        int ty = baseBlocos + tema::esp(2);
        QFont negrito = font();
        negrito.setWeight(QFont::Bold);
        p.setFont(negrito);

        p.setPen(tema::token(filtrada || estaSemana ? "accent"
                             : sob                  ? "text"
                                                    : "text-2"));
        // O intervalo inteiro quando cabe; na coluna estreita do painel, só a
        // segunda-feira, como antes.
        QString rotulo = intervalo(w.inicio);
        if (QFontMetrics(negrito).horizontalAdvance(rotulo) > col.width() - 4) {
            rotulo = mesCurto(w.inicio);
        }
        p.drawText(QRect(col.left(), ty, col.width(), fm.height()), Qt::AlignHCenter, rotulo);
        ty += fm.height();
        // Sublinhado sob a data: accent na semana filtrada, cinza no hover.
        if (filtrada || sob) {
            const int lr = QFontMetrics(negrito).horizontalAdvance(rotulo);
            p.fillRect(QRect(col.center().x() - lr / 2, ty, lr, 2),
                       tema::token(filtrada ? "accent" : "line-strong"));
        }
        ty += 3;

        if (mostrarDias_) {
            ty += tema::esp(1);
            const QFont letras = fontePequena(font(), 0.72);
            static const QString kIniciais = QStringLiteral("STQQSSD");
            for (int d = 0; d < 7; ++d) {
                const QRectF q = QRectF(quadradoDoDia(i, d)).adjusted(0.5, 0.5, -0.5, -0.5);
                const CargaDia& c = w.dias[static_cast<size_t>(d)];
                const QDate dia = w.inicio.addDays(d);
                const bool passou = dia < hoje;
                const bool ocupado = c.provas > 0 || c.entregas > 0;

                p.setPen(Qt::NoPen);
                p.setBrush(Qt::NoBrush);
                if (passou && ocupado) {
                    p.setBrush(corPassado());
                } else if (c.provas > c.inferidas) {
                    p.setBrush(tema::cor::urgente());
                } else if (c.provas > 0) {
                    QPen caneta(tema::cor::urgente());
                    caneta.setWidthF(1.2);
                    p.setPen(caneta);
                } else if (c.entregas > 0) {
                    p.setBrush(tema::token("surface-3"));
                    p.setPen(tema::token("line-strong"));
                } else {
                    // Livre: só o contorno, mais apagado no que já passou.
                    p.setPen(tema::token(passou ? "line" : "line-strong"));
                }
                p.drawRoundedRect(q, 2, 2);

                // Duas ou mais provas no MESMO dia: um ponto no meio. É o dia
                // que mais pede preparo antecipado, e sem a marca ele se
                // confundia com um dia de uma prova só.
                if (!passou && c.provas >= 2) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(tema::token("bg"));
                    p.drawEllipse(q.center(), 1.6, 1.6);
                }

                // Hoje ganha um anel accent por fora; o dia sob o mouse, um
                // anel neutro — a dica dele está aparecendo.
                const bool sobEste = sob && d == diaSobMouse_;
                if (dia == hoje || sobEste) {
                    QPen anel(tema::token(dia == hoje ? "accent" : "text-2"));
                    anel.setWidthF(1.2);
                    p.setPen(anel);
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(q.adjusted(-2, -2, 2, 2), 3, 3);
                }

                p.setFont(letras);
                p.setPen(tema::token(dia == hoje ? "accent" : "text-3"));
                p.drawText(QRectF(q.left() - 3, q.bottom() + 1, q.width() + 6,
                                  QFontMetrics(letras).height()),
                           Qt::AlignHCenter | Qt::AlignTop, kIniciais.mid(d, 1));
            }
            ty += kLadoDia + 1 + QFontMetrics(letras).height();
        }
        ty += tema::esp(1);

        p.setFont(fontePequena(font(), 0.88));
        p.setPen(tema::token("text-3"));
        p.drawText(QRect(col.left(), ty, col.width(), 2 * fm.height()),
                   Qt::AlignHCenter | Qt::AlignTop, resumo(w));
    }
}

void CargaSemanal::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const int i = colunaEm(pos);
    int d = -1;
    if (mostrarDias_ && i >= 0) {
        for (int k = 0; k < 7; ++k) {
            // Alvo um pouco maior que o quadradinho: 9 px é pouco para o mouse.
            if (quadradoDoDia(i, k).adjusted(-2, -3, 2, 3).contains(pos)) d = k;
        }
    }
    if (i == sobMouse_ && d == diaSobMouse_) return;
    sobMouse_ = i;
    diaSobMouse_ = d;
    if (i >= 0 && d >= 0) {
        // Na hora, e não depois do atraso da dica: quem passa o mouse pela
        // fileira está lendo dia a dia.
        QToolTip::showText(e->globalPosition().toPoint(), dicaDoDia(i, d), this);
    } else if (i >= 0) {
        QToolTip::hideText();
        setToolTip(QStringLiteral("Semana de %1 a %2 — clique para ver só ela na lista")
                       .arg(semanas_[i].inicio.toString(QStringLiteral("dd/MM")),
                            semanas_[i].inicio.addDays(6).toString(QStringLiteral("dd/MM"))));
    }
    update();
}

void CargaSemanal::mousePressEvent(QMouseEvent* e) {
    const int i = colunaEm(e->position().toPoint());
    if (i >= 0) emit semanaClicada(semanas_[i].inicio);
}

void CargaSemanal::leaveEvent(QEvent*) {
    sobMouse_ = -1;
    diaSobMouse_ = -1;
    update();
}

} // namespace sigaa::ui
