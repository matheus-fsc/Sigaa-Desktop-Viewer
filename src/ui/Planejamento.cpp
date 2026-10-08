#include "ui/Planejamento.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "core/store/Database.h"
#include "core/util/Texto.h"
#include "mcp/Instalar.h"
#include "mcp/Leitura.h"
#include "mcp/Propostas.h"
#include "ui/Agentes.h"
#include "ui/Controles.h"
#include "ui/GraficoCarga.h"
#include "ui/Progresso.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

using planejamento::Sessao;

QDate paraQ(const DateTime& d) { return d.valid() ? QDate(d.year, d.month, d.day) : QDate(); }

QString q(const std::string& s) { return QString::fromStdString(s); }

QString horas(int minutos) { return q(planejamento::duracao(minutos)); }

QLocale ptBr() { return QLocale(QLocale::Portuguese, QLocale::Brazil); }

// "ter 06/10"
QString diaCurto(QDate d) {
    return ptBr().toString(d, QStringLiteral("ddd dd/MM")).remove(QLatin1Char('.'));
}

// "ter 06/10 07:55", ou só o dia quando a hora não é conhecida.
QString dataHora(const DateTime& d) {
    QString s = diaCurto(paraQ(d));
    if (d.hasTime) s += QStringLiteral(" %1:%2").arg(d.hour, 2, 10, QLatin1Char('0')).arg(d.minute, 2, 10, QLatin1Char('0'));
    return s;
}

// "hoje", "amanhã", "em 5 dias"
QString relativo(qint64 dias) {
    return dias == 0 ? QStringLiteral("hoje") : dias == 1 ? QStringLiteral("amanhã") : QStringLiteral("em %1 dias").arg(dias);
}

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

// Texto que pode ter vindo do SIGAA ou de um agente: nunca HTML.
QLabel* puro(const QString& texto, tema::Papel papel, bool negrito, const char* classe, QWidget* pai) {
    auto* l = rotulo(QString(), papel, negrito, classe, pai);
    l->setTextFormat(Qt::PlainText);
    l->setText(texto);
    return l;
}

QLabel* colorido(const QString& texto, const char* token, bool negrito, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setTextFormat(Qt::PlainText);
    l->setStyleSheet(QStringLiteral("color: %1").arg(tema::token(token).name()));
    QFont f = tema::fonte(tema::Papel::Legenda);
    if (negrito) f.setWeight(QFont::Bold);
    l->setFont(f);
    return l;
}

// Esvazia um layout. `deleteLater`, e não `delete`: quem chama pode estar
// dentro do clique de um widget desta mesma lista.
void esvaziar(QLayout* area) {
    while (QLayoutItem* it = area->takeAt(0)) {
        // Escondido já: até o deleteLater rodar, ele ficaria por baixo do novo.
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        if (QLayout* l = it->layout()) esvaziar(l);
        delete it;
    }
}

// O SIGAA manda o nome em caixa alta: "EQUAÇÕES DIFERENCIAIS ORDINÁRIAS".
bool romano(const QString& w) {
    static const QRegularExpression re(QStringLiteral("^[ivxl]{1,4}$"));
    return re.match(w).hasMatch();
}
bool palavraMenor(const QString& w) {
    static const QStringList m{QStringLiteral("de"), QStringLiteral("da"), QStringLiteral("do"),
                               QStringLiteral("das"), QStringLiteral("dos"), QStringLiteral("e"),
                               QStringLiteral("em"), QStringLiteral("para"), QStringLiteral("a"),
                               QStringLiteral("o"), QStringLiteral("com")};
    return m.contains(w);
}
QString tituloMateria(const QString& nome) {
    QStringList ps = nome.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < ps.size(); ++i) {
        QString& w = ps[i];
        if (romano(w)) w = w.toUpper();
        else if (i == 0 || !palavraMenor(w)) w[0] = w[0].toUpper();
    }
    return ps.join(QLatin1Char(' '));
}
// "EDO", "PAA", "Compiladores": curto o bastante para um chip.
QString curtoMateria(const QString& nome) {
    const QString t = tituloMateria(nome);
    if (t.size() <= 14) return t;
    QString s;
    for (const QString& w : nome.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (romano(w)) s += QLatin1Char(' ') + w.toUpper();
        else if (!palavraMenor(w)) s += w[0].toUpper();
    }
    return s;
}

QFrame* cartao(QWidget* pai) {
    auto* f = new QFrame(pai);
    f->setProperty("classe", QStringLiteral("cartao"));
    return f;
}

QLabel* tituloCartao(const QString& t, QWidget* pai) {
    auto* l = new QLabel(t, pai);
    QFont f = tema::fonte(tema::Papel::Subtitulo);
    f.setWeight(QFont::Bold);
    l->setFont(f);
    return l;
}

QPixmap quadrado(const QColor& c, int lado = 10) {
    const qreal dpr = 2;
    QPixmap px(static_cast<int>(lado * dpr), static_cast<int>(lado * dpr));
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0, 0, lado, lado), 2, 2);
    return px;
}

QPixmap losango(const QColor& c, bool vazado, int lado = 12) {
    const qreal dpr = 2;
    QPixmap px(static_cast<int>(lado * dpr), static_cast<int>(lado * dpr));
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal m = lado / 2.0, r = lado / 2.0 - 1.5;
    QPolygonF poli;
    poli << QPointF(m, m - r) << QPointF(m + r, m) << QPointF(m, m + r) << QPointF(m - r, m);
    p.setPen(QPen(c, 1.5));
    p.setBrush(vazado ? Qt::NoBrush : QBrush(c));
    p.drawPolygon(poli);
    return px;
}

QLabel* imagem(const QPixmap& px, QWidget* pai) {
    auto* l = new QLabel(pai);
    l->setPixmap(px);
    l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return l;
}

// Ícone quadrado de 24 px com um glifo: "↥" entrega, "?" confirmar, "◔" estudo.
QLabel* icone(const QString& glifo, const char* tom, QWidget* pai) {
    auto* l = new QLabel(glifo, pai);
    l->setProperty("classe", QStringLiteral("icone"));
    l->setProperty("tom", QString::fromLatin1(tom));
    l->setAlignment(Qt::AlignCenter);
    l->setFixedSize(24, 24);
    QFont f = l->font();
    f.setWeight(QFont::Bold);
    l->setFont(f);
    return l;
}

QPushButton* caixa(bool marcada, const QString& nome, QWidget* pai) {
    auto* b = new QPushButton(marcada ? QStringLiteral("✓") : QString(), pai);
    b->setProperty("papel", QStringLiteral("caixa"));
    b->setCheckable(true);
    b->setChecked(marcada);
    b->setAutoDefault(false);
    b->setFixedSize(22, 22);
    b->setCursor(Qt::PointingHandCursor);
    b->setAccessibleName(nome);
    QFont f = b->font();
    f.setWeight(QFont::Bold);
    b->setFont(f);
    return b;
}

// Layout que quebra linha: chips de sessão, tópicos da prova.
class Fluxo : public QLayout {
public:
    explicit Fluxo(QWidget* pai, int h = 6, int v = 6) : QLayout(pai), h_(h), v_(v) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~Fluxo() override {
        while (QLayoutItem* it = takeAt(0)) delete it;
    }
    void addItem(QLayoutItem* i) override { itens_.append(i); }
    int count() const override { return static_cast<int>(itens_.size()); }
    QLayoutItem* itemAt(int i) const override { return itens_.value(i); }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < itens_.size() ? itens_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return arrumar(QRect(0, 0, w, 0), true); }
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        arrumar(r, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize s;
        for (auto* i : itens_) s = s.expandedTo(i->minimumSize());
        return s;
    }

private:
    int arrumar(const QRect& r, bool soMedir) const {
        int x = r.x(), y = r.y(), alt = 0;
        for (auto* i : itens_) {
            const QSize sh = i->sizeHint();
            if (x + sh.width() > r.right() + 1 && x > r.x()) {
                x = r.x();
                y += alt + v_;
                alt = 0;
            }
            if (!soMedir) i->setGeometry(QRect(QPoint(x, y), sh));
            x += sh.width() + h_;
            alt = std::max(alt, sh.height());
        }
        return y + alt - r.y();
    }
    QList<QLayoutItem*> itens_;
    int h_, v_;
};

// Barra de acerto com as marcas de 50% e 70%.
class BarraAcerto : public QWidget {
public:
    BarraAcerto(int pct, QColor cor, QWidget* pai) : QWidget(pai), pct_(pct), cor_(std::move(cor)) {
        setFixedHeight(10);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF trilho(0, 2, width(), 6);
        p.setPen(Qt::NoPen);
        p.setBrush(tema::token("surface-3"));
        p.drawRoundedRect(trilho, 3, 3);
        p.setBrush(cor_);
        p.drawRoundedRect(QRectF(0, 2, width() * std::clamp(pct_, 0, 100) / 100.0, 6), 3, 3);
        p.setPen(QPen(tema::token("text-3"), 1));
        for (double m : {0.5, 0.7}) p.drawLine(QPointF(width() * m, 0), QPointF(width() * m, 10));
    }

private:
    int pct_;
    QColor cor_;
};

const char* tomDoAcerto(int pct) { return pct < 50 ? "danger" : pct < 70 ? "warn" : "ok"; }

QString nivelTxt(int n) {
    n = std::clamp(n, 1, 3);
    return (n == 3 ? QStringLiteral("●●● crítico") : n == 2 ? QStringLiteral("●●○ dificuldade") : QStringLiteral("●○○ atenção"));
}

// O que o agente fez, numa frase.
QString acaoDoAgente(const std::string& ferramenta) {
    static const std::map<std::string, QString> m{
        {"registrar_estudo", QStringLiteral("registrou uma sessão de estudo")},
        {"registrar_desempenho", QStringLiteral("registrou o seu desempenho num tópico")},
        {"marcar_foco", QStringLiteral("marcou um ponto de foco")},
        {"resolver_foco", QStringLiteral("resolveu um ponto de foco")},
        {"materia_da_prova", QStringLiteral("leu a matéria de uma prova")},
        {"listar_provas", QStringLiteral("consultou as suas provas")},
        {"listar_turmas", QStringLiteral("consultou as suas turmas")},
        {"topicos_de_aula", QStringLiteral("leu os tópicos de aula")},
        {"meu_progresso", QStringLiteral("consultou o seu progresso")},
        {"propor_horas", QStringLiteral("propôs mudar as suas horas de estudo")},
        {"propor_dificuldade", QStringLiteral("propôs mudar a dificuldade de uma matéria")},
        {"propor_sessao", QStringLiteral("propôs uma sessão extra de revisão")},
        {"baixar_arquivo", QStringLiteral("buscou um material no SIGAA")},
    };
    const auto it = m.find(ferramenta);
    return it != m.end() ? it->second : QStringLiteral("usou %1").arg(q(ferramenta));
}

} // namespace

// ---------------------------------------------------------------------------
// Planejamento
// ---------------------------------------------------------------------------

struct PainelPlanejamento::Dados {
    QDate hoje;
    planejamento::Preferencias prefs;
    std::vector<Sessao> guardadas;              // do banco, antes do replano
    std::vector<estudo::RegistroEstudo> estudos;
    std::vector<estudo::PontoFoco> focos;       // abertos, e os resolvidos agora
    std::vector<estudo::Desempenho> desempenhos;
    std::vector<SemanaCarga> semanas;           // as colunas do gráfico
    std::vector<store::Database::AcessoMcp> acessos;
    bool conectado{false};
    QString nomeAgente;
    bool leitura{false}, arquivos{false}, escrita{false}, rede{false};
};

