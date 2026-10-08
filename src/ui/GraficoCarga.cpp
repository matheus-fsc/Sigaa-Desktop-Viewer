#include "ui/GraficoCarga.h"

#include <QEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <numeric>

#include "core/planejamento/Planejamento.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

constexpr int kEixo = 34;        // largura dos rótulos do eixo Y
constexpr int kVao = 6;          // entre colunas
constexpr int kAltura = 210;     // da área das barras
constexpr int kTopo = 10;        // respiro para o rótulo do topo do eixo
constexpr int kColunaMin = 44;
constexpr int kLosangos = 24;    // a fileira das provas
constexpr int kCalor = 9;        // a faixa de calor

QString dur(int m) { return QString::fromStdString(planejamento::duracao(m)); }
QString ddmm(QDate d) { return d.toString(QStringLiteral("dd/MM")); }

QColor comAlfa(QColor c, double a) {
    c.setAlphaF(c.alphaF() * a);
    return c;
}

// O losango de uma prova: cheio, ou vazado quando a data foi deduzida.
void losango(QPainter& p, QPointF centro, double lado, const QColor& cor, bool vazado) {
    const double r = lado / std::sqrt(2.0) + 0.5;
    QPolygonF poli;
    poli << QPointF(centro.x(), centro.y() - r) << QPointF(centro.x() + r, centro.y())
         << QPointF(centro.x(), centro.y() + r) << QPointF(centro.x() - r, centro.y());
    p.setPen(QPen(cor, 1.5));
    p.setBrush(vazado ? Qt::NoBrush : QBrush(cor));
    p.drawPolygon(poli);
}

} // namespace

int SemanaCarga::total() const { return std::accumulate(porMateria.begin(), porMateria.end(), 0); }

bool SemanaCarga::critica() const {
    const int t = total();
    return (livre > 0 && t >= 0.8 * livre && t > 0) || provas.size() >= 3;
}

GraficoCarga::GraficoCarga(QWidget* pai) : QWidget(pai) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void GraficoCarga::definir(std::vector<MateriaCarga> materias, std::vector<SemanaCarga> semanas, QDate hoje) {
    materias_ = std::move(materias);
    semanas_ = std::move(semanas);
    hoje_ = hoje;
    if (escolhida_ >= static_cast<int>(semanas_.size())) escolhida_ = -1;
    QStringList partes;
    for (const auto& s : semanas_) {
        partes << QStringLiteral("semana de %1: planejado %2 de %3 livres%4")
                      .arg(ddmm(s.inicio), dur(s.total()), dur(s.livre),
                           s.critica() ? QStringLiteral(", crítica") : QString());
    }
    setAccessibleName(QStringLiteral("Onde o semestre aperta"));
    setAccessibleDescription(partes.join(QStringLiteral("; ")));
    updateGeometry();
    update();
}

void GraficoCarga::escolher(int semana) {
    escolhida_ = semana;
    update();
}

void GraficoCarga::isolar(int materia) {
    isolada_ = materia;
    update();
}

QSize GraficoCarga::sizeHint() const {
    const QFontMetrics fm(font());
    const int n = std::max<int>(1, static_cast<int>(semanas_.size()));
    return {kEixo + 8 + n * kColunaMin + (n - 1) * kVao,
            kTopo + kAltura + 4 + kLosangos + 4 + kCalor + 8 + fm.height() * 2 + 4};
}

QSize GraficoCarga::minimumSizeHint() const { return sizeHint(); }

QRect GraficoCarga::coluna(int i) const {
    const int n = std::max<int>(1, static_cast<int>(semanas_.size()));
    const int x0 = kEixo + 8;
    const double larg = (width() - x0 - (n - 1) * kVao) / static_cast<double>(n);
    const int x = x0 + static_cast<int>(i * (larg + kVao));
    return {x, 0, static_cast<int>(larg), height()};
}

int GraficoCarga::colunaEm(QPoint p) const {
    for (int i = 0; i < static_cast<int>(semanas_.size()); ++i) {
        if (coluna(i).contains(p)) return i;
    }
    return -1;
}

int GraficoCarga::escala() const {
    int maior = 20 * 60;
    for (const auto& s : semanas_) maior = std::max({maior, s.livre, s.total(), s.feito});
    return ((maior + 599) / 600) * 600;   // múltiplo de 10h
}

