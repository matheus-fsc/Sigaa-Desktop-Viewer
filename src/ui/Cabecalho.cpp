#include "ui/Cabecalho.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTabWidget>

#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

// Medidas do protótipo, em px: padding 0 12 na aba, 8 entre rótulo e pílula,
// pílula de 20 de altura com 7 de respiro, sublinhado de 2.
constexpr int kPadAba = 12;
constexpr int kVaoPilula = 8;
constexpr int kAltPilula = 20;
constexpr int kPadPilula = 7;
constexpr int kSublinhado = 2;

} // namespace

QIcon avatarConta(const QString& login, qreal dpr) {
    // Duas primeiras LETRAS do login. É o que há: o app não coleta o nome do
    // aluno, e um login como "marina.alves" já dá "MA". Matrícula numérica não
    // tem inicial nenhuma — melhor o desenho de pessoa que "20".
    QString iniciais;
    for (const QChar c : login) {
        if (!c.isLetter()) break;
        iniciais += c.toUpper();
        if (iniciais.size() == 2) break;
    }
    if (iniciais.isEmpty()) return {};

    constexpr int kLado = 26;
    QPixmap pm(QSize(kLado, kLado) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(tema::token("accent-soft"));
    p.drawEllipse(QRectF(0, 0, kLado, kLado));

    QFont f = tema::fonte(tema::Papel::Corpo);
    f.setPixelSize(12);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(tema::token("accent"));
    p.drawText(QRect(0, 0, kLado, kLado), Qt::AlignCenter, iniciais);
    return QIcon(pm);
}

// ---------------------------------------------------------------------------

BotaoAba::BotaoAba(QWidget* pai) : QAbstractButton(pai) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    // A aba ocupa a altura inteira da barra: é na borda de baixo DELA que o
    // sublinhado precisa encostar, rente ao filete do cabeçalho.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAttribute(Qt::WA_Hover);
}

void BotaoAba::definirTitulo(const QString& titulo) {
    static const QRegularExpression re(QStringLiteral("^(.*\\S)\\s*\\(([^()]*)\\)\\s*$"));
    const auto m = re.match(titulo);
    if (m.hasMatch()) {
        rotulo_ = m.captured(1);
        contagem_ = m.captured(2);
    } else {
        rotulo_ = titulo;
        contagem_.clear();
    }
    setText(titulo);          // leitor de tela e dica leem o título inteiro
    setAccessibleName(titulo);
    updateGeometry();
    update();
}

QFont BotaoAba::fonteRotulo() const {
    // 14,5/600 inativa, 700 ativa. Largura medida SEMPRE em negrito: se a aba
    // encolhesse ao sair do ativo, as vizinhas pulariam a cada clique.
    QFont f = tema::fonte(tema::Papel::Corpo);
    f.setPointSizeF(f.pointSizeF() * 1.04);
    f.setWeight(isChecked() ? QFont::Bold : QFont::DemiBold);
    return f;
}

QFont BotaoAba::fontePilula() const {
    QFont f = tema::fonte(tema::Papel::Legenda);
    f.setWeight(QFont::Bold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    f.setFeature("tnum", 1);
#endif
    return f;
}

int BotaoAba::larguraPilula() const {
    if (contagem_.isEmpty()) return 0;
    const int texto = QFontMetrics(fontePilula()).horizontalAdvance(contagem_);
    return qMax(kAltPilula, texto + 2 * kPadPilula);
}

QSize BotaoAba::sizeHint() const {
    QFont negrito = fonteRotulo();
    negrito.setWeight(QFont::Bold);
    const QFontMetrics fm(negrito);
    int w = 2 * kPadAba + fm.horizontalAdvance(rotulo_);
    if (!contagem_.isEmpty()) w += kVaoPilula + larguraPilula();
    return {w, qMax(fm.height(), kAltPilula) + 2 * tema::esp(2)};
}

void BotaoAba::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool ativa = isChecked();
    const bool sob = underMouse();

    // Rótulo
    const QFont fr = fonteRotulo();
    const QFontMetrics fm(fr);
    const int larguraRotulo = fm.horizontalAdvance(rotulo_);
    int x = kPadAba;
    p.setFont(fr);
    p.setPen(tema::token(ativa || sob ? "text" : "text-2"));
    p.drawText(QRect(x, 0, larguraRotulo + 2, height()), Qt::AlignVCenter | Qt::AlignLeft,
               rotulo_);
    x += larguraRotulo + kVaoPilula;

    // Pílula da contagem: accent-soft na aba ativa, surface-3 nas outras.
    if (!contagem_.isEmpty()) {
        const QRectF pilula(x, (height() - kAltPilula) / 2.0, larguraPilula(), kAltPilula);
        p.setPen(Qt::NoPen);
        p.setBrush(tema::token(ativa ? "accent-soft" : "surface-3"));
        p.drawRoundedRect(pilula, kAltPilula / 2.0, kAltPilula / 2.0);
        p.setFont(fontePilula());
        p.setPen(tema::token(ativa ? "accent" : "text-2"));
        p.drawText(pilula, Qt::AlignCenter, contagem_);
    }

    // Sublinhado rente à base, na largura inteira da aba.
    if (ativa) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(0, height() - kSublinhado, width(), kSublinhado),
                   tema::token("accent"));
    }

    // Foco de teclado: o mesmo anel de @focus dos outros controles.
    if (hasFocus()) {
        QPen caneta(tema::token("focus"));
        caneta.setWidth(2);
        p.setPen(caneta);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 6, -2, -6), 6, 6);
    }
}

// ---------------------------------------------------------------------------

NavegacaoAbas::NavegacaoAbas(QTabWidget* abas, QWidget* pai)
    : QWidget(pai), abas_(abas) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* linha = new QHBoxLayout(this);
    linha->setContentsMargins(0, 0, 0, 0);
    linha->setSpacing(2);

    auto* grupo = new QButtonGroup(this);
    grupo->setExclusive(true);
    for (int i = 0; i < abas->count(); ++i) {
        auto* b = new BotaoAba(this);
        linha->addWidget(b);
        grupo->addButton(b, i);
        botoes_.push_back(b);
    }

    connect(grupo, &QButtonGroup::idClicked, abas, &QTabWidget::setCurrentIndex);
    connect(abas, &QTabWidget::currentChanged, this, [this](int i) {
        for (int k = 0; k < static_cast<int>(botoes_.size()); ++k) {
            botoes_[k]->setChecked(k == i);
        }
    });

    sincronizar();
}

void NavegacaoAbas::sincronizar() {
    for (int i = 0; i < static_cast<int>(botoes_.size()); ++i) {
        botoes_[i]->definirTitulo(abas_->tabText(i));
        botoes_[i]->setChecked(i == abas_->currentIndex());
    }
    updateGeometry();
}

} // namespace sigaa::ui