PainelPlanejamento::PainelPlanejamento(QWidget* pai) : QWidget(pai), dados_(std::make_shared<Dados>()) {
    auto* fora = new QVBoxLayout(this);
    fora->setContentsMargins(0, 0, 0, 0);
    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    fora->addWidget(rol);
    auto* pagina = new QWidget(rol);
    rol->setWidget(pagina);
    auto* grade = new QGridLayout(pagina);
    grade->setContentsMargins(0, 0, tema::esp(3), tema::esp(4));
    grade->setHorizontalSpacing(tema::esp(5));
    grade->setVerticalSpacing(tema::esp(5));
    grade->setColumnStretch(0, 1);
    grade->setColumnMinimumWidth(1, 380);

    // --- cabeçalho ---
    {
        auto* cab = new QHBoxLayout;
        cab->setSpacing(tema::esp(6));
        auto* esq = new QVBoxLayout;
        esq->setSpacing(2);
        auto* titulo = new QLabel(QStringLiteral("Planejamento"), pagina);
        QFont ft = tema::fonte(tema::Papel::Titulo);
        ft.setWeight(QFont::Bold);
        titulo->setFont(ft);
        esq->addWidget(titulo);
        semana_ = rotulo(QString(), tema::Papel::Corpo, false, "nota", pagina);
        semana_->setWordWrap(false);
        esq->addWidget(semana_);
        cab->addLayout(esq);
        cab->addStretch();
        indicadores_ = new QLabel(pagina);
        indicadores_->setTextFormat(Qt::RichText);
        cab->addWidget(indicadores_, 0, Qt::AlignBottom);
        grade->addLayout(cab, 0, 0, 1, 2);
    }

    // --- gráfico ---
    {
        auto* c = cartao(pagina);
        auto* v = new QVBoxLayout(c);
        v->setContentsMargins(tema::esp(4) + 2, tema::esp(4) + 2, tema::esp(4) + 2, tema::esp(4));
        v->setSpacing(tema::esp(3) + 2);
        auto* topo = new QHBoxLayout;
        topo->setSpacing(tema::esp(5));
        auto* t = new QVBoxLayout;
        t->setSpacing(2);
        t->addWidget(tituloCartao(QStringLiteral("Onde o semestre aperta"), c));
        t->addWidget(rotulo(QStringLiteral("Horas de estudo planejadas por semana e por matéria, contra o seu "
                                           "tempo livre (já sem as aulas)"),
                            tema::Papel::Legenda, false, "nota", c));
        topo->addLayout(t, 1);
        alertas_ = new QLabel(c);
        alertas_->setTextFormat(Qt::RichText);
        alertas_->setWordWrap(true);
        alertas_->setMaximumWidth(320);
        connect(alertas_, &QLabel::linkActivated, this, [this](const QString& l) {
            const int i = l.mid(2).toInt();
            grafico_->escolher(i);
            semanaEscolhida_ = i;
            mostrarChecklist();
        });
        topo->addWidget(alertas_, 0, Qt::AlignTop);
        v->addLayout(topo);
        legenda_ = new QWidget(c);
        new Fluxo(legenda_, tema::esp(3), tema::esp(1) + 2);
        v->addWidget(legenda_);
        // Direto no cartão, sem área de rolagem: o gráfico pede 44 px por
        // semana, e a página inteira já rola se faltar largura.
        grafico_ = new GraficoCarga(c);
        grafico_->aoEscolher = [this](int i) {
            semanaEscolhida_ = i;
            mostrarChecklist();
        };
        v->addWidget(grafico_);
        grade->addWidget(c, 1, 0);
    }

    // --- agente ---
    {
        auto* c = cartao(pagina);
        c->setFixedWidth(380);
        areaAgente_ = new QVBoxLayout(c);
        areaAgente_->setContentsMargins(tema::esp(4) + 2, tema::esp(4), tema::esp(4) + 2, tema::esp(4));
        areaAgente_->setSpacing(tema::esp(3));
        grade->addWidget(c, 1, 1, Qt::AlignTop);
    }

    // --- o que fazer ---
    {
        auto* c = cartao(pagina);
        auto* v = new QVBoxLayout(c);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto* cab = new QHBoxLayout;
        cab->setContentsMargins(tema::esp(4) + 2, tema::esp(3) + 2, tema::esp(4) + 2, tema::esp(3));
        cab->setSpacing(tema::esp(2) + 2);
        cab->addWidget(tituloCartao(QStringLiteral("O que fazer"), c));
        subChecklist_ = rotulo(QString(), tema::Papel::Legenda, false, "nota", c);
        cab->addWidget(subChecklist_, 1, Qt::AlignBottom);
        v->addLayout(cab);
        areaChecklist_ = new QVBoxLayout;
        areaChecklist_->setSpacing(0);
        v->addLayout(areaChecklist_);
        grade->addWidget(c, 2, 0, Qt::AlignTop);
    }

    // --- ao lado ---
    {
        auto* lado = new QVBoxLayout;
        lado->setSpacing(tema::esp(5));
        auto bloco = [&](QVBoxLayout** area) {
            auto* c = cartao(pagina);
            c->setFixedWidth(380);
            *area = new QVBoxLayout(c);
            (*area)->setContentsMargins(0, 0, 0, 0);
            (*area)->setSpacing(0);
            lado->addWidget(c);
        };
        bloco(&areaPreparo_);
        bloco(&areaFocos_);
        bloco(&areaDesempenho_);
        lado->addStretch();
        grade->addLayout(lado, 2, 1);
    }
    grade->setRowStretch(3, 1);
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

void PainelPlanejamento::replanejarSeExistir() {
    store::Database db;
    if (!db.aberto() || !db.migrar() || db.carregarSessoesEstudo().empty()) return;
    replanejar();
}

void PainelPlanejamento::replanejar() {
    pendente_ = false;
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        semana_->setText(QStringLiteral("Banco indisponível — não dá para guardar o plano."));
        return;
    }
    Dados& d = *dados_;
    d.hoje = QDate::currentDate();
    DateTime hoje;
    hoje.year = d.hoje.year();
    hoje.month = d.hoje.month();
    hoje.day = d.hoje.day();
    d.prefs = db.carregarPreferenciasEstudo();
    d.guardadas = db.carregarSessoesEstudo();
    d.estudos = db.carregarRegistrosEstudo();
    d.desempenhos = db.carregarDesempenho();
    d.focos.clear();
    for (auto& f : db.carregarFocos()) {
        if (!f.resolvido || std::find(resolvidos_.begin(), resolvidos_.end(), f.id) != resolvidos_.end()) {
            d.focos.push_back(std::move(f));
        }
    }
    d.acessos = db.ultimosAcessosMcp(1);
    d.leitura = mcp::permitido(db, mcp::Permissao::Leitura);
    d.arquivos = mcp::permitido(db, mcp::Permissao::Arquivos);
    d.escrita = mcp::permitido(db, mcp::Permissao::Escrita);
    d.rede = mcp::permitido(db, mcp::Permissao::Rede);
    d.conectado = false;
    const auto pastas = mcp::pastasDoSistema();
    for (const auto& c : mcp::clientes()) {
        if (mcp::instalado(c.cliente, pastas)) {
            d.conectado = true;
            d.nomeAgente = QString::fromUtf8(c.nome);
            break;
        }
    }

    // O que os agentes de IA registraram entra na conta: estudo feito com
    // eles desconta da prova, e cada dificuldade aberta pede mais tempo.
    planejamento::DoAgente agente;
    agente.estudos = d.estudos;
    for (const auto& f : d.focos) {
        if (!f.resolvido) agente.focos.push_back(f);
    }
    agente.propostas = db.carregarPropostas();
    plano_ = planejamento::planejar(entradas_.provas, entradas_.entregas, d.prefs, d.guardadas, hoje,
                                    entradas_.aulas, agente);
    if (!db.substituirSessoesEstudo(plano_.sessoes, hoje.toIso())) {
        semana_->setText(QStringLiteral("Não consegui guardar o plano: %1").arg(q(db.erro())));
    }
    mostrar();
    if (aoMudar) aoMudar();
}

int PainelPlanejamento::materiaDe(const std::string& idTurma) const {
    for (size_t i = 0; i < entradas_.turmas.size(); ++i) {
        if (entradas_.turmas[i].first == idTurma) return static_cast<int>(i);
    }
    return -1;
}

void PainelPlanejamento::mostrar() {
    mostrarGrafico();   // monta as semanas que o cabeçalho e o checklist usam
    mostrarCabecalho();
    mostrarAgente();
    mostrarChecklist();
    mostrarLateral();
}

// ---------------------------------------------------------------------------

