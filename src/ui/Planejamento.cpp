#include "ui/Planejamento.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "core/store/Database.h"
#include "ui/Agentes.h"
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
// Planejamento
// ---------------------------------------------------------------------------

PainelPlanejamento::PainelPlanejamento(QWidget* pai) : QWidget(pai) {
    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Planejamento"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    resumo_ = rotulo(QString(), tema::Papel::Corpo, false, "nota", this);
    raiz->addWidget(resumo_);

    // --- mapa de pressão ---
    raiz->addWidget(rotulo(QStringLiteral("ONDE APERTA"), tema::Papel::Legenda, true, "secao",
                           this));
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
            QStringLiteral("<span style='color:%1'>□</span> tempo para estudar (já sem as "
                           "aulas) · "
                           "<span style='color:%2'>■</span> estudo planejado · "
                           "<span style='color:%3'>■</span> já feito · "
                           "<span style='color:%4'>●</span> prova · "
                           "<span style='color:%4'>■</span> semana crítica (80%+ do tempo "
                           "tomado, ou 3+ provas)")
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
    auto montarColuna = [divisor](const QString& titulo, QVBoxLayout** area,
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
}

void PainelPlanejamento::definirEntradas(const EntradasEstudo& e) {
    entradas_ = e;
    pendente_ = true;
    // Escondido, fica para quando aparecer: replanejar grava no banco, e o
    // planejamento só passa a existir quando o aluno o abre (opt-in).
    if (isVisible()) replanejar();
}

void PainelPlanejamento::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Sempre ao aparecer, e não só com entradas novas: um check dado na
    // Agenda, ou um dia que virou, mudam o plano.
    replanejar();
}

void PainelPlanejamento::replanejar() {
    pendente_ = false;
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        resumo_->setText(QStringLiteral("Banco indisponível — não dá para guardar o plano."));
        return;
    }
    const QDate h = QDate::currentDate();
    DateTime hoje;
    hoje.year = h.year();
    hoje.month = h.month();
    hoje.day = h.day();
    // O que os agentes de IA registraram entra na conta: estudo feito com
    // eles desconta da prova, e cada dificuldade aberta pede mais tempo.
    planejamento::DoAgente agente;
    agente.estudos = db.carregarRegistrosEstudo();
    agente.focos = db.carregarFocos({}, /*soAbertos=*/true);
    plano_ = planejamento::planejar(entradas_.provas, entradas_.entregas,
                                    db.carregarPreferenciasEstudo(), db.carregarSessoesEstudo(),
                                    hoje, entradas_.aulas, agente);
    if (!db.substituirSessoesEstudo(plano_.sessoes)) {
        resumo_->setText(QStringLiteral("Não consegui guardar o plano: %1").arg(q(db.erro())));
    }

    grafico_->definir(plano_.semanas, plano_.sessoes);
    rolagemGrafico_->setFixedHeight(grafico_->sizeHint().height() +
                                    rolagemGrafico_->horizontalScrollBar()->sizeHint().height());
    atualizarResumo();
    mostrarDicas();
    mostrarPlano();
    if (aoMudar) aoMudar();
}

void PainelPlanejamento::atualizarResumo() {
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
    int comAgente = 0;
    for (const auto& [chave, m] : plano_.comAgente) comAgente += m;
    resumo_->setText(QStringLiteral("%1 %2 pela frente · %3 de estudo planejado · %4 já feito%5")
                         .arg(provas)
                         .arg(provas == 1 ? QStringLiteral("prova") : QStringLiteral("provas"))
                         .arg(horas(planejado), horas(feito))
                         .arg(comAgente ? QStringLiteral(" · %1 com agentes de IA").arg(horas(comAgente))
                                        : QString()));
}

void PainelPlanejamento::mostrarDicas() {
    esvaziar(areaDicas_);
    if (plano_.dicas.empty()) {
        areaDicas_->addWidget(rotulo(QStringLiteral("Nada a destacar: o plano cabe com folga."),
                                     tema::Papel::Corpo, false, "nota", conteudoDicas_));
    }
    for (const auto& d : plano_.dicas) {
        auto* l = rotulo(q(d.texto), tema::Papel::Corpo, false, nullptr, conteudoDicas_);
        // Déficit e provas no mesmo dia pedem ação: cartão de alerta. O resto
        // é recado.
        // Texto puro: a dica de foco cita o que o agente de IA escreveu, e um
        // QLabel em modo automático interpretaria HTML vindo dele.
        l->setTextFormat(Qt::PlainText);
        const bool alerta = d.tipo == planejamento::TipoDica::Deficit ||
                            d.tipo == planejamento::TipoDica::MesmoDia ||
                            d.tipo == planejamento::TipoDica::Foco;
        l->setProperty("classe", alerta ? QStringLiteral("alerta") : QStringLiteral("recado"));
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        areaDicas_->addWidget(l);
    }
    areaDicas_->addStretch();
}

void PainelPlanejamento::mostrarPlano() {
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
                                     "próxima vez que esta página aparecer."));
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

