#include "ui/PainelNoticias.h"

#include <QAbstractTextDocumentLayout>
#include <QDate>

#include <algorithm>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QTextBrowser>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

// Teto da área de texto: acima disso a notícia rola dentro do cartão.
constexpr int kAlturaMaxTexto = 170;

QString quando(const DateTime& d) {
    if (!d.valid()) return {};
    QString s = QDate(d.year, d.month, d.day).toString(QStringLiteral("dd/MM/yyyy"));
    if (d.hasTime) s += QTime(d.hour, d.minute).toString(QStringLiteral(" HH:mm"));
    return s;
}

} // namespace

PainelNoticias::PainelNoticias(QWidget* pai) : QFrame(pai) {
    setObjectName(QStringLiteral("painelNoticias"));
    setProperty("classe", QStringLiteral("cartao"));
    // Só a altura do conteúdo: a sobra da aba é da árvore de aulas. Sem isto
    // o layout dividia a altura com a árvore e abria vãos entre título e data.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
    raiz->setSpacing(tema::esp(1));

    // Cabeçalho: rótulo de seção, posição e as setas.
    auto* cab = new QHBoxLayout;
    cab->setSpacing(tema::esp(1));
    auto* secao = new QLabel(QStringLiteral("Notícias"), this);
    secao->setProperty("classe", QStringLiteral("secao"));
    QFont fs = tema::fonte(tema::Papel::Legenda);
    fs.setWeight(QFont::Bold);
    fs.setCapitalization(QFont::AllUppercase);
    fs.setLetterSpacing(QFont::PercentageSpacing, 107);
    secao->setFont(fs);
    posicao_ = new QLabel(this);
    posicao_->setProperty("classe", QStringLiteral("nota"));
    posicao_->setFont(tema::fonte(tema::Papel::Legenda));
    anterior_ = new QToolButton(this);
    seguinte_ = new QToolButton(this);
    anterior_->setText(QStringLiteral("‹"));
    seguinte_->setText(QStringLiteral("›"));
    anterior_->setToolTip(QStringLiteral("Notícia anterior (mais antiga)"));
    seguinte_->setToolTip(QStringLiteral("Próxima notícia (mais nova)"));
    for (QToolButton* b : {anterior_, seguinte_}) {
        b->setObjectName(QStringLiteral("setaCarga"));   // mesmo estilo das setas do painel de provas
        b->setFixedSize(26, 26);
        b->setCursor(Qt::PointingHandCursor);
    }
    cab->addWidget(secao);
    cab->addSpacing(tema::esp(2));
    cab->addWidget(posicao_);
    cab->addStretch(1);
    cab->addWidget(anterior_);
    cab->addWidget(seguinte_);
    raiz->addLayout(cab);

    titulo_ = new QLabel(this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo_->setFont(ft);
    titulo_->setWordWrap(true);
    meta_ = new QLabel(this);
    meta_->setProperty("classe", QStringLiteral("nota"));
    meta_->setFont(tema::fonte(tema::Papel::Legenda));
    for (QLabel* l : {titulo_, meta_}) l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    raiz->addWidget(titulo_);
    raiz->addWidget(meta_);

    // QTextBrowser e não QLabel: o texto do professor pode ser longo, e o
    // navegador rola sozinho sem empurrar a árvore de aulas para fora da
    // janela. Links abrem no navegador do sistema.
    texto_ = new QTextBrowser(this);
    texto_->setObjectName(QStringLiteral("textoNoticia"));
    texto_->setOpenExternalLinks(true);
    texto_->setFrameShape(QFrame::NoFrame);
    texto_->document()->setDocumentMargin(0);
    raiz->addWidget(texto_);
    // A altura acompanha o texto, até um teto: notícia de uma linha não
    // reserva a caixa de uma de dez, e a longa rola dentro do cartão em vez de
    // empurrar a árvore de aulas para fora da janela. O layout do documento
    // recalcula a cada mudança de largura, e o sinal vem junto.
    connect(texto_->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF& tam) {
                texto_->setFixedHeight(std::clamp(static_cast<int>(tam.height()) + 4, 24,
                                                  kAlturaMaxTexto));
            });

    // Mais antiga = índice maior (a lista vem da mais nova para a mais antiga).
    connect(anterior_, &QToolButton::clicked, this, [this] {
        if (atual_ + 1 < static_cast<int>(noticias_.size())) {
            ++atual_;
            mostrar();
        }
    });
    connect(seguinte_, &QToolButton::clicked, this, [this] {
        if (atual_ > 0) {
            --atual_;
            mostrar();
        }
    });

    mostrar();
}

void PainelNoticias::definir(std::vector<Noticia> noticias, bool coletado) {
    noticias_ = std::move(noticias);
    coletado_ = coletado || !noticias_.empty();
    atual_ = 0;
    mostrar();
}

void PainelNoticias::mostrar() {
    const int n = static_cast<int>(noticias_.size());
    anterior_->setEnabled(atual_ + 1 < n);
    seguinte_->setEnabled(atual_ > 0);
    anterior_->setVisible(n > 1);
    seguinte_->setVisible(n > 1);

    if (n == 0) {
        // "Não sei" ≠ "zero": sem coleta, dizer "nenhuma notícia" seria afirmar
        // o que o app nunca foi conferir.
        posicao_->clear();
        titulo_->setText(coletado_ ? QStringLiteral("Nenhuma notícia nesta turma")
                                   : QStringLiteral("Notícias ainda não coletadas"));
        meta_->setText(coletado_ ? QString()
                                 : QStringLiteral("Use Atualizar para buscar as notícias do "
                                                  "professor."));
        meta_->setVisible(!meta_->text().isEmpty());
        texto_->hide();
        return;
    }

    const Noticia& atual = noticias_[static_cast<std::size_t>(atual_)];
    // Posição cronológica: a mais antiga é a 1, e "12 de 12" é a mais nova —
    // a mesma direção das setas (‹ volta no tempo).
    posicao_->setText(QStringLiteral("%1 de %2").arg(n - atual_).arg(n));
    titulo_->setText(QString::fromStdString(atual.titulo));
    QString meta = quando(atual.data);
    if (!atual.autor.empty()) meta += QStringLiteral(" · ") + QString::fromStdString(atual.autor);
    meta_->setText(meta);
    meta_->setVisible(!meta.isEmpty());

    texto_->show();
    if (atual.conteudoHtml.empty()) {
        // A lista já mostrou que ela existe; o texto custa uma consulta a mais
        // e vem aos poucos (até cinco por turma a cada atualização).
        texto_->setHtml(QStringLiteral("<p style=\"color:%1\">Texto ainda não buscado — vem "
                                       "numa próxima atualização.</p>")
                            .arg(tema::token("text-3").name()));
    } else {
        texto_->setHtml(QString::fromStdString(atual.conteudoHtml));
    }
    // Sempre do começo. A altura da caixa se ajusta ao texto DEPOIS do
    // setHtml (sinal do layout do documento), e o reajuste deixava a rolagem
    // no fim — a notícia abria mostrando a assinatura em vez do começo.
    QTimer::singleShot(0, texto_, [t = texto_] { t->verticalScrollBar()->setValue(0); });
}

} // namespace sigaa::ui