void PainelPlanejamento::mostrarCabecalho() {
    const Dados& d = *dados_;
    const QDate seg = d.hoje.addDays(1 - d.hoje.dayOfWeek());
    semana_->setText(QStringLiteral("Semana de %1 a %2 · hoje, %3")
                         .arg(seg.toString(QStringLiteral("dd/MM")), seg.addDays(6).toString(QStringLiteral("dd/MM")),
                              diaCurto(d.hoje)));

    QStringList partes;
    QDate prox;
    for (const auto& p : entradas_.provas) {
        const QDate dp = paraQ(p.data);
        if (dp.isValid() && dp >= d.hoje && (!prox.isValid() || dp < prox)) prox = dp;
    }
    if (prox.isValid()) {
        const qint64 n = d.hoje.daysTo(prox);
        partes << QStringLiteral("<b style='color:%1'>◷ Próxima prova %2</b>")
                      .arg(tema::token("warn").name(), n == 0 ? QStringLiteral("hoje") : n == 1 ? QStringLiteral("amanhã")
                                                                                            : QStringLiteral("em %1 dias").arg(n));
    }
    if (!entradas_.atrasadas.empty()) {
        partes << QStringLiteral("<b style='color:%1'>! %2 entrega%3 atrasada%3</b>")
                      .arg(tema::token("danger").name())
                      .arg(entradas_.atrasadas.size())
                      .arg(entradas_.atrasadas.size() == 1 ? QString() : QStringLiteral("s"));
    }
    // A semana atual é a segunda coluna do gráfico (a primeira é a passada).
    for (const auto& s : d.semanas) {
        if (s.inicio != seg) continue;
        partes << QStringLiteral("<span style='color:%1'>Esta semana: <b style='color:%2'>%3</b> feitas de %4</span>")
                      .arg(tema::token("text-2").name(), tema::token("text").name(), horas(s.feito), horas(s.total()));
    }
    indicadores_->setText(partes.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
}

// ---------------------------------------------------------------------------

void PainelPlanejamento::mostrarGrafico() {
    Dados& d = *dados_;
    const size_t nMat = entradas_.turmas.size();
    std::vector<MateriaCarga> materias;
    for (size_t i = 0; i < nMat; ++i) {
        const QString nome = q(entradas_.turmas[i].second);
        materias.push_back({tituloMateria(nome), curtoMateria(nome), tema::cor::materia(static_cast<int>(i))});
    }

    // Uma semana para trás (o que foi feito) e as do plano em diante.
    const QDate segAtual = d.hoje.addDays(1 - d.hoje.dayOfWeek());
    QDate ultima = segAtual;
    for (const auto& w : plano_.semanas) ultima = std::max(ultima, paraQ(w.inicio));
    std::vector<SemanaCarga> semanas;
    for (QDate seg = segAtual.addDays(-7); seg <= ultima; seg = seg.addDays(7)) {
        SemanaCarga s;
        s.inicio = seg;
        s.porMateria.assign(nMat, 0);
        semanas.push_back(std::move(s));
    }
    auto semanaDe = [&](QDate dia) -> SemanaCarga* {
        if (!dia.isValid() || dia < semanas.front().inicio) return nullptr;
        const qint64 i = semanas.front().inicio.daysTo(dia) / 7;
        return i < static_cast<qint64>(semanas.size()) ? &semanas[static_cast<size_t>(i)] : nullptr;
    };
    // O livre: o do plano para as semanas dele; a passada, pela conta do dia.
    for (const auto& w : plano_.semanas) {
        if (SemanaCarga* s = semanaDe(paraQ(w.inicio))) s->livre = w.minutosLivres;
    }
    {
        SemanaCarga& passada = semanas.front();
        for (int k = 0; k < 7; ++k) passada.livre += planejamento::minutosParaEstudo(d.prefs, entradas_.aulas, k);
    }
    auto somar = [&](const Sessao& x) {
        const QDate dia = paraQ(x.dia);
        SemanaCarga* s = semanaDe(dia);
        if (!s) return;
        const int m = materiaDe(x.idTurma);
        if (m >= 0) s->porMateria[static_cast<size_t>(m)] += x.minutos;
        s->dia[static_cast<size_t>(dia.dayOfWeek() - 1)] += x.minutos;
        if (x.feita) s->feito += x.minutos;
    };
    for (const auto& x : plano_.sessoes) somar(x);
    // As pendentes que ficaram para trás não estão no plano novo, mas estavam
    // no de antes: contam no planejado da semana em que caíram.
    for (const auto& x : d.guardadas) {
        if (!x.feita && paraQ(x.dia) < d.hoje) somar(x);
    }
    for (const auto& r : d.estudos) {
        if (SemanaCarga* s = semanaDe(paraQ(r.quando))) {
            s->feito += r.minutos;
            s->viaAgente += r.minutos;
        }
    }
    for (const auto& p : entradas_.provas) {
        const QDate dp = paraQ(p.data);
        if (SemanaCarga* s = semanaDe(dp)) {
            const int m = materiaDe(p.idTurma);
            s->provas.push_back({dp.dayOfWeek() - 1, m,
                                 dataHora(p.data) + QStringLiteral(" · ") +
                                     (m >= 0 ? materias[static_cast<size_t>(m)].curto : q(p.turmaNome)) +
                                     QStringLiteral(" · ") + q(p.descricao),
                                 p.inferida});
        }
    }
    // A semana passada só aparece se tem o que mostrar.
    if (semanas.front().total() == 0 && semanas.front().feito == 0) semanas.erase(semanas.begin());
    d.semanas = semanas;
    if (semanaEscolhida_ >= static_cast<int>(semanas.size())) semanaEscolhida_ = -1;
    grafico_->definir(materias, semanas, d.hoje);
    grafico_->escolher(semanaEscolhida_);

    // --- alertas: semanas críticas e provas que não cabem ---
    // Curtos: as três próximas semanas críticas e um resumo do déficit. A
    // lista inteira está no gráfico e nas dicas de cada semana.
    QStringList al;
    const QString laranja = tema::token("warn").name();
    auto link = [&](size_t w, const QString& t) {
        return QStringLiteral("<a href='w:%1' style='color:%2;text-decoration:none'><b>! %3</b></a>")
            .arg(w)
            .arg(laranja, t.toHtmlEscaped());
    };
    for (size_t i = 0; i < semanas.size() && al.size() < 3; ++i) {
        const auto& s = semanas[i];
        if (!s.critica() || s.inicio.addDays(6) < d.hoje) continue;
        const int over = s.total() - s.livre;
        al << link(i, s.inicio.toString(QStringLiteral("dd/MM")) + QStringLiteral(": ") +
                          (over > 0              ? QStringLiteral("plano %1 acima do livre").arg(horas(over))
                           : s.provas.size() >= 3 ? QStringLiteral("%1 provas").arg(s.provas.size())
                                                  : QStringLiteral("%1% do tempo livre").arg(s.livre ? 100 * s.total() / s.livre : 100)));
    }
    int faltam = 0, nDeficit = 0;
    size_t semanaDeficit = 0;
    QDate primeira;
    for (const auto& p : entradas_.provas) {
        const auto it = plano_.deficit.find(p.idTurma + "|" + p.descricao);
        if (it == plano_.deficit.end()) continue;
        faltam += it->second;
        ++nDeficit;
        const QDate dp = paraQ(p.data);
        if (!primeira.isValid() || dp < primeira) {
            primeira = dp;
            for (size_t i = 0; i < semanas.size(); ++i) {
                if (dp >= semanas[i].inicio && dp <= semanas[i].inicio.addDays(6)) semanaDeficit = i;
            }
        }
    }
    if (nDeficit == 1) {
        al << link(semanaDeficit, QStringLiteral("1 prova não cabe no tempo livre: faltam %1").arg(horas(faltam)));
    } else if (nDeficit > 1) {
        al << link(semanaDeficit, QStringLiteral("%1 provas não cabem no tempo livre: faltam %2").arg(nDeficit).arg(horas(faltam)));
    }
    alertas_->setText(al.join(QStringLiteral("<br>")));
    alertas_->setVisible(!al.isEmpty());
    alertas_->setToolTip(nDeficit ? QStringLiteral("Aumente as horas em Horas e dificuldade, ou o plano não cobre tudo.")
                                  : QString());

    // --- legenda: as matérias (clique isola) e os símbolos ---
    esvaziar(legenda_->layout());
    for (size_t k = 0; k < materias.size(); ++k) {
        bool tem = false;
        for (const auto& s : semanas) tem = tem || s.porMateria[k] > 0 || std::any_of(s.provas.begin(), s.provas.end(), [&](const auto& p) { return p.materia == static_cast<int>(k); });
        if (!tem) continue;
        auto* b = new QPushButton(QIcon(quadrado(materias[k].cor)), materias[k].curto, legenda_);
        b->setProperty("papel", QStringLiteral("pilulaMateria"));
        b->setCheckable(true);
        b->setChecked(grafico_->isolada() == static_cast<int>(k));
        b->setAutoDefault(false);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(materias[k].nome + QStringLiteral(" · clique para isolar"));
        const int idx = static_cast<int>(k);
        connect(b, &QPushButton::clicked, this, [this, idx] {
            grafico_->isolar(grafico_->isolada() == idx ? -1 : idx);
            mostrarGrafico();
        });
        legenda_->layout()->addWidget(b);
    }
    auto simbolo = [&](const QPixmap& px, const QString& t) {
        auto* w = new QWidget(legenda_);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 4, 0, 4);
        h->setSpacing(tema::esp(1) + 2);
        h->addWidget(imagem(px, w));
        auto* l = rotulo(t, tema::Papel::Legenda, false, "nota", w);
        l->setWordWrap(false);
        h->addWidget(l);
        legenda_->layout()->addWidget(w);
    };
    const qreal dpr = 2;
    auto pinta = [&](int lw, int lh, const std::function<void(QPainter&)>& f) {
        QPixmap px(static_cast<int>(lw * dpr), static_cast<int>(lh * dpr));
        px.setDevicePixelRatio(dpr);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        f(p);
        return px;
    };
    simbolo(pinta(18, 12, [](QPainter& p) {
                p.setPen(QPen(tema::token("text-2"), 2, Qt::DashLine));
                p.drawLine(0, 6, 18, 6);
            }),
            QStringLiteral("tempo livre"));
    simbolo(pinta(12, 12, [](QPainter& p) {
                p.fillRect(QRectF(0, 0, 12, 12), tema::token("text-3"));
                p.fillRect(QRectF(0, 0, 12, 12), QBrush(tema::token("surface"), Qt::BDiagPattern));
                p.setPen(QPen(tema::token("warn"), 2));
                p.drawLine(0, 1, 12, 1);
            }),
            QStringLiteral("acima do livre"));
    simbolo(quadrado(tema::token("ok")), QStringLiteral("feito"));
    simbolo(losango(tema::token("text-2"), false, 11), QStringLiteral("prova"));
    simbolo(losango(tema::token("text-2"), true, 11), QStringLiteral("data deduzida"));
    simbolo(pinta(14, 12, [](QPainter& p) {
                p.setPen(Qt::NoPen);
                p.setBrush(tema::token("warn-soft"));
                p.drawRoundedRect(QRectF(0, 0, 14, 12), 2, 2);
            }),
            QStringLiteral("semana crítica"));
}

// ---------------------------------------------------------------------------

void PainelPlanejamento::mostrarAgente() {
    const Dados& d = *dados_;
    esvaziar(areaAgente_);
    QWidget* c = areaAgente_->parentWidget();
    areaAgente_->addWidget(rotulo(QStringLiteral("AGENTE DE IA"), tema::Papel::Legenda, true, "secao", c));
    if (!d.conectado) {
        areaAgente_->addWidget(tituloCartao(QStringLiteral("Nenhum agente conectado"), c));
        areaAgente_->addWidget(rotulo(QStringLiteral("Conecte Claude, Codex ou Gemini. O agente lê suas matérias, monta "
                                                     "roteiros e simulados, e registra aqui o tempo estudado e os tópicos "
                                                     "em que você tem dificuldade."),
                                      tema::Papel::Corpo, false, "nota", c));
        auto* b = new QPushButton(QStringLiteral("Conectar um agente"), c);
        b->setProperty("papel", QStringLiteral("primario"));
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this] {
            if (aoPedirAgentes) aoPedirAgentes(true);
        });
        auto* linha = new QHBoxLayout;
        linha->addWidget(b);
        linha->addStretch();
        areaAgente_->addLayout(linha);
        areaAgente_->addWidget(rotulo(QStringLiteral("Você escolhe o que ele pode ler e registrar."),
                                      tema::Papel::Legenda, false, "nota", c));
        return;
    }
    auto* topo = new QHBoxLayout;
    topo->addWidget(tituloCartao(d.nomeAgente, c), 1);
    topo->addWidget(pilula(QStringLiteral("● conectado"), "ok", c));
    areaAgente_->addLayout(topo);
    if (!d.acessos.empty()) {
        const auto& a = d.acessos.front();
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(a.quando);
        const qint64 dias = dt.date().daysTo(d.hoje);
        const QString quando = (dias == 0 ? QStringLiteral("hoje") : dias == 1 ? QStringLiteral("ontem") : diaCurto(dt.date())) +
                               QStringLiteral(", ") + dt.toString(QStringLiteral("HH:mm"));
        auto* v = new QVBoxLayout;
        v->setSpacing(2);
        v->addWidget(rotulo(QStringLiteral("Última atividade · ") + quando, tema::Papel::Legenda, false, "nota", c));
        QString frase = q(a.origem) + QStringLiteral(" ") + acaoDoAgente(a.ferramenta);
        for (const auto& [id, nome] : entradas_.turmas) {
            if (id == a.turma) frase += QStringLiteral(" de ") + tituloMateria(q(nome));
        }
        if (!a.ok) frase += QStringLiteral(" (recusado)");
        v->addWidget(puro(frase + QStringLiteral("."), tema::Papel::Corpo, false, nullptr, c));
        areaAgente_->addLayout(v);
    }
    auto* filete = new QFrame(c);
    filete->setProperty("classe", QStringLiteral("linha"));
    filete->setFixedHeight(1);
    areaAgente_->addWidget(filete);
    const std::pair<const char*, bool> perms[] = {{"Ler dados do app", d.leitura},
                                                  {"Ler PDFs dos materiais", d.arquivos},
                                                  {"Registrar estudo e desempenho", d.escrita},
                                                  {"Buscar no SIGAA", d.rede}};
    for (const auto& [rot, on] : perms) {
        auto* h = new QHBoxLayout;
        h->setSpacing(tema::esp(2));
        auto* g = colorido(on ? QStringLiteral("✓") : QStringLiteral("✕"), on ? "ok" : "text-3", true, c);
        g->setFixedWidth(18);
        h->addWidget(g);
        auto* l = rotulo(QString::fromUtf8(rot), tema::Papel::Corpo, false, on ? nullptr : "nota", c);
        h->addWidget(l, 1);
        if (!on) h->addWidget(rotulo(QStringLiteral("desligado"), tema::Papel::Legenda, false, "nota", c));
        areaAgente_->addLayout(h);
    }
    auto* alterar = botaoLink(QStringLiteral("Alterar permissões"), c);
    connect(alterar, &QPushButton::clicked, this, [this] {
        if (aoPedirAgentes) aoPedirAgentes(false);
    });
    auto* linha = new QHBoxLayout;
    linha->addWidget(alterar);
    linha->addStretch();
    areaAgente_->addLayout(linha);
}

// ---------------------------------------------------------------------------
// O que fazer
// ---------------------------------------------------------------------------

namespace {

// Uma linha do checklist: à esquerda a caixa ou o ícone; no meio o título e
// "Tipo · detalhe"; à direita, pílulas, duração ou botões.
QFrame* linhaItem(QWidget* pai, QWidget* esquerda, const QString& titulo, const QColor& cor, bool riscado,
                  const QString& tipo, const QString& meta, QHBoxLayout** direita) {
    auto* f = new QFrame(pai);
    f->setProperty("classe", QStringLiteral("linhaTopo"));
    auto* h = new QHBoxLayout(f);
    h->setContentsMargins(tema::esp(4) + 2, tema::esp(2) + 2, tema::esp(4) + 2, tema::esp(2) + 2);
    h->setSpacing(tema::esp(3));
    auto* slot = new QWidget(f);
    slot->setFixedWidth(28);
    auto* sl = new QHBoxLayout(slot);
    sl->setContentsMargins(0, 0, 0, 0);
    if (esquerda) {
        esquerda->setParent(slot);
        sl->addWidget(esquerda, 0, Qt::AlignCenter);
    }
    h->addWidget(slot);
    auto* meio = new QVBoxLayout;
    meio->setSpacing(2);
    auto* lt = new QHBoxLayout;
    lt->setSpacing(tema::esp(2));
    if (cor.isValid()) lt->addWidget(imagem(quadrado(cor), f));
    auto* t = puro(titulo, tema::Papel::Corpo, true, riscado ? "nota" : nullptr, f);
    if (riscado) {
        QFont ft = t->font();
        ft.setStrikeOut(true);
        t->setFont(ft);
    }
    lt->addWidget(t, 1);
    meio->addLayout(lt);
    auto* lm = new QHBoxLayout;
    lm->setSpacing(4);
    lm->addWidget(rotulo(tipo, tema::Papel::Legenda, true, "nota", f));
    if (!meta.isEmpty()) {
        auto* m = puro(QStringLiteral("· ") + meta, tema::Papel::Legenda, false, "nota", f);
        lm->addWidget(m, 1);
    } else {
        lm->addStretch();
    }
    meio->addLayout(lm);
    h->addLayout(meio, 1);
    *direita = new QHBoxLayout;
    (*direita)->setSpacing(tema::esp(2));
    h->addLayout(*direita);
    return f;
}

} // namespace

