#include "ui/Planejamento.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "core/store/Database.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

using planejamento::Sessao;
using planejamento::SemanaPlano;

constexpr int kColuna = 104;      // largura de uma semana no mapa
constexpr int kAltBarras = 150;   // altura da área das barras

QDate paraQ(const DateTime& d) { return d.valid() ? QDate(d.year, d.month, d.day) : QDate(); }

QString q(const std::string& s) { return QString::fromStdString(s); }

QString nomeMes(QDate d) {
    QString m = QLocale(QLocale::Portuguese, QLocale::Brazil).toString(d, QStringLiteral("MMM"));
    m.remove(QLatin1Char('.'));
    return m.toLower();
}

QString intervalo(QDate ini) {
    const QDate fim = ini.addDays(6);
    if (ini.month() == fim.month()) {
        return QStringLiteral("%1–%2 %3").arg(ini.day()).arg(fim.day()).arg(nomeMes(fim));
    }
    return QStringLiteral("%1 %2 – %3 %4")
        .arg(ini.day())
        .arg(nomeMes(ini))
        .arg(fim.day())
        .arg(nomeMes(fim));
}

// "qui 02/10 · hoje"
QString cabecalhoDia(QDate d) {
    QString s = QLocale(QLocale::Portuguese, QLocale::Brazil)
                    .toString(d, QStringLiteral("ddd dd/MM"))
                    .remove(QLatin1Char('.'));
    const qint64 n = QDate::currentDate().daysTo(d);
    if (n == 0) s += QStringLiteral(" · hoje");
    else if (n == 1) s += QStringLiteral(" · amanhã");
    return s;
}

QString horas(int minutos) { return q(planejamento::duracao(minutos)); }

QLabel* rotulo(const QString& texto, tema::Papel papel, bool negrito, const char* classe,
               QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    QFont f = tema::fonte(papel);
    if (negrito) f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setWordWrap(true);
    if (classe) l->setProperty("classe", QString::fromLatin1(classe));
    return l;
}

