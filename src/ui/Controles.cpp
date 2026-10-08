#include "ui/Controles.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

#include <algorithm>

#include "ui/Tema.h"

namespace sigaa::ui {

// ---------------------------------------------------------------------------

Interruptor::Interruptor(QWidget* pai) : QAbstractButton(pai) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize Interruptor::sizeHint() const { return {44, 26}; }

void Interruptor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    const QRectF trilho = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(hasFocus() ? QPen(tema::token("focus"), 2) : Qt::NoPen);
    p.setBrush(isChecked() ? tema::token("accent-fill") : tema::token("line-strong"));
    p.drawRoundedRect(trilho, trilho.height() / 2, trilho.height() / 2);
    const qreal d = trilho.height() - 6;
    const qreal x = isChecked() ? trilho.right() - 3 - d : trilho.left() + 3;
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? tema::token("on-accent") : tema::token("surface"));
    p.drawEllipse(QRectF(x, trilho.top() + 3, d, d));
}

// ---------------------------------------------------------------------------

Segmentado::Segmentado(const QStringList& rotulos, QWidget* pai) : QWidget(pai) {
    setObjectName(QStringLiteral("segmentado"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(1, 1, 1, 1);
    h->setSpacing(0);
    for (int i = 0; i < rotulos.size(); ++i) {
        auto* b = new QPushButton(rotulos[i], this);
        b->setCheckable(true);
        b->setAutoDefault(false);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("pos", i == 0                    ? QStringLiteral("primeiro")
                              : i == rotulos.size() - 1 ? QStringLiteral("ultimo")
                                                        : QStringLiteral("meio"));
        connect(b, &QPushButton::clicked, this, [this, i] {
            const bool mudou = i != indice_;
            definir(i);
            if (mudou && aoMudar) aoMudar(i);
        });
        h->addWidget(b);
        botoes_.push_back(b);
    }
}

void Segmentado::definir(int i) {
    indice_ = i;
    for (int k = 0; k < static_cast<int>(botoes_.size()); ++k) {
        botoes_[static_cast<size_t>(k)]->setChecked(k == i);
        // O negrito do marcado em C++: propriedade de fonte no QSS remonta a
        // fonte e perde o tamanho do papel.
        QFont f = botoes_[static_cast<size_t>(k)]->font();
        f.setWeight(k == i ? QFont::Bold : QFont::DemiBold);
        botoes_[static_cast<size_t>(k)]->setFont(f);
    }
}

// ---------------------------------------------------------------------------

Passo::Passo(int minimo, int maximo, int passo, std::function<QString(int)> formato, QWidget* pai)
    : QWidget(pai), minimo_(minimo), maximo_(maximo), passo_(passo), valor_(minimo),
      formato_(std::move(formato)) {
    setObjectName(QStringLiteral("passo"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(1, 1, 1, 1);
    h->setSpacing(0);
    menos_ = new QPushButton(QStringLiteral("−"), this);
    mais_ = new QPushButton(QStringLiteral("+"), this);
    rotulo_ = new QLabel(this);
    rotulo_->setAlignment(Qt::AlignCenter);
    QFont fr = tema::fonte(tema::Papel::Numero);
    fr.setWeight(QFont::DemiBold);
    rotulo_->setFont(fr);
    rotulo_->setMinimumWidth(QFontMetrics(fr).horizontalAdvance(QStringLiteral("00,0 h")) + 16);
    for (QPushButton* b : {menos_, mais_}) {
        b->setAutoDefault(false);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(34, 32);
        QFont f = b->font();
        f.setPointSizeF(f.pointSizeF() * 1.25);
        b->setFont(f);
    }
    menos_->setAccessibleName(QStringLiteral("Menos"));
    mais_->setAccessibleName(QStringLiteral("Mais"));
    h->addWidget(menos_);
    h->addWidget(rotulo_);
    h->addWidget(mais_);
    auto mudar = [this](int delta) {
        const int v = std::clamp(valor_ + delta, minimo_, maximo_);
        if (v == valor_) return;
        valor_ = v;
        mostrar();
        if (aoMudar) aoMudar(v);
    };
    connect(menos_, &QPushButton::clicked, this, [mudar, this] { mudar(-passo_); });
    connect(mais_, &QPushButton::clicked, this, [mudar, this] { mudar(passo_); });
    mostrar();
}

void Passo::definir(int v) {
    valor_ = std::clamp(v, minimo_, maximo_);
    mostrar();
}

void Passo::definirNome(const QString& nome) {
    setAccessibleName(nome);
    menos_->setAccessibleName(nome + QStringLiteral(": menos"));
    mais_->setAccessibleName(nome + QStringLiteral(": mais"));
}

void Passo::mostrar() {
    rotulo_->setText(formato_ ? formato_(valor_) : QString::number(valor_));
    menos_->setEnabled(valor_ > minimo_);
    mais_->setEnabled(valor_ < maximo_);
}

// ---------------------------------------------------------------------------

QPushButton* botaoLink(const QString& texto, QWidget* pai) {
    auto* b = new QPushButton(texto, pai);
    b->setProperty("papel", QStringLiteral("link"));
    b->setAutoDefault(false);
    b->setCursor(Qt::PointingHandCursor);
    QFont f = b->font();
    f.setWeight(QFont::DemiBold);
    b->setFont(f);
    return b;
}

QLabel* pilula(const QString& texto, const char* tom, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setProperty("classe", QStringLiteral("pilula"));
    l->setProperty("tom", QString::fromLatin1(tom));
    QFont f = tema::fonte(tema::Papel::Legenda);
    f.setWeight(QFont::Bold);
    l->setFont(f);
    l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return l;
}

} // namespace sigaa::ui