void PainelPlanejamento::mostrarChecklist() {
    esvaziar(areaChecklist_);
    const Dados& d = *dados_;
    QWidget* pai = areaChecklist_->parentWidget();
    const bool semanaFiltrada = semanaEscolhida_ >= 0 && semanaEscolhida_ < static_cast<int>(d.semanas.size()) &&
                                !(d.hoje >= d.semanas[static_cast<size_t>(semanaEscolhida_)].inicio &&
                                  d.hoje <= d.semanas[static_cast<size_t>(semanaEscolhida_)].inicio.addDays(6));
    subChecklist_->setText(semanaFiltrada ? QStringLiteral("Filtrado pela semana escolhida no gráfico")
                                          : QStringLiteral("Hoje, esta semana e até a próxima prova"));
    if (semanaFiltrada) {
        mostrarSemana(semanaEscolhida_);
        return;
    }

    auto nomeMateria = [&](const std::string& id, const std::string& fallback) {
        const int m = materiaDe(id);
        return m >= 0 ? tituloMateria(q(entradas_.turmas[static_cast<size_t>(m)].second)) : tituloMateria(q(fallback));
    };
    auto curto = [&](const std::string& id, const std::string& fallback) {
        const int m = materiaDe(id);
        return m >= 0 ? curtoMateria(q(entradas_.turmas[static_cast<size_t>(m)].second)) : curtoMateria(q(fallback));
    };
    auto cor = [&](const std::string& id) {
        const int m = materiaDe(id);
        return m >= 0 ? tema::cor::materia(m) : QColor();
    };

    // O cabeçalho de um grupo: abre e fecha.
    auto grupo = [&](const QString& chave, const QString& titulo, const QString& sub, bool perigo) {
        const bool aberto = !fechados_.count(chave);
        auto* b = new QPushButton(pai);
        b->setProperty("papel", QStringLiteral("grupo"));
        b->setProperty("tom", perigo ? QStringLiteral("perigo") : QString());
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
        b->setAccessibleName(titulo + (aberto ? QStringLiteral(", aberto") : QStringLiteral(", fechado")));
        auto* h = new QHBoxLayout(b);
        h->setContentsMargins(tema::esp(4) + 2, tema::esp(2), tema::esp(4) + 2, tema::esp(2));
        h->setSpacing(tema::esp(2) + 2);
        auto* seta = rotulo(aberto ? QStringLiteral("▾") : QStringLiteral("▸"), tema::Papel::Legenda, false, "nota", b);
        seta->setFixedWidth(12);
        h->addWidget(seta);
        auto* t = new QLabel(titulo, b);
        QFont ft = tema::fonte(tema::Papel::Corpo);
        ft.setWeight(QFont::Bold);
        t->setFont(ft);
        if (perigo) t->setStyleSheet(QStringLiteral("color: %1").arg(tema::token("danger").name()));
        h->addWidget(t);
        h->addStretch();
        h->addWidget(rotulo(sub, tema::Papel::Legenda, false, "nota", b));
        for (QLabel* l : b->findChildren<QLabel*>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
        b->setMinimumHeight(40);
        connect(b, &QPushButton::clicked, this, [this, chave] {
            if (fechados_.count(chave)) fechados_.erase(chave);
            else fechados_.insert(chave);
            mostrarChecklist();
        });
        areaChecklist_->addWidget(b);
        return aberto;
    };

    // Uma sessão como linha (hoje) ou como chip (outros dias).
    auto linhaSessao = [&](const Sessao& s) {
        auto* cx = caixa(s.feita, QStringLiteral("Sessão de %1, %2").arg(nomeMateria(s.idTurma, s.turmaNome), horas(s.minutos)), pai);
        const std::string chave = s.chave();
        connect(cx, &QPushButton::toggled, this, [this, chave](bool on) { marcar(chave, on); });
        QHBoxLayout* dir = nullptr;
        auto* f = linhaItem(pai, cx, nomeMateria(s.idTurma, s.turmaNome), cor(s.idTurma), s.feita,
                            QStringLiteral("Sessão de estudo"), QStringLiteral("para a ") + q(s.prova), &dir);
        dir->addWidget(rotulo(horas(s.minutos), tema::Papel::Corpo, false, "nota", f));
        areaChecklist_->addWidget(f);
    };
    auto linhaDia = [&](QDate dia, const std::vector<const Sessao*>& ss) {
        auto* f = new QFrame(pai);
        f->setProperty("classe", QStringLiteral("linhaTopo"));
        auto* h = new QHBoxLayout(f);
        h->setContentsMargins(tema::esp(4) + 2, tema::esp(2) + 2, tema::esp(4) + 2, tema::esp(2) + 2);
        h->setSpacing(tema::esp(3));
        auto* l = rotulo(diaCurto(dia), tema::Papel::Corpo, true, "nota", f);
        l->setFixedWidth(84);
        h->addWidget(l, 0, Qt::AlignTop);
        auto* chips = new QWidget(f);
        auto* fl = new Fluxo(chips);
        int total = 0;
        for (const Sessao* s : ss) {
            total += s->minutos;
            const QString txt = QStringLiteral("%1%2 · %3   %4")
                                    .arg(s->feita ? QStringLiteral("✓ ") : QString(), curto(s->idTurma, s->turmaNome),
                                         q(s->prova), horas(s->minutos));
            auto* b = new QPushButton(QIcon(quadrado(cor(s->idTurma).isValid() ? cor(s->idTurma) : tema::token("text-3"), 9)), txt, chips);
            b->setProperty("papel", QStringLiteral("chip"));
            b->setCheckable(true);
            b->setChecked(s->feita);
            b->setAutoDefault(false);
            b->setCursor(Qt::PointingHandCursor);
            b->setAccessibleName(QStringLiteral("%1, %2, %3").arg(nomeMateria(s->idTurma, s->turmaNome), q(s->prova), horas(s->minutos)));
            if (s->feita) {
                QFont fb = b->font();
                fb.setStrikeOut(true);
                b->setFont(fb);
            }
            const std::string chave = s->chave();
            connect(b, &QPushButton::toggled, this, [this, chave](bool on) { marcar(chave, on); });
            fl->addWidget(b);
        }
        h->addWidget(chips, 1);
        h->addWidget(rotulo(horas(total), tema::Papel::Legenda, false, "nota", f), 0, Qt::AlignTop);
        areaChecklist_->addWidget(f);
    };
    auto sessoesDoDia = [&](QDate dia) {
        std::vector<const Sessao*> v;
        for (const auto& s : plano_.sessoes) {
            if (paraQ(s.dia) == dia) v.push_back(&s);
        }
        return v;
    };
    auto prazoTxt = [&](const DateTime& p) {
        return p.hasTime ? QStringLiteral("%1:%2").arg(p.hour, 2, 10, QLatin1Char('0')).arg(p.minute, 2, 10, QLatin1Char('0'))
                         : QString();
    };

    // --- atrasado ---
    if (!entradas_.atrasadas.empty()) {
        const int n = static_cast<int>(entradas_.atrasadas.size());
        if (grupo(QStringLiteral("atrasado"), QStringLiteral("Atrasado"),
                  QStringLiteral("%1 entrega%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s")), true)) {
            for (const auto& e : entradas_.atrasadas) {
                QHBoxLayout* dir = nullptr;
                auto* f = linhaItem(pai, icone(QStringLiteral("↥"), "perigo", pai), q(e.titulo), cor(e.idTurma), false,
                                    QStringLiteral("Entrega"),
                                    nomeMateria(e.idTurma, e.turmaNome) + QStringLiteral(" · prazo ") + dataHora(e.prazo), &dir);
                const qint64 dias = paraQ(e.prazo).daysTo(d.hoje);
                dir->addWidget(pilula(QStringLiteral("! Atrasado · %1 dia%2").arg(dias).arg(dias == 1 ? QString() : QStringLiteral("s")),
                                      "perigo", f));
                areaChecklist_->addWidget(f);
            }
        }
    }

    // --- hoje ---
    {
        // Os focos que pedem revisão já: dificuldade ou crítico, com prova da
        // turma nas próximas 3 semanas. No máximo dois.
        std::vector<const estudo::PontoFoco*> focosHoje;
        for (const auto& f : d.focos) {
            if (focosHoje.size() >= 2 || f.nivel < 2) continue;
            for (const auto& p : entradas_.provas) {
                const qint64 n = d.hoje.daysTo(paraQ(p.data));
                if (p.idTurma == f.idTurma && n >= 0 && n <= 21) {
                    focosHoje.push_back(&f);
                    break;
                }
            }
        }
        std::vector<const planejamento::EntregaAlvo*> entregasHoje;
        for (const auto& e : entradas_.entregas) {
            if (paraQ(e.prazo) == d.hoje) entregasHoje.push_back(&e);
        }
        const auto ss = sessoesDoDia(d.hoje);
        int min = 0;
        for (const Sessao* s : ss) min += s->minutos;
        const int itens = static_cast<int>(focosHoje.size() + entregasHoje.size() + ss.size());
        if (grupo(QStringLiteral("hoje"), QStringLiteral("Hoje · ") + diaCurto(d.hoje),
                  itens == 0 ? QStringLiteral("nada planejado")
                             : QStringLiteral("%1 ite%2 · %3 de estudo").arg(itens).arg(itens == 1 ? QStringLiteral("m") : QStringLiteral("ns")).arg(horas(min)),
                  false)) {
            for (const estudo::PontoFoco* f : focosHoje) {
                const std::int64_t id = f->id;
                auto* cx = caixa(f->resolvido, QStringLiteral("Ponto de foco ") + q(f->topico), pai);
                connect(cx, &QPushButton::toggled, this, [this, id](bool on) {
                    store::Database db;
                    if (on) {
                        db.resolverFoco(id, "resolvido pelo aluno", QDateTime::currentSecsSinceEpoch());
                        resolvidos_.push_back(id);
                    } else {
                        db.reabrirFoco(id, QDateTime::currentSecsSinceEpoch());
                    }
                    QTimer::singleShot(0, this, [this] { replanejar(); });
                });
                QString meta;
                for (const auto& p : entradas_.provas) {
                    const qint64 n = d.hoje.daysTo(paraQ(p.data));
                    if (p.idTurma == f->idTurma && n >= 0) {
                        meta = q(p.descricao) + QStringLiteral(" de ") + curto(p.idTurma, p.turmaNome) + QStringLiteral(" ") + relativo(n);
                        break;
                    }
                }
                meta += QStringLiteral(" · marcado por ") + q(f->origem);
                QHBoxLayout* dir = nullptr;
                auto* fr = linhaItem(pai, cx, QStringLiteral("Revisar ") + q(f->topico), cor(f->idTurma), f->resolvido,
                                     QStringLiteral("Ponto de foco"), meta, &dir);
                if (!f->resolvido) dir->addWidget(colorido(nivelTxt(f->nivel), f->nivel >= 3 ? "danger" : "warn", true, fr));
                areaChecklist_->addWidget(fr);
            }
            for (const auto* e : entregasHoje) {
                QHBoxLayout* dir = nullptr;
                auto* f = linhaItem(pai, icone(QStringLiteral("↥"), "aviso", pai), q(e->titulo), cor(e->idTurma), false,
                                    QStringLiteral("Entrega"), nomeMateria(e->idTurma, e->turmaNome), &dir);
                dir->addWidget(pilula(QStringLiteral("◷ Vence hoje ") + prazoTxt(e->prazo), "aviso", f));
                areaChecklist_->addWidget(f);
            }
            for (const Sessao* s : ss) linhaSessao(*s);
            if (itens == 0) {
                auto* f = new QFrame(pai);
                f->setProperty("classe", QStringLiteral("linhaTopo"));
                auto* h = new QHBoxLayout(f);
                h->setContentsMargins(tema::esp(4) + 2, tema::esp(3), tema::esp(4) + 2, tema::esp(3));
                h->addWidget(rotulo(QStringLiteral("Nada para hoje."), tema::Papel::Corpo, false, "nota", f));
                areaChecklist_->addWidget(f);
            }
        }
    }

    // --- esta semana: de amanhã a domingo ---
    const QDate domingo = d.hoje.addDays(7 - d.hoje.dayOfWeek());
    {
        std::vector<const planejamento::EntregaAlvo*> entregas;
        for (const auto& e : entradas_.entregas) {
            const QDate p = paraQ(e.prazo);
            if (p > d.hoje && p <= domingo) entregas.push_back(&e);
        }
        // As três datas deduzidas mais próximas; o resto espera a sua vez.
        std::vector<const planejamento::ProvaAlvo*> deduzidas;
        for (const auto& p : entradas_.provas) {
            if (p.inferida && paraQ(p.data) >= d.hoje) deduzidas.push_back(&p);
        }
        std::stable_sort(deduzidas.begin(), deduzidas.end(), [](auto* a, auto* b) { return paraQ(a->data) < paraQ(b->data); });
        if (deduzidas.size() > 3) deduzidas.resize(3);
        int min = 0;
        for (QDate x = d.hoje.addDays(1); x <= domingo; x = x.addDays(1)) {
            for (const Sessao* s : sessoesDoDia(x)) min += s->minutos;
        }
        const bool temDias = d.hoje < domingo;
        if ((temDias || !entregas.empty() || !deduzidas.empty()) &&
            grupo(QStringLiteral("semana"),
                  temDias ? QStringLiteral("Esta semana · %1 a dom").arg(ptBr().toString(d.hoje.addDays(1), QStringLiteral("ddd")).remove(QLatin1Char('.')))
                          : QStringLiteral("Esta semana"),
                  QStringLiteral("%1 de estudo").arg(horas(min)), false)) {
            for (const auto* e : entregas) {
                QHBoxLayout* dir = nullptr;
                auto* f = linhaItem(pai, icone(QStringLiteral("↥"), "aviso", pai), q(e->titulo), cor(e->idTurma), false,
                                    QStringLiteral("Entrega"), nomeMateria(e->idTurma, e->turmaNome), &dir);
                const QDate p = paraQ(e->prazo);
                dir->addWidget(pilula(QStringLiteral("◷ ") +
                                          (d.hoje.daysTo(p) == 1 ? QStringLiteral("Amanhã")
                                                                 : ptBr().toString(p, QStringLiteral("ddd")).remove(QLatin1Char('.'))) +
                                          QStringLiteral(" ") + prazoTxt(e->prazo),
                                      "aviso", f));
                areaChecklist_->addWidget(f);
            }
            for (const auto* p : deduzidas) {
                QHBoxLayout* dir = nullptr;
                auto* f = linhaItem(pai, icone(QStringLiteral("?"), "aviso", pai),
                                    q(p->descricao) + QStringLiteral(" · ") + nomeMateria(p->idTurma, p->turmaNome), cor(p->idTurma),
                                    false, QStringLiteral("Confirmar data"),
                                    dataHora(p->data) + QStringLiteral(", deduzida de um tópico de aula"), &dir);
                auto* conf = new QPushButton(QStringLiteral("Confirmar"), f);
                conf->setProperty("papel", QStringLiteral("primario"));
                auto* corr = new QPushButton(QStringLiteral("Corrigir"), f);
                corr->setProperty("papel", QStringLiteral("secundario"));
                const std::string id = p->idTurma, desc = p->descricao;
                for (QPushButton* b : {conf, corr}) {
                    b->setAutoDefault(false);
                    dir->addWidget(b);
                }
                connect(conf, &QPushButton::clicked, this, [this, id, desc] {
                    if (aoTratarProva) aoTratarProva(id, desc, false);
                });
                connect(corr, &QPushButton::clicked, this, [this, id, desc] {
                    if (aoTratarProva) aoTratarProva(id, desc, true);
                });
                areaChecklist_->addWidget(f);
            }
            for (QDate x = d.hoje.addDays(1); x <= domingo; x = x.addDays(1)) {
                const auto ss = sessoesDoDia(x);
                if (!ss.empty()) linhaDia(x, ss);
            }
        }
    }

    // --- até a próxima prova ---
    {
        QDate prox;
        for (const auto& p : entradas_.provas) {
            const QDate dp = paraQ(p.data);
            if (dp.isValid() && dp >= d.hoje && (!prox.isValid() || dp < prox)) prox = dp;
        }
        if (prox.isValid()) {
            const planejamento::ProvaAlvo* primeira = nullptr;
            for (const auto& p : entradas_.provas) {
                if (paraQ(p.data) == prox) {
                    primeira = &p;
                    break;
                }
            }
            if (grupo(QStringLiteral("prova"), QStringLiteral("Até a próxima prova"), dataHora(primeira->data), false)) {
                // Os dias depois desta semana, até a véspera (no máximo duas semanas).
                for (QDate x = domingo.addDays(1); x < prox && x <= domingo.addDays(14); x = x.addDays(1)) {
                    const auto ss = sessoesDoDia(x);
                    if (!ss.empty()) linhaDia(x, ss);
                }
                for (const auto& p : entradas_.provas) {
                    if (paraQ(p.data) != prox) continue;
                    QHBoxLayout* dir = nullptr;
                    const QColor c = cor(p.idTurma).isValid() ? cor(p.idTurma) : tema::token("text-2");
                    auto* f = linhaItem(pai, imagem(losango(c, p.inferida), pai),
                                        q(p.descricao) + QStringLiteral(" · ") + nomeMateria(p.idTurma, p.turmaNome), QColor(), false,
                                        QStringLiteral("Prova"), dataHora(p.data), &dir);
                    dir->addWidget(colorido(QStringLiteral("◷ ") + relativo(d.hoje.daysTo(prox)), "warn", true, f));
                    areaChecklist_->addWidget(f);
                }
            }
        }
    }
}