// Esvazia um layout de linhas. `deleteLater`, e não `delete`: quem chama pode
// estar dentro do clique de um widget desta mesma lista.
void esvaziar(QVBoxLayout* area) {
    while (QLayoutItem* it = area->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Mapa de pressão
// ---------------------------------------------------------------------------

GraficoPressao::GraficoPressao(QWidget* pai) : QWidget(pai) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GraficoPressao::definir(std::vector<SemanaPlano> semanas, std::vector<Sessao> sessoes) {
    semanas_ = std::move(semanas);
    feitosPorSemana_.assign(semanas_.size(), 0);
    for (const auto& s : sessoes) {
        if (!s.feita) continue;
        const QDate d = paraQ(s.dia);
        for (size_t i = 0; i < semanas_.size(); ++i) {
            const QDate ini = paraQ(semanas_[i].inicio);
            if (d >= ini && d < ini.addDays(7)) feitosPorSemana_[i] += s.minutos;
        }
    }
    QStringList partes;
    for (const auto& w : semanas_) {
        partes << QStringLiteral("semana de %1: %2 planejadas de %3 livres, %4 provas%5")
                      .arg(intervalo(paraQ(w.inicio)), horas(w.minutosPlanejados),
                           horas(w.minutosLivres))
                      .arg(w.provas)
                      .arg(w.critica() ? QStringLiteral(", crítica") : QString());
    }
    setAccessibleDescription(partes.join(QStringLiteral("; ")));
    updateGeometry();
    update();
}

QSize GraficoPressao::sizeHint() const {
    const QFontMetrics fm(font());
    return {std::max<int>(6, static_cast<int>(semanas_.size())) * kColuna,
            kAltBarras + 3 * fm.height() + tema::esp(6)};
}

QSize GraficoPressao::minimumSizeHint() const { return sizeHint(); }

void GraficoPressao::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm(font());

    // Escala comum a todas as colunas: o maior entre o tempo livre e o
    // planejado de qualquer semana. Uma escala por coluna faria toda barra
    // parecer cheia.
    int teto = 60;
    for (const auto& w : semanas_) teto = std::max({teto, w.minutosLivres, w.minutosPlanejados});

    const int topo = fm.height() + tema::esp(2);   // espaço da faixa "crítica"
    const int base = topo + kAltBarras;
    const QDate hoje = QDate::currentDate();
    const QColor laranja = tema::cor::urgente();

    for (size_t i = 0; i < semanas_.size(); ++i) {
        const SemanaPlano& w = semanas_[i];
        const QRect col(static_cast<int>(i) * kColuna, 0, kColuna, height());
        const int larg = 44;
        const int x = col.center().x() - larg / 2;
        const QDate ini = paraQ(w.inicio);
        const bool estaSemana = hoje >= ini && hoje < ini.addDays(7);
        auto alturaDe = [&](int minutos) {
            return static_cast<int>(static_cast<double>(minutos) / teto * kAltBarras);
        };

        // Zona crítica: fundo laranja bem leve na coluna inteira, e o rótulo
        // no alto. É a primeira coisa que o olho tem de achar no mapa.
        if (w.critica()) {
            QColor fundo = laranja;
            fundo.setAlphaF(0.10);
            p.setPen(Qt::NoPen);
            p.setBrush(fundo);
            p.drawRoundedRect(QRectF(col).adjusted(4, 0, -4, 0), 8, 8);
            QFont f = font();
            f.setWeight(QFont::Bold);
            f.setPointSizeF(f.pointSizeF() * 0.82);
            p.setFont(f);
            p.setPen(laranja);
            p.drawText(QRect(col.left(), tema::esp(1), col.width(), fm.height()),
                       Qt::AlignHCenter, QStringLiteral("CRÍTICA"));
        }

        // Tempo livre: o contorno da barra. O planejado enche por dentro.
        const int hLivre = std::max(2, alturaDe(w.minutosLivres));
        QPen contorno(tema::token("line-strong"));
        contorno.setWidthF(1.2);
        p.setPen(contorno);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(x, base - hLivre, larg, hLivre).adjusted(0.6, 0.6, -0.6, -0.6), 4,
                          4);

        const int feitos = feitosPorSemana_.empty() ? 0 : feitosPorSemana_[i];
        const int hPlan = alturaDe(w.minutosPlanejados);
        const int hFeito = std::min(hPlan, alturaDe(feitos));
        p.setPen(Qt::NoPen);
        if (hPlan > 0) {
            QColor pendente = w.critica() ? laranja : tema::token("accent");
            pendente.setAlphaF(0.55);
            p.setBrush(pendente);
            p.drawRoundedRect(QRectF(x + 3, base - hPlan, larg - 6, hPlan), 3, 3);
        }
        if (hFeito > 0) {
            p.setBrush(tema::cor::sucesso());
            p.drawRoundedRect(QRectF(x + 3, base - hFeito, larg - 6, hFeito), 3, 3);
        }

        // Uma bolinha laranja por prova, logo acima da barra.
        const int hTopo = std::max(hLivre, hPlan);
        for (int k = 0; k < std::min(w.provas, 6); ++k) {
            p.setBrush(laranja);
            p.drawEllipse(QPointF(col.center().x() - (std::min(w.provas, 6) - 1) * 5.0 + k * 10.0,
                                  base - hTopo - 8),
                          3.5, 3.5);
        }

        // Rótulos: o intervalo e "planejado / livre".
        int ty = base + tema::esp(2);
        QFont negrito = font();
        negrito.setWeight(QFont::Bold);
        p.setFont(negrito);
        p.setPen(tema::token(estaSemana ? "accent" : "text-2"));
        p.drawText(QRect(col.left(), ty, col.width(), fm.height()), Qt::AlignHCenter,
                   intervalo(ini));
        ty += fm.height() + 2;
        QFont pequena = font();
        pequena.setPointSizeF(pequena.pointSizeF() * 0.88);
        p.setFont(pequena);
        p.setPen(tema::token("text-3"));
        p.drawText(QRect(col.left(), ty, col.width(), fm.height()), Qt::AlignHCenter,
                   QStringLiteral("%1 / %2").arg(horas(w.minutosPlanejados), horas(w.minutosLivres)));
    }
}

// ---------------------------------------------------------------------------
// Horas e dificuldade
// ---------------------------------------------------------------------------