void PainelPlanejamento::marcar(size_t i, bool feita) {
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
    // reajusta da próxima vez que a página aparecer.
    plano_.sessoes[i].feita = feita;
    grafico_->definir(plano_.semanas, plano_.sessoes);
    atualizarResumo();
    if (aoMudar) aoMudar();
}

// ---------------------------------------------------------------------------
// Horas e dificuldade
// ---------------------------------------------------------------------------

PainelDisponibilidade::PainelDisponibilidade(QWidget* pai) : QWidget(pai) {
    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Horas e dificuldade"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    raiz->addWidget(rotulo(
        QStringLiteral("Quanto tempo você tem para a faculdade em cada dia — aulas e estudo "
                       "juntos. O app desconta as aulas da sua grade (cada horário conta "
                       "%1 min) e planeja o estudo no que sobra. Tudo é salvo na hora.")
            .arg(planejamento::kMinutosPorHoraAula),
        tema::Papel::Corpo, false, "nota", this));

    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    conteudo_ = new QWidget(rol);
    area_ = new QVBoxLayout(conteudo_);
    area_->setContentsMargins(0, 0, tema::esp(2), 0);
    area_->setSpacing(tema::esp(4));
    rol->setWidget(conteudo_);
    raiz->addWidget(rol, 1);

    // Um intervalo curto antes de gravar: girar o spin de 2 para 6 h não
    // precisa de cinco escritas no banco e cinco replanos.
    adiar_ = new QTimer(this);
    adiar_->setSingleShot(true);
    adiar_->setInterval(400);
    connect(adiar_, &QTimer::timeout, this, &PainelDisponibilidade::salvar);
}

void PainelDisponibilidade::definirEntradas(const EntradasEstudo& e) {
    // Não remonta com uma edição pendente: o formulário sumiria debaixo do
    // dedo de quem está digitando.
    if (adiar_->isActive()) salvar();
    entradas_ = e;
    store::Database db;
    if (db.aberto() && db.migrar()) prefs_ = db.carregarPreferenciasEstudo();
    montar();
}

void PainelDisponibilidade::montar() {
    esvaziar(area_);

    // --- tempo por dia ---
    auto* horasBox = new QGroupBox(QStringLiteral("Tempo disponível por dia"), conteudo_);
    auto* grade = new QGridLayout(horasBox);
    grade->setHorizontalSpacing(tema::esp(4));
    grade->setVerticalSpacing(tema::esp(2));
    static const char* kDias[] = {"Segunda", "Terça", "Quarta", "Quinta",
                                  "Sexta",   "Sábado", "Domingo"};
    for (int d = 0; d < 7; ++d) {
        const auto i = static_cast<size_t>(d);
        auto* nome = new QLabel(QString::fromUtf8(kDias[d]), horasBox);
        auto* campo = new QDoubleSpinBox(horasBox);
        campo->setRange(0, 16);
        campo->setSingleStep(0.5);
        campo->setDecimals(1);
        campo->setSuffix(QStringLiteral(" h"));
        campo->setValue(prefs_.minutosPorDia[i] / 60.0);
        auto* conta = new QLabel(horasBox);
        conta->setProperty("classe", QStringLiteral("nota"));

        // "− 3h20 de aula = 2h40 para estudar": a conta à vista, para o
        // desconto não ser mágica.
        auto atualizarConta = [this, conta, i] {
            const int aula = entradas_.aulas[i];
            const int estudo = planejamento::minutosParaEstudo(prefs_, entradas_.aulas,
                                                               static_cast<int>(i));
            QString t;
            if (aula > 0) {
                t = QStringLiteral("− %1 de aula (%2 horários) = ")
                        .arg(horas(aula))
                        .arg(aula / planejamento::kMinutosPorHoraAula);
            }
            t += QStringLiteral("<b>%1</b> para estudar").arg(horas(estudo));
            conta->setText(t);
        };
        atualizarConta();
        connect(campo, &QDoubleSpinBox::valueChanged, this, [this, i, atualizarConta](double v) {
            prefs_.minutosPorDia[i] = static_cast<int>(v * 60);
            atualizarConta();
            adiar_->start();
        });

        grade->addWidget(nome, d, 0);
        grade->addWidget(campo, d, 1);
        grade->addWidget(conta, d, 2);
    }
    grade->setColumnStretch(2, 1);
    horasBox->setMaximumWidth(760);
    area_->addWidget(horasBox);

    // --- dificuldade ---
    auto* difBox = new QGroupBox(QStringLiteral("Dificuldade de cada matéria"), conteudo_);
    difBox->setMaximumWidth(760);
    auto* fd = new QFormLayout(difBox);
    // Os combos no tamanho do texto deles: esticados até a borda da janela,
    // o "Média" ficava a meio metro do nome da matéria.
    fd->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    fd->setHorizontalSpacing(tema::esp(4));
    fd->addRow(rotulo(QStringLiteral("Matéria difícil recebe 50% mais tempo de estudo; fácil, "
                                     "30% menos."),
                      tema::Papel::Legenda, false, "nota", difBox));
    for (const auto& [id, nomeTurma] : entradas_.turmas) {
        auto* c = new QComboBox(difBox);
        c->addItem(QStringLiteral("Fácil"), 1);
        c->addItem(QStringLiteral("Média"), 2);
        c->addItem(QStringLiteral("Difícil"), 3);
        c->setCurrentIndex(static_cast<int>(prefs_.dificuldadeDe(id)) - 1);
        c->setMinimumWidth(160);
        auto* nomeL = new QLabel(q(nomeTurma), difBox);
        nomeL->setWordWrap(true);
        const std::string idTurma = id;
        connect(c, &QComboBox::currentIndexChanged, this, [this, c, idTurma] {
            const auto dif = static_cast<planejamento::Dificuldade>(c->currentData().toInt());
            // Média é o padrão: só guarda o que difere dele.
            if (dif == planejamento::Dificuldade::Media) prefs_.dificuldade.erase(idTurma);
            else prefs_.dificuldade[idTurma] = dif;
            adiar_->start();
        });
        fd->addRow(nomeL, c);
    }
    if (entradas_.turmas.empty()) {
        fd->addRow(rotulo(QStringLiteral("Nenhuma turma coletada ainda."), tema::Papel::Corpo,
                          false, "nota", difBox));
    }
    area_->addWidget(difBox);
    area_->addStretch();
}