// A semana escolhida no gráfico: o que tem nela, por matéria.
void PainelPlanejamento::mostrarSemana(int i) {
    const Dados& d = *dados_;
    const SemanaCarga& s = d.semanas[static_cast<size_t>(i)];
    QWidget* pai = areaChecklist_->parentWidget();

    auto* cab = new QFrame(pai);
    cab->setProperty("classe", QStringLiteral("faixaAcento"));
    auto* h = new QHBoxLayout(cab);
    h->setContentsMargins(tema::esp(4) + 2, tema::esp(2) + 2, tema::esp(4) + 2, tema::esp(2) + 2);
    h->setSpacing(tema::esp(3));
    auto* t = new QLabel(QStringLiteral("Semana de %1 a %2")
                             .arg(s.inicio.toString(QStringLiteral("dd/MM")), s.inicio.addDays(6).toString(QStringLiteral("dd/MM"))),
                         cab);
    QFont ft = t->font();
    ft.setWeight(QFont::Bold);
    t->setFont(ft);
    h->addWidget(t);
    const int total = s.total();
    if (s.critica()) {
        QStringList porque;
        if (s.livre > 0 && total >= 0.8 * s.livre) porque << QStringLiteral("%1% do livre").arg(100 * total / s.livre);
        if (s.provas.size() >= 3) porque << QStringLiteral("%1 provas").arg(s.provas.size());
        h->addWidget(pilula(QStringLiteral("! crítica · ") + porque.join(QStringLiteral(" · ")), "aviso", cab));
    }
    h->addStretch();
    auto* voltar = botaoLink(QStringLiteral("Voltar para esta semana"), cab);
    connect(voltar, &QPushButton::clicked, this, [this] {
        semanaEscolhida_ = -1;
        grafico_->escolher(-1);
        mostrarChecklist();
    });
    h->addWidget(voltar);
    areaChecklist_->addWidget(cab);

    auto* resumo = new QFrame(pai);
    resumo->setProperty("classe", QStringLiteral("linhaTopo"));
    auto* rh = new QHBoxLayout(resumo);
    rh->setContentsMargins(tema::esp(4) + 2, tema::esp(2) + 2, tema::esp(4) + 2, tema::esp(2) + 2);
    QString r = QStringLiteral("Livre %1 · planejado %2").arg(horas(s.livre), horas(total));
    if (s.livre > 0) r += QStringLiteral(" (%1%)").arg(100 * total / s.livre);
    if (total > s.livre) r += QStringLiteral(" · %1 acima do livre").arg(horas(total - s.livre));
    if (s.inicio <= d.hoje) r += QStringLiteral(" · feito %1").arg(horas(s.feito));
    rh->addWidget(rotulo(r, tema::Papel::Corpo, false, "nota", resumo));
    areaChecklist_->addWidget(resumo);

    for (const auto& p : entradas_.provas) {
        const QDate dp = paraQ(p.data);
        if (dp < s.inicio || dp > s.inicio.addDays(6)) continue;
        const int m = materiaDe(p.idTurma);
        const QColor c = m >= 0 ? tema::cor::materia(m) : tema::token("text-2");
        QHBoxLayout* dir = nullptr;
        auto* f = linhaItem(pai, imagem(losango(c, p.inferida), pai),
                            q(p.descricao) + QStringLiteral(" · ") +
                                (m >= 0 ? tituloMateria(q(entradas_.turmas[static_cast<size_t>(m)].second)) : q(p.turmaNome)),
                            QColor(), false, QStringLiteral("Prova"), dataHora(p.data), &dir);
        if (p.inferida) dir->addWidget(pilula(QStringLiteral("? deduzida — confirme"), "aviso", f));
        areaChecklist_->addWidget(f);
    }
    for (size_t k = 0; k < s.porMateria.size(); ++k) {
        if (s.porMateria[k] <= 0) continue;
        const std::string& id = entradas_.turmas[k].first;
        QString alvo = QStringLiteral("sem prova marcada");
        const planejamento::ProvaAlvo* melhor = nullptr;
        for (const auto& p : entradas_.provas) {
            if (p.idTurma == id && paraQ(p.data) >= s.inicio && (!melhor || paraQ(p.data) < paraQ(melhor->data))) melhor = &p;
        }
        if (melhor) alvo = QStringLiteral("alvo: ") + q(melhor->descricao) + QStringLiteral(" · ") + dataHora(melhor->data);
        QHBoxLayout* dir = nullptr;
        auto* f = linhaItem(pai, icone(QStringLiteral("◔"), "acento", pai), tituloMateria(q(entradas_.turmas[k].second)),
                            tema::cor::materia(static_cast<int>(k)), false, QStringLiteral("Estudo planejado"), alvo, &dir);
        dir->addWidget(rotulo(horas(s.porMateria[k]), tema::Papel::Corpo, false, "nota", f));
        areaChecklist_->addWidget(f);
    }
    if (total == 0) {
        auto* f = new QFrame(pai);
        f->setProperty("classe", QStringLiteral("linhaTopo"));
        auto* fh = new QHBoxLayout(f);
        fh->setContentsMargins(tema::esp(4) + 2, tema::esp(3), tema::esp(4) + 2, tema::esp(3));
        fh->addWidget(rotulo(QStringLiteral("Nenhum estudo planejado nesta semana."), tema::Papel::Corpo, false, "nota", f));
        areaChecklist_->addWidget(f);
    }
}

// ---------------------------------------------------------------------------
// Ao lado: preparo, focos, desempenho
// ---------------------------------------------------------------------------