DialogoPreferenciasEstudo::DialogoPreferenciasEstudo(
    const planejamento::Preferencias& atual,
    const std::vector<std::pair<std::string, std::string>>& turmas, QWidget* pai)
    : QDialog(pai), prefs_(atual) {
    setWindowTitle(QStringLiteral("Horas e dificuldade"));
    setModal(true);
    // Largo o bastante para o nome da matéria caber em uma ou duas linhas.
    setMinimumWidth(560);

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));
    raiz->setSpacing(tema::esp(3));

    auto* horasBox = new QGroupBox(QStringLiteral("Horas livres para estudar"), this);
    auto* fh = new QFormLayout(horasBox);
    static const char* kDias[] = {"Segunda", "Terça", "Quarta", "Quinta",
                                  "Sexta",   "Sábado", "Domingo"};
    std::vector<QDoubleSpinBox*> campos;
    for (int d = 0; d < 7; ++d) {
        auto* c = new QDoubleSpinBox(horasBox);
        c->setRange(0, 12);
        c->setSingleStep(0.5);
        c->setDecimals(1);
        c->setSuffix(QStringLiteral(" h"));
        c->setValue(atual.minutosPorDia[static_cast<size_t>(d)] / 60.0);
        fh->addRow(QString::fromUtf8(kDias[d]), c);
        campos.push_back(c);
    }
    raiz->addWidget(horasBox);

    auto* difBox = new QGroupBox(QStringLiteral("Dificuldade de cada matéria"), this);
    auto* fd = new QFormLayout(difBox);
    auto* explica = rotulo(QStringLiteral("Matéria difícil recebe 50% mais tempo; fácil, 30% "
                                          "menos."),
                           tema::Papel::Legenda, false, "nota", difBox);
    fd->addRow(explica);
    std::vector<std::pair<std::string, QComboBox*>> combos;
    for (const auto& [id, nome] : turmas) {
        auto* c = new QComboBox(difBox);
        c->addItem(QStringLiteral("Fácil"), 1);
        c->addItem(QStringLiteral("Média"), 2);
        c->addItem(QStringLiteral("Difícil"), 3);
        c->setCurrentIndex(static_cast<int>(atual.dificuldadeDe(id)) - 1);
        auto* nomeL = new QLabel(q(nome), difBox);
        nomeL->setWordWrap(true);
        fd->addRow(nomeL, c);
        combos.emplace_back(id, c);
    }
    raiz->addWidget(difBox);

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    botoes->button(QDialogButtonBox::Save)->setText(QStringLiteral("Salvar"));
    botoes->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(botoes, &QDialogButtonBox::accepted, this, [this, campos, combos] {
        for (int d = 0; d < 7; ++d) {
            prefs_.minutosPorDia[static_cast<size_t>(d)] =
                static_cast<int>(campos[static_cast<size_t>(d)]->value() * 60);
        }
        prefs_.dificuldade.clear();
        for (const auto& [id, c] : combos) {
            const auto dif = static_cast<planejamento::Dificuldade>(c->currentData().toInt());
            // Média é o padrão: só guarda o que difere dele.
            if (dif != planejamento::Dificuldade::Media) prefs_.dificuldade[id] = dif;
        }
        accept();
    });
    raiz->addWidget(botoes);
}

// ---------------------------------------------------------------------------
// A janela
// ---------------------------------------------------------------------------