void PainelDisponibilidade::salvar() {
    adiar_->stop();
    store::Database db;
    if (!db.aberto() || !db.migrar() || !db.gravarPreferenciasEstudo(prefs_)) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar as preferências: %1")
                                 .arg(q(db.erro())));
        return;
    }
    if (aoSalvar) aoSalvar();
}

// ---------------------------------------------------------------------------
// A aba
// ---------------------------------------------------------------------------

PainelEstudo::PainelEstudo(QString banco, QString materiais, QWidget* pai) : QWidget(pai) {
    auto* raiz = new QHBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(0);

    menu_ = new QListWidget(this);
    menu_->setObjectName(QStringLiteral("menuEstudo"));
    menu_->setFixedWidth(220);
    menu_->setFrameShape(QFrame::NoFrame);
    menu_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    menu_->addItem(QStringLiteral("Planejamento"));
    menu_->addItem(QStringLiteral("Progresso"));
    menu_->addItem(QStringLiteral("Horas e dificuldade"));
    menu_->addItem(QStringLiteral("Agentes de IA"));
    raiz->addWidget(menu_);

    auto* filete = new QFrame(this);
    filete->setObjectName(QStringLiteral("fileteVertical"));
    filete->setFixedWidth(1);
    raiz->addWidget(filete);

    auto* direita = new QWidget(this);
    auto* v = new QVBoxLayout(direita);
    v->setContentsMargins(tema::esp(6), tema::esp(5), tema::esp(6), tema::esp(4));
    paginas_ = new QStackedWidget(direita);
    planejamento_ = new PainelPlanejamento(paginas_);
    progresso_ = new PainelProgresso(banco, paginas_);
    disponibilidade_ = new PainelDisponibilidade(paginas_);
    agentes_ = new PainelAgentes(banco, materiais, paginas_);
    // Na mesma ordem do menu.
    paginas_->addWidget(planejamento_);
    paginas_->addWidget(progresso_);
    paginas_->addWidget(disponibilidade_);
    paginas_->addWidget(agentes_);
    v->addWidget(paginas_);
    raiz->addWidget(direita, 1);

    connect(menu_, &QListWidget::currentRowChanged, paginas_, &QStackedWidget::setCurrentIndex);
    menu_->setCurrentRow(0);

    planejamento_->aoMudar = [this] {
        if (aoMudarPlano) aoMudarPlano();
    };
    // Horas ou dificuldade novas mudam o plano; ele é refeito ao voltar à
    // página Planejamento (showEvent), e a Agenda precisa saber já.
    disponibilidade_->aoSalvar = [this] {
        if (aoMudarPlano) aoMudarPlano();
    };
    // Sem agente conectado, o Progresso oferece o caminho até a conexão.
    progresso_->aoPedirConexao = [this] { mostrarAgentes(); };
    // Um foco resolvido ou um registro apagado mudam o que o plano reserva.
    progresso_->aoMudar = [this] {
        if (aoMudarPlano) aoMudarPlano();
    };
}

void PainelEstudo::definirEntradas(const EntradasEstudo& e) {
    planejamento_->definirEntradas(e);
    disponibilidade_->definirEntradas(e);
    // Recarga do banco (um sync, ou um agente que gravou): o Progresso à
    // vista relê; escondido, relê ao aparecer.
    if (progresso_->isVisible()) progresso_->atualizar();
}

void PainelEstudo::mostrarPlanejamento() { menu_->setCurrentRow(0); }

void PainelEstudo::mostrarAgentes() {
    menu_->setCurrentRow(3);
    agentes_->mostrarConexao();
}

} // namespace sigaa::ui