void PainelPlanejamento::mostrarLateral() {
    const Dados& d = *dados_;
    auto cabecalho = [&](QVBoxLayout* area, const QString& titulo, const QString& sub) {
        QWidget* pai = area->parentWidget();
        auto* h = new QHBoxLayout;
        h->setContentsMargins(tema::esp(4), tema::esp(3) + 2, tema::esp(4), tema::esp(2) + 2);
        h->setSpacing(tema::esp(2));
        h->addWidget(tituloCartao(titulo, pai));
        if (!sub.isEmpty()) h->addWidget(rotulo(sub, tema::Papel::Legenda, false, "nota", pai), 1, Qt::AlignBottom);
        else h->addStretch();
        area->addLayout(h);
    };
    auto bloco = [&](QVBoxLayout* area) {
        auto* f = new QFrame(area->parentWidget());
        f->setProperty("classe", QStringLiteral("linhaTopo"));
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        v->setSpacing(tema::esp(2));
        area->addWidget(f);
        return v;
    };

    // --- preparo para as provas ---
    esvaziar(areaPreparo_);
    cabecalho(areaPreparo_, QStringLiteral("Preparo para as provas"), QStringLiteral("próximas 4"));
    std::vector<const planejamento::ProvaAlvo*> provas;
    for (const auto& p : entradas_.provas) {
        if (paraQ(p.data) >= d.hoje) provas.push_back(&p);
    }
    std::stable_sort(provas.begin(), provas.end(), [](auto* a, auto* b) { return paraQ(a->data) < paraQ(b->data); });
    if (provas.size() > 4) provas.resize(4);
    for (const auto* p : provas) {
        QVBoxLayout* v = bloco(areaPreparo_);
        QWidget* pai = v->parentWidget();
        const int m = materiaDe(p->idTurma);
        const QColor c = m >= 0 ? tema::cor::materia(m) : tema::token("text-2");
        const QString nome = m >= 0 ? q(entradas_.turmas[static_cast<size_t>(m)].second) : q(p->turmaNome);
        const std::string chave = p->idTurma + "|" + p->descricao;

        auto* topo = new QHBoxLayout;
        topo->setSpacing(tema::esp(2) + 2);
        topo->addWidget(imagem(losango(c, p->inferida), pai), 0, Qt::AlignTop);
        auto* tt = new QVBoxLayout;
        tt->setSpacing(1);
        tt->addWidget(puro(q(p->descricao) + QStringLiteral(" · ") + curtoMateria(nome), tema::Papel::Corpo, true, nullptr, pai));
        tt->addWidget(rotulo(dataHora(p->data), tema::Papel::Legenda, false, "nota", pai));
        topo->addLayout(tt, 1);
        const qint64 n = d.hoje.daysTo(paraQ(p->data));
        topo->addWidget(colorido(n <= 7 ? QStringLiteral("◷ ") + relativo(n) : relativo(n), n <= 7 ? "warn" : "text-2", n <= 7, pai),
                        0, Qt::AlignTop);
        v->addLayout(topo);

        const auto info = entradas_.infoProvas.find(chave);
        if (info != entradas_.infoProvas.end() && info->second.corrigida) {
            auto* h = new QHBoxLayout;
            h->setSpacing(tema::esp(2));
            h->addWidget(pilula(QStringLiteral("✓ você corrigiu"), "ok", pai));
            if (info->second.quandoSigaa.valid()) {
                h->addWidget(rotulo(QStringLiteral("SIGAA diz ") + diaCurto(paraQ(info->second.quandoSigaa)), tema::Papel::Legenda,
                                    false, "nota", pai));
            }
            h->addStretch();
            v->addLayout(h);
        }

        // Preparo: o feito (sessões e agentes) contra tudo o que o plano pede.
        int feito = 0, pendente = 0;
        for (const auto& s : plano_.sessoes) {
            if (s.chaveProva() != chave) continue;
            if (s.feita) feito += s.minutos;
            else if (paraQ(s.dia) >= d.hoje) pendente += s.minutos;
        }
        const auto ag = plano_.comAgente.find(chave);
        const int viaAgente = ag != plano_.comAgente.end() ? ag->second : 0;
        feito += viaAgente;
        const auto def = plano_.deficit.find(chave);
        const int nec = feito + pendente + (def != plano_.deficit.end() ? def->second : 0);
        const int pct = nec > 0 ? std::min(100, 100 * feito / nec) : 100;
        auto* barra = new QHBoxLayout;
        barra->setSpacing(tema::esp(2) + 2);
        auto* pb = new QProgressBar(pai);
        pb->setRange(0, 100);
        pb->setValue(pct);
        pb->setTextVisible(false);
        pb->setFixedHeight(6);
        pb->setAccessibleName(QStringLiteral("Preparo"));
        barra->addWidget(pb, 1);
        barra->addWidget(rotulo(QStringLiteral("%1 de %2 · %3%").arg(horas(feito), horas(nec)).arg(pct), tema::Papel::Legenda, false,
                                "nota", pai));
        v->addLayout(barra);
        if (viaAgente > 0) v->addWidget(colorido(QStringLiteral("✓ inclui %1 via agente de IA").arg(horas(viaAgente)), "ok", true, pai));

        // Os tópicos da matéria, com o que o agente marcou como foco em destaque.
        QString focoTopico;
        if (info != entradas_.infoProvas.end() && !info->second.topicos.empty()) {
            auto* tw = new QWidget(pai);
            auto* fl = new Fluxo(tw, 4, 4);
            int k = 0;
            for (const auto& t : info->second.topicos) {
                if (++k > 8) {
                    auto* mais = rotulo(QStringLiteral("+%1").arg(info->second.topicos.size() - 8), tema::Papel::Legenda, false,
                                        "nota", tw);
                    mais->setWordWrap(false);
                    fl->addWidget(mais);
                    break;
                }
                bool foco = false;
                for (const auto& f : d.focos) {
                    if (f.resolvido || f.idTurma != p->idTurma) continue;
                    const std::string a = util::dobrar(t), b = util::dobrar(f.topico);
                    if (a.find(b) != std::string::npos || b.find(a) != std::string::npos) foco = true;
                }
                if (foco && focoTopico.isEmpty()) focoTopico = q(t);
                auto* chip = puro(foco ? QStringLiteral("◎ ") + q(t) : q(t), tema::Papel::Legenda, foco, nullptr, tw);
                chip->setWordWrap(false);
                chip->setProperty("classe", QStringLiteral("pilula"));
                chip->setProperty("tom", foco ? QStringLiteral("aviso") : QStringLiteral("neutro"));
                chip->setMaximumWidth(330);
                fl->addWidget(chip);
            }
            v->addWidget(tw);
        } else {
            v->addWidget(rotulo(QStringLiteral("Tópicos: o professor ainda não registrou."), tema::Papel::Legenda, false, "nota", pai));
        }

        // Estudar com IA: prompts prontos para colar no agente.
        const bool aberto = iaAberta_.count(chave) > 0;
        auto* ia = botaoLink((aberto ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + QStringLiteral("Estudar com IA"), pai);
        connect(ia, &QPushButton::clicked, this, [this, chave] {
            if (iaAberta_.count(chave)) iaAberta_.erase(chave);
            else iaAberta_.insert(chave);
            mostrarLateral();
        });
        auto* lh = new QHBoxLayout;
        lh->addWidget(ia);
        lh->addStretch();
        v->addLayout(lh);
        if (aberto) {
            const QString mat = tituloMateria(nome);
            const QString onde = QStringLiteral(" de %1 (%2, %3). Use os tópicos e materiais do SIGAA Viewer.")
                                     .arg(mat, q(p->descricao), dataHora(p->data));
            QStringList acoes{QStringLiteral("Roteiro para a ") + q(p->descricao), QStringLiteral("Simulado de 8 questões")};
            if (!focoTopico.isEmpty()) acoes << QStringLiteral("Explicar ") + focoTopico;
            else if (info != entradas_.infoProvas.end() && !info->second.topicos.empty())
                acoes << QStringLiteral("Explicar ") + q(info->second.topicos.front());
            auto* aw = new QWidget(pai);
            auto* fl = new Fluxo(aw, 6, 6);
            auto* status = colorido(QString(), "ok", true, pai);
            status->hide();
            for (const QString& a : acoes) {
                auto* b = new QPushButton(a, aw);
                b->setProperty("papel", QStringLiteral("secundario"));
                b->setAutoDefault(false);
                b->setToolTip(QStringLiteral("Copia um pedido pronto para colar no seu agente"));
                const QString texto = a + onde;
                connect(b, &QPushButton::clicked, status, [status, texto] {
                    QGuiApplication::clipboard()->setText(texto);
                    status->setText(QStringLiteral("✓ Pedido copiado. Cole no seu agente de IA."));
                    status->show();
                });
                fl->addWidget(b);
            }
            v->addWidget(aw);
            v->addWidget(status);
        }
    }
    if (provas.empty()) {
        QVBoxLayout* v = bloco(areaPreparo_);
        v->addWidget(rotulo(QStringLiteral("Nenhuma prova pela frente."), tema::Papel::Corpo, false, "nota", v->parentWidget()));
    }

    // --- pontos de foco ---
    esvaziar(areaFocos_);
    cabecalho(areaFocos_, QStringLiteral("Pontos de foco"), d.focos.empty() ? QString() : QStringLiteral("marcados pelo agente"));
    for (const auto& f : d.focos) {
        QVBoxLayout* v = bloco(areaFocos_);
        QWidget* pai = v->parentWidget();
        const std::int64_t id = f.id;
        const int m = materiaDe(f.idTurma);
        if (f.resolvido) {
            auto* h = new QHBoxLayout;
            h->addWidget(colorido(QStringLiteral("✓"), "ok", true, pai));
            h->addWidget(puro(q(f.topico) + QStringLiteral(" · resolvido"), tema::Papel::Corpo, false, "nota", pai), 1);
            auto* desf = botaoLink(QStringLiteral("Desfazer"), pai);
            connect(desf, &QPushButton::clicked, this, [this, id] {
                store::Database db;
                db.reabrirFoco(id, QDateTime::currentSecsSinceEpoch());
                resolvidos_.erase(std::remove(resolvidos_.begin(), resolvidos_.end(), id), resolvidos_.end());
                replanejar();
            });
            h->addWidget(desf);
            v->addLayout(h);
            continue;
        }
        auto* h = new QHBoxLayout;
        h->setSpacing(tema::esp(2));
        if (m >= 0) h->addWidget(imagem(quadrado(tema::cor::materia(m)), pai));
        h->addWidget(puro(q(f.topico), tema::Papel::Corpo, true, nullptr, pai), 1);
        h->addWidget(colorido(nivelTxt(f.nivel), f.nivel >= 3 ? "danger" : f.nivel == 2 ? "warn" : "text-2", true, pai));
        v->addLayout(h);
        QString onde = m >= 0 ? tituloMateria(q(entradas_.turmas[static_cast<size_t>(m)].second)) : QString();
        for (const auto& p : entradas_.provas) {
            if (p.idTurma == f.idTurma && paraQ(p.data) >= d.hoje) {
                onde += QStringLiteral(" · ") + q(p.descricao) + QStringLiteral(" · ") + diaCurto(paraQ(p.data));
                break;
            }
        }
        if (!onde.isEmpty()) v->addWidget(puro(onde, tema::Papel::Legenda, false, "nota", pai));
        if (!f.motivo.empty()) v->addWidget(puro(q(f.motivo), tema::Papel::Corpo, false, nullptr, pai));
        auto* rodape = new QHBoxLayout;
        rodape->addWidget(puro(q(f.origem) + QStringLiteral(" · ") +
                                   QDateTime::fromSecsSinceEpoch(f.atualizadoEm).toString(QStringLiteral("dd/MM HH:mm")),
                               tema::Papel::Legenda, false, "nota", pai),
                          1);
        auto* res = new QPushButton(QStringLiteral("✓ Resolvido"), pai);
        res->setProperty("papel", QStringLiteral("secundario"));
        res->setAutoDefault(false);
        connect(res, &QPushButton::clicked, this, [this, id] {
            store::Database db;
            db.resolverFoco(id, "resolvido pelo aluno", QDateTime::currentSecsSinceEpoch());
            resolvidos_.push_back(id);
            replanejar();
        });
        rodape->addWidget(res);
        v->addLayout(rodape);
    }
    if (d.focos.empty()) {
        QVBoxLayout* v = bloco(areaFocos_);
        v->addWidget(rotulo(d.conectado ? QStringLiteral("Nenhum ponto de foco aberto.")
                                        : QStringLiteral("Sem agente conectado, não há pontos de foco. Eles aparecem quando "
                                                         "um agente corrige seus exercícios e encontra tópicos com dificuldade."),
                            tema::Papel::Corpo, false, "nota", v->parentWidget()));
    }

    // --- desempenho por tópico ---
    esvaziar(areaDesempenho_);
    cabecalho(areaDesempenho_, QStringLiteral("Desempenho por tópico"),
              d.desempenhos.empty() ? QString() : QStringLiteral("acertos registrados"));
    std::map<std::pair<std::string, std::string>, std::pair<int, int>> acum;
    std::map<std::pair<std::string, std::string>, std::string> nomeTopico;
    for (const auto& x : d.desempenhos) {
        const auto k = std::make_pair(x.idTurma, util::dobrar(x.topico));
        acum[k].first += x.acertos;
        acum[k].second += x.total;
        if (!nomeTopico.count(k)) nomeTopico[k] = x.topico;
    }
    for (const auto& [k, v2] : acum) {
        auto* f = new QFrame(areaDesempenho_->parentWidget());
        f->setProperty("classe", QStringLiteral("linhaTopo"));
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(tema::esp(4), tema::esp(2) + 2, tema::esp(4), tema::esp(2) + 2);
        v->setSpacing(tema::esp(1) + 2);
        const int pct = v2.second ? 100 * v2.first / v2.second : 0;
        const int m = materiaDe(k.first);
        auto* h = new QHBoxLayout;
        h->setSpacing(tema::esp(2));
        if (m >= 0) h->addWidget(imagem(quadrado(tema::cor::materia(m)), f));
        h->addWidget(puro(q(nomeTopico[k]), tema::Papel::Corpo, false, nullptr, f), 1);
        const QString glifo = pct < 50 ? QStringLiteral("✕") : pct < 70 ? QStringLiteral("!") : QStringLiteral("✓");
        h->addWidget(colorido(QStringLiteral("%1 %2/%3 · %4%").arg(glifo).arg(v2.first).arg(v2.second).arg(pct), tomDoAcerto(pct), true, f));
        v->addLayout(h);
        v->addWidget(new BarraAcerto(pct, tema::token(tomDoAcerto(pct)), f));
        areaDesempenho_->addWidget(f);
    }
    {
        QVBoxLayout* v = bloco(areaDesempenho_);
        v->addWidget(rotulo(acum.empty() ? QStringLiteral("Ainda não há desempenho registrado. Com um agente conectado, os "
                                                          "acertos dos exercícios aparecem por tópico.")
                                         : QStringLiteral("Marcas em 50% e 70%. Registrado pelos agentes de IA. Só os seus "
                                                          "dados aparecem aqui."),
                            acum.empty() ? tema::Papel::Corpo : tema::Papel::Legenda, false, "nota", v->parentWidget()));
    }
}

void PainelPlanejamento::marcar(const std::string& chave, bool feita) {
    store::Database db;
    if (!db.aberto() || !db.marcarSessaoEstudo(chave, feita, QDateTime::currentSecsSinceEpoch())) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"), QStringLiteral("Não consegui guardar a marcação."));
        return;
    }
    // Só a marca muda agora; o plano não é refeito debaixo do cursor. Ele se
    // reajusta da próxima vez que a página aparecer.
    for (auto& s : plano_.sessoes) {
        if (s.chave() == chave) s.feita = feita;
    }
    // Depois do clique: o botão clicado é destruído no redesenho.
    QTimer::singleShot(0, this, [this] { mostrar(); });
    if (aoMudar) aoMudar();
}