DialogoPlanejamento::DialogoPlanejamento(Entradas e, QWidget* pai)
    : QDialog(pai), entradas_(std::move(e)) {
    setWindowTitle(QStringLiteral("Planejamento de estudo"));
    setModal(true);

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));
    raiz->setSpacing(tema::esp(3));

    // Título, resumo e o botão das preferências na mesma faixa.
    auto* topo = new QHBoxLayout;
    auto* textos = new QVBoxLayout;
    textos->setSpacing(2);
    auto* titulo = new QLabel(QStringLiteral("Planejamento de estudo"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    textos->addWidget(titulo);
    resumo_ = rotulo(QString(), tema::Papel::Corpo, false, "nota", this);
    textos->addWidget(resumo_);
    topo->addLayout(textos, 1);
    auto* prefs = new QPushButton(QStringLiteral("Horas e dificuldade…"), this);
    prefs->setProperty("papel", QStringLiteral("secundario"));
    prefs->setAutoDefault(false);
    connect(prefs, &QPushButton::clicked, this, &DialogoPlanejamento::abrirPreferencias);
    topo->addWidget(prefs, 0, Qt::AlignTop);
    raiz->addLayout(topo);

    // --- mapa de pressão ---
    auto* secaoMapa = rotulo(QStringLiteral("ONDE APERTA"), tema::Papel::Legenda, true, "secao",
                             this);
    raiz->addWidget(secaoMapa);
    grafico_ = new GraficoPressao(this);
    rolagemGrafico_ = new QScrollArea(this);
    rolagemGrafico_->setWidget(grafico_);
    rolagemGrafico_->setWidgetResizable(true);
    rolagemGrafico_->setFrameShape(QFrame::NoFrame);
    rolagemGrafico_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    raiz->addWidget(rolagemGrafico_);
    {
        const QString laranja = tema::cor::urgente().name();
        const QString accent = tema::token("accent").name();
        const QString verde = tema::cor::sucesso().name();
        const QString linha = tema::token("line-strong").name();
        auto* legenda = new QLabel(
            QStringLiteral("<span style='color:%1'>□</span> tempo livre · "
                           "<span style='color:%2'>■</span> estudo planejado · "
                           "<span style='color:%3'>■</span> já feito · "
                           "<span style='color:%4'>●</span> prova · "
                           "<span style='color:%4'>■</span> semana crítica (80%+ do tempo "
                           "livre tomado, ou 3+ provas)")
                .arg(linha, accent, verde, laranja),
            this);
        legenda->setTextFormat(Qt::RichText);
        legenda->setWordWrap(true);
        legenda->setFont(tema::fonte(tema::Papel::Legenda));
        legenda->setProperty("classe", QStringLiteral("nota"));
        raiz->addWidget(legenda);
    }

    // --- dicas | plano ---
    auto* divisor = new QSplitter(Qt::Horizontal, this);
    divisor->setChildrenCollapsible(false);

    auto montarColuna = [this, divisor](const QString& titulo, QVBoxLayout** area,
                                        QWidget** conteudo) {
        auto* coluna = new QWidget(divisor);
        auto* v = new QVBoxLayout(coluna);
        v->setContentsMargins(0, 0, tema::esp(2), 0);
        v->setSpacing(tema::esp(2));
        v->addWidget(rotulo(titulo, tema::Papel::Legenda, true, "secao", coluna));
        auto* rol = new QScrollArea(coluna);
        rol->setWidgetResizable(true);
        rol->setFrameShape(QFrame::NoFrame);
        *conteudo = new QWidget(rol);
        *area = new QVBoxLayout(*conteudo);
        (*area)->setContentsMargins(0, 0, tema::esp(2), 0);
        (*area)->setSpacing(tema::esp(2));
        rol->setWidget(*conteudo);
        v->addWidget(rol, 1);
        divisor->addWidget(coluna);
    };
    montarColuna(QStringLiteral("O QUE FAZER"), &areaDicas_, &conteudoDicas_);
    montarColuna(QStringLiteral("PLANO, DIA A DIA"), &areaPlano_, &conteudoPlano_);
    divisor->setStretchFactor(0, 2);
    divisor->setStretchFactor(1, 3);
    raiz->addWidget(divisor, 1);

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* fechar = botoes->button(QDialogButtonBox::Close);
    fechar->setText(QStringLiteral("Fechar"));
    fechar->setAutoDefault(false);
    fechar->setIcon(QIcon());
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes);

    resize(1100, 760);
    replanejar();
}

void DialogoPlanejamento::replanejar() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        resumo_->setText(QStringLiteral("Banco indisponível — não dá para guardar o plano."));
        return;
    }
    prefs_ = db.carregarPreferenciasEstudo();
    const QDate h = QDate::currentDate();
    DateTime hoje;
    hoje.year = h.year();
    hoje.month = h.month();
    hoje.day = h.day();
    plano_ = planejamento::planejar(entradas_.provas, entradas_.entregas, prefs_,
                                    db.carregarSessoesEstudo(), hoje);
    if (!db.substituirSessoesEstudo(plano_.sessoes)) {
        QMessageBox::warning(this, QStringLiteral("Plano não salvo"),
                             QStringLiteral("Não consegui guardar o plano: %1")
                                 .arg(q(db.erro())));
    }

    grafico_->definir(plano_.semanas, plano_.sessoes);
    rolagemGrafico_->setFixedHeight(grafico_->sizeHint().height() +
                                    rolagemGrafico_->horizontalScrollBar()->sizeHint().height());
    atualizarResumo();
    mostrarDicas();
    mostrarPlano();
}

void DialogoPlanejamento::atualizarResumo() {
    int provas = 0;
    for (const auto& w : plano_.semanas) provas += w.provas;
    int planejado = 0, feito = 0;
    const QDate hoje = QDate::currentDate();
    for (const auto& s : plano_.sessoes) {
        if (s.feita) feito += s.minutos;
        else if (paraQ(s.dia) >= hoje) planejado += s.minutos;
    }
    if (provas == 0) {
        resumo_->setText(QStringLiteral("Nenhuma prova pela frente — nada a planejar por "
                                        "enquanto."));
        return;
    }
    resumo_->setText(QStringLiteral("%1 %2 pela frente · %3 de estudo planejado · %4 já feito")
                         .arg(provas)
                         .arg(provas == 1 ? QStringLiteral("prova") : QStringLiteral("provas"))
                         .arg(horas(planejado), horas(feito)));
}