void GraficoCarga::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(0, kTopo);
    const QFontMetrics fm(font());
    const int teto = escala();
    auto alt = [&](int minutos) { return static_cast<int>(std::lround(static_cast<double>(minutos) / teto * kAltura)); };
    const QColor linha = tema::token("line"), forte = tema::token("line-strong");
    const QColor texto2 = tema::token("text-2"), texto3 = tema::token("text-3");
    const QColor laranja = tema::token("warn"), verde = tema::token("ok");

    // --- eixo Y e grade ---
    QFont pequena = font();
    pequena.setPointSizeF(pequena.pointSizeF() * 0.82);
    p.setFont(pequena);
    const int passo = teto > 40 * 60 ? 20 * 60 : 10 * 60;
    for (int m = 0; m <= teto; m += passo) {
        const int y = kAltura - alt(m);
        p.setPen(QPen(m == 0 ? forte : linha, 1));
        p.drawLine(kEixo + 8, y, width(), y);
        p.setPen(texto3);
        p.drawText(QRect(0, y - fm.height() / 2 - 1, kEixo, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1h").arg(m / 60));
    }

    const int baseDias = kAltura + 4;
    for (int i = 0; i < static_cast<int>(semanas_.size()); ++i) {
        const SemanaCarga& s = semanas_[static_cast<size_t>(i)];
        const QRect col = coluna(i);
        const QRect area(col.left(), 0, col.width(), kAltura);
        const bool passada = s.inicio.addDays(6) < hoje_;
        const bool atual = hoje_ >= s.inicio && hoje_ <= s.inicio.addDays(6);
        const int total = s.total();

        // Fundo da coluna: crítica, sob o mouse; moldura da atual e da escolhida.
        if (s.critica()) {
            p.setPen(Qt::NoPen);
            p.setBrush(tema::token("warn-soft"));
            p.drawRoundedRect(area, 6, 6);
        } else if (i == sobMouse_) {
            p.setPen(Qt::NoPen);
            p.setBrush(tema::token("surface-2"));
            p.drawRoundedRect(area, 6, 6);
        }
        if (i == escolhida_) {
            p.setPen(QPen(tema::token("accent"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(area).adjusted(1, 1, -1, -1), 6, 6);
        } else if (atual) {
            p.setPen(QPen(forte, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        }
        if (s.critica()) {
            QFont f = pequena;
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(laranja);
            p.drawText(QRect(col.left(), 4, col.width(), fm.height()), Qt::AlignHCenter,
                       col.width() >= 60 ? QStringLiteral("! crítica") : QStringLiteral("!"));
        }

        // --- a barra ---
        const int bx = col.left() + static_cast<int>(col.width() * 0.22);
        const int bw = std::max(6, static_cast<int>(col.width() * 0.56));
        const int hPlan = std::min(kAltura, alt(total));
        QPainterPath forma;
        forma.addRoundedRect(QRectF(bx, kAltura - std::max(hPlan, alt(s.feito)), bw,
                                    std::max(hPlan, alt(s.feito)) + 4),
                             3, 3);
        if (passada) {
            // O que estava planejado, tracejado; o feito, cheio.
            if (hPlan > 0) {
                QPen tr(forte, 1.5, Qt::DashLine);
                p.setPen(tr);
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(bx, kAltura - hPlan, bw, hPlan));
            }
            const int hf = std::min(kAltura, alt(s.feito));
            if (hf > 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(comAlfa(verde, 0.8));
                p.drawRoundedRect(QRectF(bx, kAltura - hf, bw, hf + 3), 3, 3);
            }
        } else {
            p.save();
            p.setClipPath(forma);
            int y = kAltura;
            p.setPen(Qt::NoPen);
            int feito = 0;
            if (atual && s.feito > 0 && total > 0) {
                feito = std::min(s.feito, total);
                const int h = alt(feito);
                p.setBrush(verde);
                p.drawRect(QRectF(bx, y - h, bw, h));
                y -= h;
            }
            const double resto = total > 0 ? static_cast<double>(total - feito) / total : 0;
            for (size_t k = 0; k < s.porMateria.size() && k < materias_.size(); ++k) {
                const int m = s.porMateria[k];
                if (m <= 0) continue;
                const int h = std::max(2, static_cast<int>(std::lround(alt(m) * resto)));
                QColor c = materias_[k].cor;
                if (isolada_ >= 0 && isolada_ != static_cast<int>(k)) c = comAlfa(c, 0.2);
                p.setBrush(c);
                p.drawRect(QRectF(bx, y - h, bw, h));
                // Um fio da cor da superfície entre as matérias.
                p.setBrush(tema::token("surface"));
                p.drawRect(QRectF(bx, y - h, bw, 1));
                y -= h;
            }
            p.restore();

            // O que passa do tempo livre: hachurado, com "+3h" em cima.
            if (total > s.livre && s.livre >= 0) {
                const int capY = kAltura - alt(s.livre), planY = kAltura - hPlan;
                const QRectF ex(bx, planY, bw, capY - planY);
                p.setPen(Qt::NoPen);
                p.setBrush(QBrush(tema::token("surface"), Qt::BDiagPattern));
                p.drawRect(ex);
                p.setPen(QPen(laranja, 2));
                p.drawLine(QPointF(bx, planY + 1), QPointF(bx + bw, planY + 1));
                QFont f = pequena;
                f.setWeight(QFont::Bold);
                p.setFont(f);
                p.setPen(laranja);
                p.drawText(QRect(col.left() - 6, planY - fm.height() - 1, col.width() + 12, fm.height()),
                           Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral("+") + dur(total - s.livre));
            }
        }

        // O tempo livre: linha tracejada.
        if (s.livre > 0) {
            const int capY = kAltura - std::min(kAltura, alt(s.livre));
            QPen tr(texto2, 2, Qt::DashLine);
            p.setPen(tr);
            p.drawLine(QPointF(col.left() + col.width() * 0.1, capY), QPointF(col.right() - col.width() * 0.1, capY));
        }

        // --- os dias: provas e calor ---
        const double fatia = (col.width() - 4) / 7.0;
        for (int d = 0; d < 7; ++d) {
            const double cx = col.left() + 2 + fatia * (d + 0.5);
            int k = 0;
            for (const auto& pr : s.provas) {
                if (pr.dia != d) continue;
                QColor c = pr.materia >= 0 && pr.materia < static_cast<int>(materias_.size())
                               ? materias_[static_cast<size_t>(pr.materia)].cor
                               : texto2;
                if (isolada_ >= 0 && isolada_ != pr.materia) c = comAlfa(c, 0.25);
                losango(p, QPointF(cx, baseDias + 6 + k * 11), 7, c, pr.deduzida);
                ++k;
            }
            const int m = s.dia[static_cast<size_t>(d)];
            const int nivel = m <= 0 ? 0 : m < 60 ? 1 : m < 120 ? 2 : m < 240 ? 3 : 4;
            static const double kOpac[] = {1, 0.28, 0.5, 0.75, 1};
            const QDate data = s.inicio.addDays(d);
            QColor c = nivel ? comAlfa(tema::token("accent-fill"), kOpac[nivel]) : tema::token("surface-3");
            if (data < hoje_) c = comAlfa(c, 0.45);
            const QRectF r(col.left() + 2 + fatia * d + 1, baseDias + kLosangos + 4, fatia - 2, kCalor);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(r, 2, 2);
            // Hoje: um ponto embaixo da fatia. Um contorno numa fatia de 5 px
            // parecia um "0".
            if (data == hoje_) {
                p.setBrush(tema::token("text"));
                p.drawEllipse(QPointF(r.center().x(), r.bottom() + 3.5), 2, 2);
            }
        }

        // --- rótulos ---
        int ty = baseDias + kLosangos + 4 + kCalor + 8;
        QFont rot = font();
        rot.setWeight(atual || i == escolhida_ ? QFont::Bold : QFont::DemiBold);
        rot.setPointSizeF(rot.pointSizeF() * 0.9);
        p.setFont(rot);
        p.setPen(atual ? tema::token("accent") : i == escolhida_ ? tema::token("text") : texto2);
        p.drawText(QRect(col.left() - 4, ty, col.width() + 8, fm.height()), Qt::AlignHCenter, ddmm(s.inicio));
        ty += fm.height();
        if (atual) {
            QFont f = pequena;
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(tema::token("accent"));
            p.drawText(QRect(col.left() - 8, ty - 2, col.width() + 16, fm.height()), Qt::AlignHCenter,
                       col.width() >= 70 ? QStringLiteral("esta semana") : QStringLiteral("atual"));
        } else if (s.viaAgente > 0 && col.width() >= 70) {
            QFont f = pequena;
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(verde);
            p.drawText(QRect(col.left() - 8, ty - 2, col.width() + 16, fm.height()), Qt::AlignHCenter,
                       QStringLiteral("✓ via agente"));
        }
        p.setFont(font());
    }
}

QString GraficoCarga::dica(int i) const {
    const SemanaCarga& s = semanas_[static_cast<size_t>(i)];
    const bool passada = s.inicio.addDays(6) < hoje_;
    const bool atual = hoje_ >= s.inicio && hoje_ <= s.inicio.addDays(6);
    const int total = s.total();
    QString h = QStringLiteral("<b>%1 – %2%3</b>")
                    .arg(ddmm(s.inicio), ddmm(s.inicio.addDays(6)),
                         atual ? QStringLiteral(" · esta semana") : passada ? QStringLiteral(" · já passou") : QString());
    if (s.critica()) {
        QStringList porque;
        if (s.livre > 0 && total >= 0.8 * s.livre) porque << QStringLiteral("%1% do tempo livre").arg(100 * total / s.livre);
        if (s.provas.size() >= 3) porque << QStringLiteral("%1 provas").arg(s.provas.size());
        h += QStringLiteral("<br><span style='color:%1'><b>! Semana crítica · %2</b></span>")
                 .arg(tema::token("warn").name(), porque.join(QStringLiteral(" · ")));
    }
    h += QStringLiteral("<br>Livre %1 · planejado %2%3")
             .arg(dur(s.livre), dur(total),
                  s.livre > 0 ? QStringLiteral(" (%1%)").arg(100 * total / s.livre) : QString());
    if (total > s.livre && !passada) {
        h += QStringLiteral("<br><span style='color:%1'>Plano %2 acima do tempo livre</span>")
                 .arg(tema::token("warn").name(), dur(total - s.livre));
    }
    if ((passada || atual) && s.feito > 0) {
        h += QStringLiteral("<br><span style='color:%1'>✓ Feito %2%3</span>")
                 .arg(tema::token("ok").name(), dur(s.feito),
                      s.viaAgente ? QStringLiteral(" (%1 via agente)").arg(dur(s.viaAgente)) : QString());
    }
    QString linhas;
    for (size_t k = 0; k < s.porMateria.size() && k < materias_.size(); ++k) {
        if (s.porMateria[k] <= 0) continue;
        linhas += QStringLiteral("<br><span style='color:%1'>■</span> %2 — %3")
                      .arg(materias_[k].cor.name(), materias_[k].nome.toHtmlEscaped(), dur(s.porMateria[k]));
    }
    if (!linhas.isEmpty()) h += QStringLiteral("<hr>") + linhas.mid(4);
    if (!s.provas.empty()) {
        h += QStringLiteral("<hr>");
        QStringList ps;
        for (const auto& pr : s.provas) {
            const QColor c = pr.materia >= 0 && pr.materia < static_cast<int>(materias_.size())
                                 ? materias_[static_cast<size_t>(pr.materia)].cor
                                 : tema::token("text-2");
            ps << QStringLiteral("<span style='color:%1'>%2</span> %3%4")
                      .arg(c.name(), pr.deduzida ? QStringLiteral("◇") : QStringLiteral("◆"), pr.texto.toHtmlEscaped(),
                           pr.deduzida ? QStringLiteral(" · deduzida") : QString());
        }
        h += ps.join(QStringLiteral("<br>"));
    }
    return h;
}

bool GraficoCarga::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int i = colunaEm(he->pos());
        if (i >= 0) QToolTip::showText(he->globalPos(), dica(i), this, coluna(i));
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void GraficoCarga::mouseMoveEvent(QMouseEvent* e) {
    const int i = colunaEm(e->position().toPoint());
    if (i != sobMouse_) {
        sobMouse_ = i;
        update();
    }
}

void GraficoCarga::mousePressEvent(QMouseEvent* e) {
    const int i = colunaEm(e->position().toPoint());
    if (i < 0) return;
    escolhida_ = escolhida_ == i ? -1 : i;
    update();
    if (aoEscolher) aoEscolher(escolhida_);
}

void GraficoCarga::leaveEvent(QEvent*) {
    sobMouse_ = -1;
    update();
}

} // namespace sigaa::ui