// ---------------------------------------------------------------------------
// Horas e dificuldade
// ---------------------------------------------------------------------------

namespace {

const char* const kDias[] = {"Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado", "Domingo"};

QString horasDecimais(int minutos) {
    return QString::number(minutos / 60.0, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')) +
           QStringLiteral(" h");
}

QString efeitoDaDificuldade(int i) {
    return i == 2 ? QStringLiteral("+50% de tempo") : i == 0 ? QStringLiteral("−30% de tempo")
                                                              : QStringLiteral("tempo padrão");
}

// "↑ pelo agente  Desfazer": escondido até o valor ser o do agente.
QWidget* marcaDoAgente(const QString& texto, QWidget* pai, QPushButton** desfazer) {
    auto* w = new QWidget(pai);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(tema::esp(1));
    h->addWidget(pilula(texto, "acento", w));
    *desfazer = botaoLink(QStringLiteral("Desfazer"), w);
    h->addWidget(*desfazer);
    w->hide();
    return w;
}

QFrame* linhaDeLista(QWidget* pai) {
    auto* f = new QFrame(pai);
    f->setProperty("classe", QStringLiteral("linha"));
    return f;
}

} // namespace

PainelDisponibilidade::PainelDisponibilidade(QWidget* pai) : QWidget(pai) {
    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(tema::esp(4));

    auto* cab = new QWidget(this);
    cab->setMaximumWidth(760);
    auto* ch = new QHBoxLayout(cab);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(tema::esp(5));
    auto* textos = new QVBoxLayout;
    textos->setSpacing(tema::esp(1));
    auto* titulo = new QLabel(QStringLiteral("Horas e dificuldade"), cab);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    textos->addWidget(titulo);
    textos->addWidget(rotulo(
        QStringLiteral("Quanto tempo você tem para a faculdade em cada dia, aulas e estudo juntos. O app "
                       "desconta as aulas da sua grade (cada horário conta %1 min) e planeja o estudo no "
                       "que sobra.")
            .arg(planejamento::kMinutosPorHoraAula),
        tema::Papel::Corpo, false, "nota", cab));
    ch->addLayout(textos, 1);
    // Sem botão Salvar, o aluno precisa ver que gravou.
    estado_ = new QLabel(QStringLiteral("Grava sozinho ao mudar"), cab);
    estado_->setProperty("classe", QStringLiteral("nota"));
    QFont fe = tema::fonte(tema::Papel::Legenda);
    fe.setWeight(QFont::DemiBold);
    estado_->setFont(fe);
    estado_->setAccessibleDescription(QStringLiteral("Estado da gravação"));
    ch->addWidget(estado_, 0, Qt::AlignTop);
    raiz->addWidget(cab);

    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    conteudo_ = new QWidget(rol);
    area_ = new QVBoxLayout(conteudo_);
    area_->setContentsMargins(0, 0, tema::esp(2), tema::esp(4));
    area_->setSpacing(tema::esp(5));
    rol->setWidget(conteudo_);
    raiz->addWidget(rol, 1);

    // Um intervalo curto antes de gravar: quatro cliques no + não precisam
    // de quatro escritas no banco e quatro replanos.
    adiar_ = new QTimer(this);
    adiar_->setSingleShot(true);
    adiar_->setInterval(400);
    connect(adiar_, &QTimer::timeout, this, &PainelDisponibilidade::salvar);
}

void PainelDisponibilidade::definirEntradas(const EntradasEstudo& e) {
    entradas_ = e;
    recarregar();
}

void PainelDisponibilidade::recarregar() {
    // Não remonta com uma edição pendente: grava antes, ou ela se perderia.
    if (adiar_->isActive()) salvar();
    store::Database db;
    if (db.aberto() && db.migrar()) prefs_ = db.carregarPreferenciasEstudo();
    montar();
}

void PainelDisponibilidade::montar() {
    esvaziar(area_);
    materias_.clear();
    dias_ = {};

    std::vector<estudo::Proposta> propostas;
    std::vector<estudo::PontoFoco> focos;
    std::vector<estudo::Desempenho> desempenhos;
    bool difVisivel = true;
    {
        store::Database db;
        if (db.aberto() && db.migrar()) {
            propostas = db.carregarPropostas();
            focos = db.carregarFocos({}, /*soAbertos=*/true);
            desempenhos = db.carregarDesempenho();
            difVisivel = mcp::modo(db, estudo::TipoProposta::Dificuldade) != mcp::Modo::NaoPode;
        }
    }

    // --- tempo por dia ---
    auto* horasBox = new QFrame(conteudo_);
    horasBox->setProperty("classe", QStringLiteral("cartao"));
    horasBox->setMaximumWidth(760);
    auto* hv = new QVBoxLayout(horasBox);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(0);
    {
        auto* cab = linhaDeLista(horasBox);
        auto* l = new QHBoxLayout(cab);
        l->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        l->addWidget(rotulo(QStringLiteral("Tempo disponível por dia"), tema::Papel::Subtitulo, true, nullptr, cab));
        hv->addWidget(cab);
    }
    for (int d = 0; d < 7; ++d) {
        const auto i = static_cast<size_t>(d);
        auto* linha = linhaDeLista(horasBox);
        auto* g = new QHBoxLayout(linha);
        g->setContentsMargins(tema::esp(4), tema::esp(2) + 2, tema::esp(4), tema::esp(2) + 2);
        g->setSpacing(tema::esp(4));
        auto* nome = rotulo(QString::fromUtf8(kDias[d]), tema::Papel::Corpo, true, nullptr, linha);
        nome->setFixedWidth(84);
        g->addWidget(nome);
        auto* passo = new Passo(0, 16 * 60, 30, horasDecimais, linha);
        passo->definir(prefs_.minutosPorDia[i]);
        passo->definirNome(QString::fromUtf8(kDias[d]) + QStringLiteral(": horas disponíveis"));
        g->addWidget(passo);
        auto* conta = new QLabel(linha);
        conta->setProperty("classe", QStringLiteral("nota"));
        conta->setWordWrap(true);
        g->addWidget(conta, 1);
        QPushButton* desfazer = nullptr;
        auto* marca = marcaDoAgente(QStringLiteral("↑ pelo agente"), linha, &desfazer);
        g->addWidget(marca);
        connect(desfazer, &QPushButton::clicked, this,
                [this, d] { desfazerDoAgente(static_cast<int>(estudo::TipoProposta::Horas), {}, d); });
        passo->aoMudar = [this, i, d](int v) {
            prefs_.minutosPorDia[i] = v;
            atualizarConta(d);
            atualizarTotal();
            estado_->setText(QStringLiteral("Gravando…"));
            estado_->setStyleSheet(QString());
            adiar_->start();
        };
        dias_[i] = {passo, conta, marca};
        atualizarConta(d);
        hv->addWidget(linha);
    }
    auto* rodape = new QFrame(horasBox);
    rodape->setProperty("classe", QStringLiteral("rodape"));
    auto* rl = new QHBoxLayout(rodape);
    rl->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
    total_ = rotulo(QString(), tema::Papel::Corpo, false, "nota", rodape);
    rl->addWidget(total_);
    hv->addWidget(rodape);
    atualizarTotal();
    area_->addWidget(horasBox);

    // --- dificuldade ---
    auto* difBox = new QFrame(conteudo_);
    difBox->setProperty("classe", QStringLiteral("cartao"));
    difBox->setMaximumWidth(760);
    auto* dv = new QVBoxLayout(difBox);
    dv->setContentsMargins(0, 0, 0, 0);
    dv->setSpacing(0);
    {
        auto* cab = linhaDeLista(difBox);
        auto* l = new QVBoxLayout(cab);
        l->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        l->setSpacing(2);
        l->addWidget(rotulo(QStringLiteral("Dificuldade de cada matéria"), tema::Papel::Subtitulo, true, nullptr, cab));
        l->addWidget(rotulo(QStringLiteral("Matéria difícil recebe 50% mais tempo de estudo; fácil, 30% menos."),
                            tema::Papel::Corpo, false, "nota", cab));
        dv->addWidget(cab);
    }
    for (const auto& [id, nomeTurma] : entradas_.turmas) {
        auto* linha = linhaDeLista(difBox);
        auto* v = new QVBoxLayout(linha);
        v->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        v->setSpacing(tema::esp(1));
        auto* h = new QHBoxLayout;
        h->setSpacing(tema::esp(4));
        auto* nomeL = rotulo(q(nomeTurma), tema::Papel::Corpo, true, nullptr, linha);
        h->addWidget(nomeL, 1);
        auto* escolha = new Segmentado({QStringLiteral("Fácil"), QStringLiteral("Média"), QStringLiteral("Difícil")}, linha);
        escolha->setAccessibleName(QStringLiteral("Dificuldade de ") + q(nomeTurma));
        const int atual = static_cast<int>(prefs_.dificuldadeDe(id)) - 1;
        escolha->definir(atual);
        h->addWidget(escolha);
        auto* efeito = new QLabel(efeitoDaDificuldade(atual), linha);
        efeito->setFixedWidth(110);
        efeito->setProperty("classe", atual == 1 ? QStringLiteral("nota") : QString());
        h->addWidget(efeito);
        v->addLayout(h);

        // O que pesa na escolha: focos abertos e acerto; e o que o agente
        // acha, se ele propôs.
        auto* extra = new QHBoxLayout;
        extra->setSpacing(tema::esp(3));
        int abertos = 0, ac = 0, tot = 0;
        for (const auto& f : focos) abertos += f.idTurma == id;
        for (const auto& x : desempenhos) {
            if (x.idTurma != id) continue;
            ac += x.acertos;
            tot += x.total;
        }
        QString resumo = abertos == 0   ? QStringLiteral("nenhum ponto de foco aberto")
                         : abertos == 1 ? QStringLiteral("1 ponto de foco aberto")
                                        : QStringLiteral("%1 pontos de foco abertos").arg(abertos);
        resumo += tot ? QStringLiteral(" · acerto %1%").arg(100 * ac / tot) : QStringLiteral(" · sem desempenho registrado");
        auto* info = new QLabel(resumo, linha);
        info->setProperty("classe", QStringLiteral("nota"));
        info->setFont(tema::fonte(tema::Papel::Legenda));
        extra->addWidget(info);
        for (const auto& p : propostas) {
            if (!difVisivel || p.tipo != estudo::TipoProposta::Dificuldade || p.idTurma != id ||
                p.estado != estudo::EstadoProposta::Pendente) {
                continue;
            }
            static const char* const kDif[] = {"", "Fácil", "Média", "Difícil"};
            auto* sug = new QLabel(QStringLiteral("● Agente sugere ") + QString::fromUtf8(kDif[std::clamp(p.para, 1, 3)]), linha);
            sug->setStyleSheet(QStringLiteral("color: %1").arg(tema::token("warn").name()));
            QFont fs = tema::fonte(tema::Papel::Legenda);
            fs.setWeight(QFont::Bold);
            sug->setFont(fs);
            extra->addWidget(sug);
            auto* ver = botaoLink(QStringLiteral("Ver proposta"), linha);
            connect(ver, &QPushButton::clicked, this, [this] {
                if (aoVerProposta) aoVerProposta();
            });
            extra->addWidget(ver);
            break;
        }
        QPushButton* desfazer = nullptr;
        auto* marca = marcaDoAgente(QStringLiteral("↑ mudado pelo agente"), linha, &desfazer);
        extra->addWidget(marca);
        const std::string idTurma = id;
        connect(desfazer, &QPushButton::clicked, this, [this, idTurma] {
            desfazerDoAgente(static_cast<int>(estudo::TipoProposta::Dificuldade), idTurma, -1);
        });
        extra->addStretch();
        v->addLayout(extra);

        escolha->aoMudar = [this, idTurma, efeito](int i) {
            const auto dif = static_cast<planejamento::Dificuldade>(i + 1);
            // Média é o padrão: só guarda o que difere dele.
            if (dif == planejamento::Dificuldade::Media) prefs_.dificuldade.erase(idTurma);
            else prefs_.dificuldade[idTurma] = dif;
            efeito->setText(efeitoDaDificuldade(i));
            efeito->setProperty("classe", i == 1 ? QStringLiteral("nota") : QString());
            efeito->style()->unpolish(efeito);
            efeito->style()->polish(efeito);
            estado_->setText(QStringLiteral("Gravando…"));
            estado_->setStyleSheet(QString());
            adiar_->start();
        };
        materias_.push_back({idTurma, escolha, efeito, marca});
        dv->addWidget(linha);
    }
    if (entradas_.turmas.empty()) {
        auto* vazio = new QFrame(difBox);
        auto* l = new QHBoxLayout(vazio);
        l->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        l->addWidget(rotulo(QStringLiteral("Nenhuma turma coletada ainda."), tema::Papel::Corpo, false, "nota", vazio));
        dv->addWidget(vazio);
    }
    area_->addWidget(difBox);
    area_->addStretch();
    atualizarMarcas();
}

void PainelDisponibilidade::atualizarConta(int d) {
    const auto i = static_cast<size_t>(d);
    if (!dias_[i].conta) return;
    // "− 2h45 de aula (3 horários) = 3h15 para estudar": a conta à vista,
    // para o desconto não ser mágica.
    const int aula = entradas_.aulas[i];
    const int estudo = planejamento::minutosParaEstudo(prefs_, entradas_.aulas, d);
    QString t;
    if (aula > 0) {
        t = QStringLiteral("− %1 de aula (%2 horário%3) = ")
                .arg(horas(aula))
                .arg(aula / planejamento::kMinutosPorHoraAula)
                .arg(aula / planejamento::kMinutosPorHoraAula == 1 ? QString() : QStringLiteral("s"));
    }
    const QString texto = tema::token("text").name();
    if (prefs_.minutosPorDia[i] < aula) {
        t += QStringLiteral("<b style='color:%1'>! as aulas já passam desse tempo</b>").arg(tema::token("warn").name());
    } else if (estudo == 0) {
        t += QStringLiteral("<b style='color:%1'>nada para estudar</b>").arg(texto);
    } else {
        t += QStringLiteral("<b style='color:%1'>%2 para estudar</b>").arg(texto, horas(estudo));
    }
    dias_[i].conta->setText(t);
}

void PainelDisponibilidade::atualizarTotal() {
    if (!total_) return;
    int disp = 0, aula = 0, est = 0;
    for (int d = 0; d < 7; ++d) {
        const auto i = static_cast<size_t>(d);
        disp += prefs_.minutosPorDia[i];
        aula += entradas_.aulas[i];
        est += planejamento::minutosParaEstudo(prefs_, entradas_.aulas, d);
    }
    total_->setText(QStringLiteral("Na semana: %1 disponíveis − %2 de aula = %3 para estudar")
                        .arg(horas(disp), horas(aula), horas(est)));
}

void PainelDisponibilidade::atualizarMarcas() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) return;
    const auto propostas = db.carregarPropostas();
    for (int d = 0; d < 7; ++d) {
        const auto& l = dias_[static_cast<size_t>(d)];
        if (!l.marca) continue;
        l.marca->setVisible(mcp::mudancaValendo(propostas, estudo::TipoProposta::Horas, {}, d,
                                                prefs_.minutosPorDia[static_cast<size_t>(d)])
                                .has_value());
    }
    for (const auto& m : materias_) {
        m.marca->setVisible(mcp::mudancaValendo(propostas, estudo::TipoProposta::Dificuldade, m.idTurma, -1,
                                                static_cast<int>(prefs_.dificuldadeDe(m.idTurma)))
                                .has_value());
    }
}