void DialogoPlanejamento::mostrarDicas() {
    esvaziar(areaDicas_);
    if (plano_.dicas.empty()) {
        areaDicas_->addWidget(rotulo(QStringLiteral("Nada a destacar: o plano cabe com folga."),
                                     tema::Papel::Corpo, false, "nota", conteudoDicas_));
    }
    for (const auto& d : plano_.dicas) {
        auto* l = rotulo(q(d.texto), tema::Papel::Corpo, false, nullptr, conteudoDicas_);
        // Déficit e provas no mesmo dia pedem ação: cartão de alerta. O resto
        // é recado.
        const bool alerta = d.tipo == planejamento::TipoDica::Deficit ||
                            d.tipo == planejamento::TipoDica::MesmoDia;
        l->setProperty("classe", alerta ? QStringLiteral("alerta") : QStringLiteral("recado"));
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        areaDicas_->addWidget(l);
    }
    areaDicas_->addStretch();
}

void DialogoPlanejamento::mostrarPlano() {
    esvaziar(areaPlano_);
    const QDate hoje = QDate::currentDate();

    // De hoje em diante; o passado feito aparece só no "já feito" do resumo e
    // no verde do mapa — a lista é do que vem.
    QDate diaAtual;
    QLabel* cabecalho = nullptr;
    int totalDia = 0;
    auto fecharDia = [&] {
        if (cabecalho) {
            cabecalho->setText(QStringLiteral("%1 · %2").arg(cabecalhoDia(diaAtual), horas(totalDia)));
        }
    };
    bool algum = false;
    for (size_t i = 0; i < plano_.sessoes.size(); ++i) {
        const Sessao& s = plano_.sessoes[i];
        const QDate d = paraQ(s.dia);
        if (d < hoje) continue;
        algum = true;
        if (d != diaAtual) {
            fecharDia();
            diaAtual = d;
            totalDia = 0;
            cabecalho = rotulo(QString(), tema::Papel::Corpo, true, nullptr, conteudoPlano_);
            if (d == hoje) cabecalho->setStyleSheet(QStringLiteral("color: %1").arg(tema::token("accent").name()));
            if (areaPlano_->count() > 0) areaPlano_->addSpacing(tema::esp(2));
            areaPlano_->addWidget(cabecalho);
        }
        totalDia += s.minutos;

        const QDate dp = paraQ(s.dataProva);
        auto* c = new QCheckBox(
            QStringLiteral("%1 — %2 (%3) · %4")
                .arg(q(s.turmaNome), q(s.prova),
                     dp.isValid() ? dp.toString(QStringLiteral("dd/MM")) : QStringLiteral("?"),
                     horas(s.minutos)),
            conteudoPlano_);
        c->setChecked(s.feita);
        c->setToolTip(QStringLiteral("Marque quando tiver estudado. O que fica feito é "
                                     "descontado da prova; o resto do plano se ajusta da "
                                     "próxima vez que esta janela abrir."));
        connect(c, &QCheckBox::toggled, this, [this, i](bool on) { marcar(i, on); });
        areaPlano_->addWidget(c);
    }
    fecharDia();
    if (!algum) {
        areaPlano_->addWidget(rotulo(QStringLiteral("Nenhuma sessão pela frente."),
                                     tema::Papel::Corpo, false, "nota", conteudoPlano_));
    }
    areaPlano_->addStretch();
}

void DialogoPlanejamento::marcar(size_t i, bool feita) {
    if (i >= plano_.sessoes.size()) return;
    store::Database db;
    if (!db.aberto() ||
        !db.marcarSessaoEstudo(plano_.sessoes[i].chave(), feita,
                               QDateTime::currentSecsSinceEpoch())) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar a marcação."));
        return;
    }
    // Só a marca muda agora; o plano não é refeito debaixo do cursor. Ele se
    // reajusta na próxima abertura, ou ao mudar horas e dificuldade.
    plano_.sessoes[i].feita = feita;
    grafico_->definir(plano_.semanas, plano_.sessoes);
    atualizarResumo();
}

void DialogoPlanejamento::abrirPreferencias() {
    DialogoPreferenciasEstudo d(prefs_, entradas_.turmas, this);
    if (d.exec() != QDialog::Accepted) return;
    store::Database db;
    if (!db.aberto() || !db.migrar() || !db.gravarPreferenciasEstudo(d.preferencias())) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar as preferências: %1")
                                 .arg(q(db.erro())));
        return;
    }
    replanejar();
}

} // namespace sigaa::ui