void PainelDisponibilidade::desfazerDoAgente(int tipo, const std::string& idTurma, int dia) {
    if (adiar_->isActive()) salvar();
    store::Database db;
    if (!db.aberto() || !db.migrar()) return;
    const auto t = static_cast<estudo::TipoProposta>(tipo);
    const int atual = t == estudo::TipoProposta::Horas ? prefs_.minutosPorDia[static_cast<size_t>(dia)]
                                                        : static_cast<int>(prefs_.dificuldadeDe(idTurma));
    auto p = mcp::mudancaValendo(db.carregarPropostas(), t, idTurma, dia, atual);
    if (!p || !mcp::desfazer(db, *p, QDateTime::currentSecsSinceEpoch())) return;
    recarregar();
    estado_->setText(QStringLiteral("✓ Desfeito"));
    estado_->setStyleSheet(QStringLiteral("color: %1").arg(tema::cor::sucesso().name()));
    if (aoSalvar) aoSalvar();
}

void PainelDisponibilidade::salvar() {
    adiar_->stop();
    store::Database db;
    if (!db.aberto() || !db.migrar() || !db.gravarPreferenciasEstudo(prefs_)) {
        estado_->setText(QStringLiteral("Não gravado"));
        estado_->setStyleSheet(QStringLiteral("color: %1").arg(tema::cor::atrasado().name()));
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar as preferências: %1")
                                 .arg(q(db.erro())));
        return;
    }
    estado_->setText(QStringLiteral("✓ Gravado"));
    estado_->setStyleSheet(QStringLiteral("color: %1").arg(tema::cor::sucesso().name()));
    // Mexeu no valor que o agente pôs: a marca dele sai.
    atualizarMarcas();
    if (aoSalvar) aoSalvar();
}

// ---------------------------------------------------------------------------
// A aba
// ---------------------------------------------------------------------------

namespace {

constexpr int PapelContador = Qt::UserRole + 10;

// O item do menu com o contador à direita ("Progresso  2"): a pílula é
// pintada aqui, por cima do item que a folha de estilo já desenhou.
class ItemDoMenu : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const override {
        QStyledItemDelegate::paint(p, opt, i);
        const int n = i.data(PapelContador).toInt();
        if (n <= 0) return;
        const QString t = QString::number(n);
        QFont f = opt.font;
        f.setWeight(QFont::Bold);
        f.setPointSizeF(f.pointSizeF() * 0.85);
        const QFontMetrics fm(f);
        const int alt = fm.height() + 4;
        const int larg = std::max(alt, fm.horizontalAdvance(t) + 14);
        const QRect r(opt.rect.right() - larg - 12, opt.rect.center().y() - alt / 2, larg, alt);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(tema::token("warn-soft"));
        p->drawRoundedRect(r, alt / 2.0, alt / 2.0);
        p->setFont(f);
        p->setPen(tema::token("warn"));
        p->drawText(r, Qt::AlignCenter, t);
        p->restore();
    }
};

} // namespace

PainelEstudo::PainelEstudo(QString banco, QString materiais, QWidget* pai)
    : QWidget(pai), banco_(banco) {
    auto* raiz = new QHBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(0);

    menu_ = new QListWidget(this);
    menu_->setObjectName(QStringLiteral("menuEstudo"));
    menu_->setFixedWidth(220);
    menu_->setFrameShape(QFrame::NoFrame);
    menu_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    menu_->setItemDelegate(new ItemDoMenu(menu_));
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
    v->setContentsMargins(tema::esp(8), tema::esp(6), tema::esp(6), tema::esp(4));
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
    planejamento_->aoPedirAgentes = [this](bool conectar) {
        menu_->setCurrentRow(3);
        if (conectar) agentes_->mostrarConexao();
        else agentes_->mostrarPermissoes();
    };
    planejamento_->aoTratarProva = [this](const std::string& id, const std::string& desc, bool corrigir) {
        if (aoTratarProva) aoTratarProva(id, desc, corrigir);
    };
    // Horas ou dificuldade novas mudam o plano.
    disponibilidade_->aoSalvar = [this] { sincronizar(disponibilidade_); };
    disponibilidade_->aoVerProposta = [this] { menu_->setCurrentRow(1); };
    // Sem agente conectado, o Progresso oferece o caminho até a conexão.
    progresso_->aoPedirConexao = [this] { mostrarAgentes(); };
    progresso_->aoPedirPermissoes = [this] {
        menu_->setCurrentRow(3);
        agentes_->mostrarPermissoes();
    };
    // Proposta aceita, foco resolvido, registro apagado: o plano muda.
    progresso_->aoMudar = [this] { sincronizar(progresso_); };
    // Um modo trocado pode aplicar as pendentes.
    agentes_->aoMudar = [this] { sincronizar(agentes_); };
    atualizarContador();
}

void PainelEstudo::sincronizar(QWidget* origem) {
    if (origem != disponibilidade_) disponibilidade_->recarregar();
    if (origem != progresso_ && progresso_->isVisible()) progresso_->atualizar();
    // Replanejar grava e avisa a Agenda (aoMudar do planejamento); sem plano
    // ainda, a Agenda relê do mesmo jeito.
    planejamento_->replanejarSeExistir();
    if (aoMudarPlano) aoMudarPlano();
    atualizarContador();
}

void PainelEstudo::atualizarContador() {
    store::Database db(banco_.toStdString());
    const int n = db.aberto() && db.migrar() ? mcp::pendentesVisiveis(db) : 0;
    QListWidgetItem* it = menu_->item(1);
    it->setData(PapelContador, n);
    it->setData(Qt::AccessibleTextRole,
                n ? QStringLiteral("Progresso, %1 proposta(s) esperando você").arg(n) : QStringLiteral("Progresso"));
    it->setToolTip(n ? QStringLiteral("%1 proposta(s) do agente esperando sua resposta").arg(n) : QString());
}

void PainelEstudo::definirEntradas(const EntradasEstudo& e) {
    planejamento_->definirEntradas(e);
    disponibilidade_->definirEntradas(e);
    // Recarga do banco (um sync, ou um agente que gravou): o Progresso à
    // vista relê; escondido, relê ao aparecer.
    progresso_->definirEntradas(e);
    atualizarContador();
}

void PainelEstudo::mostrarPlanejamento() { menu_->setCurrentRow(0); }

void PainelEstudo::mostrarAgentes() {
    menu_->setCurrentRow(3);
    agentes_->mostrarConexao();
}

} // namespace sigaa::ui
