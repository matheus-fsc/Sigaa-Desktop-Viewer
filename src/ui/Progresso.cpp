#include "ui/Progresso.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

#include "core/planejamento/Planejamento.h"
#include "core/store/Database.h"
#include "core/util/Texto.h"
#include "mcp/Propostas.h"
#include "ui/Controles.h"
#include "ui/Tema.h"

namespace sigaa::ui {

using estudo::EstadoProposta;
using estudo::Proposta;
using estudo::TipoProposta;

struct PainelProgresso::Dados {
    std::map<std::string, QString> nomes;
    std::vector<estudo::RegistroEstudo> estudos;
    std::vector<estudo::Desempenho> desempenhos;
    std::vector<estudo::PontoFoco> focos;
    std::vector<Proposta> propostas;
    std::vector<planejamento::Sessao> sessoes;
    planejamento::Preferencias prefs;
    std::map<TipoProposta, mcp::Modo> modos;
    int teto{8};
    QDate hoje;

    QString nome(const std::string& id) const {
        const auto it = nomes.find(id);
        return it == nomes.end() ? QString::fromStdString(id) : it->second;
    }
};

namespace {

constexpr int PapelTabela = Qt::UserRole + 1;
constexpr int PapelId = Qt::UserRole + 2;

QString q(const std::string& s) { return QString::fromStdString(s); }
QString dur(int minutos) { return q(planejamento::duracao(minutos)); }
QDate paraQ(const DateTime& d) { return d.valid() ? QDate(d.year, d.month, d.day) : QDate(); }
std::int64_t agora() { return QDateTime::currentSecsSinceEpoch(); }

QString quandoCurto(std::int64_t t) {
    return QDateTime::fromSecsSinceEpoch(t).toString(QStringLiteral("dd/MM HH:mm"));
}

// "ter 06/10"
QString diaCurto(QDate d) {
    return QLocale(QLocale::Portuguese, QLocale::Brazil)
        .toString(d, QStringLiteral("ddd dd/MM"))
        .remove(QLatin1Char('.'));
}

QString horasTxt(int minutos) {
    return QString::number(minutos / 60.0, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')) +
           QStringLiteral(" h");
}

const char* const kDificuldade[] = {"", "Fácil", "Média", "Difícil"};
const char* const kNivel[] = {"", "atenção", "dificuldade", "crítico"};
const char* const kDia[] = {"segunda", "terça", "quarta", "quinta", "sexta", "sábado", "domingo"};

QString difTxt(int v) { return QString::fromUtf8(kDificuldade[std::clamp(v, 1, 3)]); }
QString nivelTxt(int v) { return QString::fromUtf8(kNivel[std::clamp(v, 1, 3)]); }
// "no sábado", "na segunda"
QString noDia(int d) {
    const QString n = QString::fromUtf8(kDia[std::clamp(d, 0, 6)]);
    return (d >= 5 ? QStringLiteral("no ") : QStringLiteral("na ")) + n;
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

QLabel* secao(const QString& t, QWidget* pai) {
    return rotulo(t.toUpper(), tema::Papel::Legenda, true, "secao", pai);
}

// Texto do agente: nunca interpretado como HTML.
QLabel* puro(const QString& texto, QWidget* pai) {
    auto* l = new QLabel(pai);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    l->setText(texto);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QLabel* colorido(const QString& texto, const char* token, bool negrito, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setStyleSheet(QStringLiteral("color: %1").arg(tema::token(token).name()));
    if (negrito) {
        QFont f = l->font();
        f.setWeight(QFont::Bold);
        l->setFont(f);
    }
    return l;
}

QPushButton* botao(const QString& texto, const char* papel, QWidget* pai) {
    auto* b = new QPushButton(texto, pai);
    b->setAutoDefault(false);
    b->setProperty("papel", QString::fromLatin1(papel));
    return b;
}

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

QTableWidget* tabela(const QStringList& colunas, QWidget* pai) {
    auto* t = new QTableWidget(0, static_cast<int>(colunas.size()), pai);
    t->setHorizontalHeaderLabels(colunas);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setShowGrid(false);
    t->setWordWrap(false);
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    t->horizontalHeader()->setStretchLastSection(true);
    tema::ajustarLista(t);
    return t;
}

// A tabela mora numa página que rola: sem altura fixa ela pediria a altura
// de uma tela e empurraria o resto para baixo do nada.
void ajustarAltura(QTableWidget* t, int maxLinhas) {
    int h = t->horizontalHeader()->sizeHint().height();
    const int n = std::max(1, std::min(t->rowCount(), maxLinhas));
    h += n * t->verticalHeader()->defaultSectionSize();
    t->setFixedHeight(h + 2 * t->frameWidth() + 2);
}

// As duas tabelas lado a lado dividem ~1100 px: a coluna que importa
// (`estica`) fica com a sobra, e a da turma — nomes como "ANÁLISE E
// DESENVOLVIMENTO DE SOFTWARE IV" — tem largura fixa e corta com reticências
// (o nome inteiro vai na dica). Medida pelo conteúdo, ela engolia o tópico.
void larguras(QTableWidget* t, int estica, int turma) {
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->setTextElideMode(Qt::ElideRight);
    t->horizontalHeader()->setStretchLastSection(false);
    for (int col = 0; col < t->columnCount(); ++col) {
        t->horizontalHeader()->setSectionResizeMode(
            col, col == estica ? QHeaderView::Stretch
                 : col == turma ? QHeaderView::Fixed
                                : QHeaderView::ResizeToContents);
    }
    t->setColumnWidth(turma, 130);
    for (int r = 0; r < t->rowCount(); ++r) {
        if (auto* it = t->item(r, turma)) it->setToolTip(it->text());
    }
}

QTableWidgetItem* celula(const QString& s, const char* cor = nullptr, bool negrito = false) {
    auto* it = new QTableWidgetItem(s);
    if (cor) it->setForeground(tema::token(cor));
    if (negrito) {
        QFont f = it->font();
        f.setWeight(QFont::Bold);
        it->setFont(f);
    }
    return it;
}

// Acerto: a cor e um símbolo, para a cor não ser o único sinal.
const char* tomDoAcerto(int pct) { return pct < 50 ? "danger" : pct < 70 ? "warn" : "ok"; }
QString simboloDoAcerto(int pct) {
    return pct < 50 ? QStringLiteral("✕") : pct < 70 ? QStringLiteral("!") : QStringLiteral("✓");
}

// A próxima prova da turma depois de `d` (exclusive), ou nula.
const planejamento::ProvaAlvo* proximaProva(const std::vector<planejamento::ProvaAlvo>& provas,
                                            const std::string& idTurma, QDate d) {
    const planejamento::ProvaAlvo* melhor = nullptr;
    for (const auto& p : provas) {
        const QDate dp = paraQ(p.data);
        if (p.idTurma != idTurma || !dp.isValid() || dp <= d) continue;
        if (!melhor || dp < paraQ(melhor->data)) melhor = &p;
    }
    return melhor;
}

QString nomeDaProva(const planejamento::ProvaAlvo& p) {
    return q(p.descricao) + QStringLiteral(" (") + diaCurto(paraQ(p.data)) + QStringLiteral(")");
}

} // namespace

// ---------------------------------------------------------------------------

PainelProgresso::PainelProgresso(QString banco, QWidget* pai)
    : QWidget(pai), banco_(std::move(banco)) {
    auto* fora = new QVBoxLayout(this);
    fora->setContentsMargins(0, 0, 0, 0);
    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    fora->addWidget(rol);
    auto* pagina = new QWidget(rol);
    rol->setWidget(pagina);
    auto* raiz = new QVBoxLayout(pagina);
    raiz->setContentsMargins(0, 0, tema::esp(3), tema::esp(4));
    raiz->setSpacing(tema::esp(5));

    {
        auto* cab = new QVBoxLayout;
        cab->setSpacing(tema::esp(1));
        auto* titulo = new QLabel(QStringLiteral("Progresso"), pagina);
        QFont ft = tema::fonte(tema::Papel::Subtitulo);
        ft.setWeight(QFont::Bold);
        titulo->setFont(ft);
        cab->addWidget(titulo);
        resumo_ = rotulo(QString(), tema::Papel::Corpo, false, "nota", pagina);
        cab->addWidget(resumo_);
        raiz->addLayout(cab);
    }

    // --- estado vazio ---
    vazio_ = new QWidget(pagina);
    {
        auto* v = new QVBoxLayout(vazio_);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(tema::esp(3));
        auto* t = rotulo(QStringLiteral("Aqui aparece o que você estudou com um agente de IA: o tempo de "
                                        "cada sessão, quantas questões acertou em cada tópico e os "
                                        "pontos em que ele achou que você precisa focar. Ele também "
                                        "pode propor mudanças no seu plano, que você aceita ou não. "
                                        "Nada disso sai do SIGAA; quem registra é o agente, com a sua "
                                        "permissão."),
                         tema::Papel::Corpo, false, "recado", vazio_);
        t->setMaximumWidth(720);
        v->addWidget(t);
        auto* b = botao(QStringLiteral("Conectar um agente"), "primario", vazio_);
        connect(b, &QPushButton::clicked, this, [this] {
            if (aoPedirConexao) aoPedirConexao();
        });
        auto* linha = new QHBoxLayout;
        linha->addWidget(b);
        linha->addStretch();
        v->addLayout(linha);
    }
    raiz->addWidget(vazio_);

    conteudo_ = new QWidget(pagina);
    auto* c = new QVBoxLayout(conteudo_);
    c->setContentsMargins(0, 0, 0, 0);
    c->setSpacing(tema::esp(6));

    // --- propostas ---
    {
        auto* s = new QVBoxLayout;
        s->setSpacing(tema::esp(2) + 2);
        auto* cab = new QHBoxLayout;
        cab->setSpacing(tema::esp(3));
        auto* titulo = secao(QStringLiteral("Propostas do agente"), conteudo_);
        titulo->setWordWrap(false);
        cab->addWidget(titulo);
        pendentes_ = new QLabel(conteudo_);
        pendentes_->setStyleSheet(QStringLiteral("color: %1").arg(tema::token("warn").name()));
        QFont fp = pendentes_->font();
        fp.setWeight(QFont::Bold);
        pendentes_->setFont(fp);
        cab->addWidget(pendentes_);
        modos_ = rotulo(QString(), tema::Papel::Legenda, false, "nota", conteudo_);
        modos_->setWordWrap(false);
        cab->addWidget(modos_);
        auto* alterar = botaoLink(QStringLiteral("Alterar o que ele pode mudar"), conteudo_);
        connect(alterar, &QPushButton::clicked, this, [this] {
            if (aoPedirPermissoes) aoPedirPermissoes();
        });
        cab->addWidget(alterar);
        cab->addStretch();
        s->addLayout(cab);
        areaPropostas_ = new QVBoxLayout;
        areaPropostas_->setSpacing(tema::esp(2) + 2);
        s->addLayout(areaPropostas_);
        escondidas_ = rotulo(QString(), tema::Papel::Legenda, false, "nota", conteudo_);
        s->addWidget(escondidas_);
        c->addLayout(s);
    }

    // --- métricas ---
    {
        auto* s = new QVBoxLayout;
        s->setSpacing(tema::esp(2) + 2);
        s->addWidget(secao(QStringLiteral("Métricas"), conteudo_));
        areaKpis_ = new QHBoxLayout;
        areaKpis_->setSpacing(tema::esp(3));
        s->addLayout(areaKpis_);
        porMateria_ = tabela({QStringLiteral("Matéria"), QStringLiteral("Dificuldade"),
                              QStringLiteral("Estudado · 7 dias"), QStringLiteral("Falta até a prova"),
                              QStringLiteral("Acerto"), QStringLiteral("Tendência")},
                             conteudo_);
        porMateria_->setSelectionMode(QAbstractItemView::NoSelection);
        porMateria_->setAccessibleName(QStringLiteral("Por matéria"));
        s->addWidget(porMateria_);
        c->addLayout(s);
    }

    // --- focos ---
    {
        auto* s = new QVBoxLayout;
        s->setSpacing(tema::esp(2) + 2);
        s->addWidget(secao(QStringLiteral("Pontos de foco"), conteudo_));
        areaFocos_ = new QVBoxLayout;
        areaFocos_->setSpacing(tema::esp(2));
        s->addLayout(areaFocos_);
        c->addLayout(s);
    }

    // --- desempenho | histórico ---
    {
        auto* colunas = new QHBoxLayout;
        colunas->setSpacing(tema::esp(5));
        auto* esq = new QVBoxLayout;
        esq->setSpacing(tema::esp(2) + 2);
        esq->addWidget(secao(QStringLiteral("Desempenho por tópico"), conteudo_));
        desempenho_ = tabela({QStringLiteral("Tópico"), QStringLiteral("Turma"),
                              QStringLiteral("Acertos"), QStringLiteral("%")},
                             conteudo_);
        esq->addWidget(desempenho_);
        esq->addWidget(rotulo(QStringLiteral("Acertos somados de todos os registros do tópico. ✕ abaixo "
                                             "de 50%, ! abaixo de 70%."),
                              tema::Papel::Legenda, false, "nota", conteudo_));
        esq->addStretch();

        auto* dir = new QVBoxLayout;
        dir->setSpacing(tema::esp(2) + 2);
        dir->addWidget(secao(QStringLiteral("Histórico"), conteudo_));
        historico_ = tabela({QStringLiteral("Quando"), QStringLiteral("O quê"), QStringLiteral("Turma"),
                             QStringLiteral("Detalhe"), QStringLiteral("Agente")},
                            conteudo_);
        dir->addWidget(historico_);
        auto* linha = new QHBoxLayout;
        apagarSel_ = botao(QStringLiteral("Apagar o selecionado"), "secundario", conteudo_);
        apagarSel_->setEnabled(false);
        auto* apagarOrigem = botao(QStringLiteral("Apagar tudo de um agente…"), "secundario", conteudo_);
        apagarOrigem->setCheckable(true);
        linha->addWidget(apagarSel_);
        linha->addWidget(apagarOrigem);
        linha->addStretch();
        dir->addLayout(linha);
        perigo_ = new QFrame(conteudo_);
        perigo_->setProperty("classe", QStringLiteral("perigo"));
        {
            auto* v = new QVBoxLayout(perigo_);
            v->setContentsMargins(tema::esp(3), tema::esp(2) + 2, tema::esp(3), tema::esp(2) + 2);
            v->addWidget(new QLabel(QStringLiteral("Apaga estudo, desempenho e foco registrados por:"), perigo_));
            chips_ = new QHBoxLayout;
            chips_->setSpacing(tema::esp(2));
            v->addLayout(chips_);
        }
        perigo_->hide();
        dir->addWidget(perigo_);
        dir->addStretch();
        connect(apagarOrigem, &QPushButton::toggled, perigo_, &QWidget::setVisible);
        connect(historico_, &QTableWidget::itemSelectionChanged, this,
                [this] { apagarSel_->setEnabled(historico_->currentRow() >= 0 && !historico_->selectedItems().isEmpty()); });
        connect(apagarSel_, &QPushButton::clicked, this, [this] {
            const int r = historico_->currentRow();
            if (r < 0) return;
            const auto* it = historico_->item(r, 0);
            store::Database db(banco_.toStdString());
            db.apagarDoAgente(static_cast<store::Database::TabelaAgente>(it->data(PapelTabela).toInt()),
                              it->data(PapelId).toLongLong());
            mudou();
        });
        colunas->addLayout(esq, 2);
        colunas->addLayout(dir, 3);
        c->addLayout(colunas);
    }
    c->addStretch();
    raiz->addWidget(conteudo_);
    raiz->addStretch();
}

void PainelProgresso::definirEntradas(const EntradasEstudo& e) {
    entradas_ = e;
    if (isVisible()) atualizar();
}

void PainelProgresso::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    atualizar();
}

void PainelProgresso::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    // O "Desfazer" de um foco fechado e o ajuste aberto são desta visita.
    fechados_.clear();
    ajustando_ = 0;
    ajuste_.clear();
}

void PainelProgresso::mudou() {
    atualizar();
    if (aoMudar) aoMudar();
}

void PainelProgresso::atualizar() {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar()) return;
    Dados d;
    for (const auto& t : db.carregarUltimo().turmas) d.nomes[t.idTurma] = q(t.nome);
    d.estudos = db.carregarRegistrosEstudo();
    d.desempenhos = db.carregarDesempenho();
    d.focos = db.carregarFocos();
    d.propostas = db.carregarPropostas();
    d.sessoes = db.carregarSessoesEstudo();
    d.prefs = db.carregarPreferenciasEstudo();
    for (auto t : {TipoProposta::Horas, TipoProposta::Dificuldade, TipoProposta::Sessao, TipoProposta::Foco}) {
        d.modos[t] = mcp::modo(db, t);
    }
    d.teto = mcp::tetoHoras(db);
    d.hoje = QDate::currentDate();

    const bool nada = d.estudos.empty() && d.desempenhos.empty() && d.focos.empty() &&
                      d.propostas.empty() && fechados_.empty();
    vazio_->setVisible(nada);
    conteudo_->setVisible(!nada);

    // Resumo: tempo nos últimos 7 dias, por turma.
    std::map<std::string, int> semana;
    int total = 0;
    for (const auto& r : d.estudos) {
        if (paraQ(r.quando).daysTo(d.hoje) < 7) {
            semana[r.idTurma] += r.minutos;
            total += r.minutos;
        }
    }
    QStringList partes;
    for (const auto& [id, m] : semana) partes << d.nome(id) + QStringLiteral(" ") + dur(m);
    int abertos = 0;
    for (const auto& f : d.focos) abertos += f.resolvido ? 0 : 1;
    resumo_->setVisible(!nada);
    resumo_->setText(QStringLiteral("%1 de estudo com agentes nos últimos 7 dias%2 · %3 ponto(s) de foco aberto(s)")
                         .arg(dur(total))
                         .arg(partes.isEmpty() ? QString()
                                               : QStringLiteral(" (") + partes.join(QStringLiteral(", ")) +
                                                     QStringLiteral(")"))
                         .arg(abertos));
    if (nada) return;

    mostrarPropostas(d);
    mostrarMetricas(d);
    mostrarFocos(d);
    mostrarDesempenho(d);
    mostrarHistorico(d);
}

// ---------------------------------------------------------------------------
// Propostas
// ---------------------------------------------------------------------------

void PainelProgresso::mostrarPropostas(const Dados& d) {
    esvaziar(areaPropostas_);
    int pend = 0, escondidas = 0, respondidas = 0;
    std::vector<const Proposta*> lista;
    // Pendentes primeiro; depois as respondidas nos últimos 7 dias, que ainda
    // dão para desfazer ou reabrir daqui. As mais velhas ficam no Histórico.
    for (const auto& p : d.propostas) {
        if (p.estado != EstadoProposta::Pendente) continue;
        if (d.modos.at(p.tipo) == mcp::Modo::NaoPode) {
            ++escondidas;
            continue;
        }
        ++pend;
        lista.push_back(&p);
    }
    const std::int64_t limite = agora() - 7 * 86400;
    for (const auto& p : d.propostas) {
        if (p.estado == EstadoProposta::Pendente || p.respondidaEm < limite) continue;
        if (++respondidas > 6) break;
        lista.push_back(&p);
    }
    for (const Proposta* p : lista) areaPropostas_->addWidget(cartaoProposta(*p, d));
    if (lista.empty()) {
        areaPropostas_->addWidget(rotulo(QStringLiteral("Nenhuma proposta por enquanto. Quando o agente "
                                                        "quiser mudar suas horas, a dificuldade de uma "
                                                        "matéria ou encaixar uma revisão, aparece aqui."),
                                         tema::Papel::Corpo, false, "nota", conteudo_));
    }

    pendentes_->setVisible(pend > 0);
    pendentes_->setText(QStringLiteral("● %1 esperando você").arg(pend));

    static const std::pair<TipoProposta, const char*> kNomes[] = {
        {TipoProposta::Horas, "horas"},
        {TipoProposta::Dificuldade, "dificuldade"},
        {TipoProposta::Sessao, "sessões extras"},
        {TipoProposta::Foco, "pontos de foco"}};
    QStringList aprova, aplica;
    for (const auto& [t, n] : kNomes) {
        if (d.modos.at(t) == mcp::Modo::Propoe) aprova << QString::fromUtf8(n);
        if (d.modos.at(t) == mcp::Modo::Aplica) aplica << QString::fromUtf8(n);
    }
    QStringList m;
    if (!aprova.isEmpty()) m << QStringLiteral("Você aprova: ") + aprova.join(QStringLiteral(", "));
    if (!aplica.isEmpty()) m << QStringLiteral("aplica e avisa: ") + aplica.join(QStringLiteral(", "));
    modos_->setText(m.join(QStringLiteral(" · ")));

    escondidas_->setVisible(escondidas > 0);
    escondidas_->setText(QStringLiteral("%1 proposta(s) escondida(s), porque você não permite esse tipo "
                                        "de mudança.")
                             .arg(escondidas));
}

QWidget* PainelProgresso::cartaoProposta(const Proposta& p, const Dados& d) {
    const bool pend = p.estado == EstadoProposta::Pendente;
    const int valor = pend && ajuste_.count(p.id) ? ajuste_.at(p.id) : p.para;
    const bool overTeto = pend && p.tipo == TipoProposta::Horas && valor > d.teto * 60;
    const bool ajustando = pend && ajustando_ == p.id;

    auto* cartao = new QFrame(conteudo_);
    cartao->setProperty("classe", pend ? QStringLiteral("cartaoForte") : QStringLiteral("cartaoApagado"));
    auto* v = new QVBoxLayout(cartao);
    v->setContentsMargins(tema::esp(4), tema::esp(3) + 2, tema::esp(4), tema::esp(3) + 2);
    v->setSpacing(tema::esp(2) + 2);
    auto* linha = new QHBoxLayout;
    linha->setSpacing(tema::esp(4));
    auto* texto = new QVBoxLayout;
    texto->setSpacing(3);

    // O que muda, de quanto para quanto, e o efeito no plano.
    QString tipo, titulo, efeito;
    const QDate hoje = d.hoje;
    switch (p.tipo) {
        case TipoProposta::Dificuldade: {
            tipo = QStringLiteral("Dificuldade");
            titulo = QStringLiteral("Dificuldade de %1: %2 → %3").arg(d.nome(p.idTurma), difTxt(p.de), difTxt(valor));
            const auto* prova = proximaProva(entradas_.provas, p.idTurma, hoje.addDays(-1));
            if (!prova) {
                efeito = QStringLiteral("muda o peso da matéria nas próximas provas");
            } else {
                const int delta =
                    planejamento::minutosNecessarios(*prova, static_cast<planejamento::Dificuldade>(std::clamp(valor, 1, 3))) -
                    planejamento::minutosNecessarios(*prova, static_cast<planejamento::Dificuldade>(std::clamp(p.de, 1, 3)));
                efeito = delta == 0 ? QStringLiteral("nenhuma mudança no plano")
                                    : QStringLiteral("%1%2 de estudo até a %3")
                                          .arg(delta > 0 ? QStringLiteral("+") : QStringLiteral("−"),
                                               dur(std::abs(delta)), nomeDaProva(*prova));
            }
            break;
        }
        case TipoProposta::Horas: {
            tipo = QStringLiteral("Horas por dia");
            titulo = QStringLiteral("Tempo disponível %1: %2 → %3").arg(noDia(p.diaSemana), horasTxt(p.de), horasTxt(valor));
            const int aula = p.diaSemana >= 0 ? entradas_.aulas[static_cast<size_t>(p.diaSemana)] : 0;
            const int delta = std::max(0, valor - aula) - std::max(0, p.de - aula);
            efeito = delta == 0 ? QStringLiteral("nenhuma mudança no tempo de estudo (as aulas já tomam o dia)")
                                : QStringLiteral("%1%2 para estudar toda semana %3")
                                      .arg(delta > 0 ? QStringLiteral("+") : QStringLiteral("−"),
                                           dur(std::abs(delta)), noDia(p.diaSemana));
            break;
        }
        case TipoProposta::Sessao: {
            tipo = QStringLiteral("Sessão extra");
            titulo = QStringLiteral("Revisão de %1 (%2): +%3 %4")
                         .arg(q(p.topico), d.nome(p.idTurma), dur(valor), QStringLiteral("em ") + diaCurto(paraQ(p.dia)));
            const auto* prova = proximaProva(entradas_.provas, p.idTurma, paraQ(p.dia));
            efeito = prova ? QStringLiteral("%1 de revisão antes da %2").arg(dur(valor), nomeDaProva(*prova))
                           : QStringLiteral("%1 de revisão; nenhuma prova da turma depois desse dia").arg(dur(valor));
            break;
        }
        case TipoProposta::Foco: {
            tipo = QStringLiteral("Ponto de foco");
            titulo = QStringLiteral("%1 (%2): %3").arg(q(p.topico), d.nome(p.idTurma), nivelTxt(valor));
            const auto* prova = proximaProva(entradas_.provas, p.idTurma, hoje.addDays(-1));
            efeito = QStringLiteral("+%1 de estudo%2").arg(dur(30 * std::clamp(valor, 1, 3)),
                                                          prova ? QStringLiteral(" para a ") + nomeDaProva(*prova)
                                                                : QString());
            break;
        }
    }
    texto->addWidget(rotulo(tipo.toUpper(), tema::Papel::Legenda, true, "secao", cartao));
    // O título cita o tópico que o agente escreveu: texto puro.
    auto* t = puro(titulo, cartao);
    QFont ft = tema::fonte(tema::Papel::Corpo);
    ft.setWeight(QFont::Bold);
    t->setFont(ft);
    texto->addWidget(t);
    if (!p.motivo.empty()) texto->addWidget(puro(q(p.motivo), cartao));
    texto->addWidget(rotulo(QStringLiteral("Efeito: ") + efeito, tema::Papel::Corpo, false, "nota", cartao));
    if (overTeto) {
        auto* av = colorido(QStringLiteral("! Passa do seu teto de %1 h por dia. Ajuste o valor ou aumente "
                                           "o teto em Permissões.")
                                .arg(d.teto),
                            "warn", true, cartao);
        av->setWordWrap(true);
        texto->addWidget(av);
    }
    auto* origem = new QLabel(cartao);
    origem->setTextFormat(Qt::PlainText);
    origem->setProperty("classe", QStringLiteral("nota"));
    origem->setFont(tema::fonte(tema::Papel::Legenda));
    origem->setText(QStringLiteral("proposta por %1 em %2").arg(q(p.origem), quandoCurto(p.criadoEm)));
    texto->addWidget(origem);
    linha->addLayout(texto, 1);

    auto* acoes = new QHBoxLayout;
    acoes->setSpacing(tema::esp(1) + 2);
    const std::int64_t id = p.id;
    Proposta copia = p;
    if (pend) {
        auto* aceitar = botao(QStringLiteral("Aceitar"), "primario", cartao);
        aceitar->setEnabled(!overTeto);
        auto* ajustar = botao(ajustando ? QStringLiteral("Fechar ajuste") : QStringLiteral("Ajustar"),
                              "secundario", cartao);
        auto* recusar = botao(QStringLiteral("Recusar"), "discreto", cartao);
        acoes->addWidget(aceitar);
        acoes->addWidget(ajustar);
        acoes->addWidget(recusar);
        connect(aceitar, &QPushButton::clicked, this, [this, copia, valor]() mutable {
            store::Database db(banco_.toStdString());
            std::string erro;
            if (!db.aberto() || !mcp::aceitar(db, copia, valor, EstadoProposta::Aceita, agora(), &erro)) {
                QMessageBox::warning(this, QStringLiteral("Não aplicado"),
                                     QStringLiteral("Não consegui aplicar a proposta: %1").arg(q(erro)));
                return;
            }
            ajustando_ = 0;
            ajuste_.erase(copia.id);
            mudou();
        });
        connect(ajustar, &QPushButton::clicked, this, [this, id, ajustando] {
            ajustando_ = ajustando ? 0 : id;
            atualizar();
        });
        connect(recusar, &QPushButton::clicked, this, [this, copia]() mutable {
            store::Database db(banco_.toStdString());
            mcp::recusar(db, copia, agora());
            ajustando_ = 0;
            mudou();
        });
    } else if (p.valendo()) {
        acoes->addWidget(colorido(p.estado == EstadoProposta::Aceita
                                      ? QStringLiteral("✓ Aceita por você")
                                      : QStringLiteral("✓ Aplicada pelo agente, como você permitiu"),
                                  "ok", true, cartao));
        auto* desfazer = botaoLink(QStringLiteral("Desfazer"), cartao);
        acoes->addWidget(desfazer);
        connect(desfazer, &QPushButton::clicked, this, [this, copia]() mutable {
            store::Database db(banco_.toStdString());
            mcp::desfazer(db, copia, agora());
            mudou();
        });
    } else {
        acoes->addWidget(rotulo(p.estado == EstadoProposta::Recusada ? QStringLiteral("Recusada por você")
                                                                     : QStringLiteral("Desfeita por você"),
                                tema::Papel::Corpo, false, "nota", cartao));
        auto* reabrir = botaoLink(QStringLiteral("Reabrir"), cartao);
        acoes->addWidget(reabrir);
        connect(reabrir, &QPushButton::clicked, this, [this, copia]() mutable {
            store::Database db(banco_.toStdString());
            mcp::reabrir(db, copia);
            mudou();
        });
    }
    linha->addLayout(acoes);
    linha->setAlignment(acoes, Qt::AlignTop);
    v->addLayout(linha);

    if (ajustando) {
        auto* faixa = new QFrame(cartao);
        faixa->setProperty("classe", QStringLiteral("faixa"));
        auto* h = new QHBoxLayout(faixa);
        h->setContentsMargins(tema::esp(3), tema::esp(2) + 2, tema::esp(3), tema::esp(2) + 2);
        h->setSpacing(tema::esp(2) + 2);
        h->addWidget(rotulo(QStringLiteral("Ajustar para"), tema::Papel::Corpo, true, nullptr, faixa));
        QString dica;
        auto guardar = [this, id](int v) {
            ajuste_[id] = v;
            atualizar();
        };
        if (p.tipo == TipoProposta::Dificuldade || p.tipo == TipoProposta::Foco) {
            const QStringList ops = p.tipo == TipoProposta::Dificuldade
                                        ? QStringList{QStringLiteral("Fácil"), QStringLiteral("Média"), QStringLiteral("Difícil")}
                                        : QStringList{QStringLiteral("atenção"), QStringLiteral("dificuldade"), QStringLiteral("crítico")};
            auto* s = new Segmentado(ops, faixa);
            s->definir(std::clamp(valor, 1, 3) - 1);
            s->aoMudar = [guardar](int i) { guardar(i + 1); };
            h->addWidget(s);
        } else {
            const bool horas = p.tipo == TipoProposta::Horas;
            auto* passo = new Passo(horas ? 0 : 30, horas ? 16 * 60 : 180, 30,
                                    horas ? std::function<QString(int)>(horasTxt) : std::function<QString(int)>(dur), faixa);
            passo->definir(valor);
            passo->definirNome(QStringLiteral("Valor ajustado"));
            passo->aoMudar = guardar;
            h->addWidget(passo);
            dica = horas ? QStringLiteral("seu teto: %1 h por dia · ").arg(d.teto) : QStringLiteral("de 30 min a 3h · ");
        }
        h->addWidget(rotulo(dica + QStringLiteral("depois clique em Aceitar"), tema::Papel::Legenda, false, "nota", faixa));
        h->addStretch();
        v->addWidget(faixa);
    }
    return cartao;
}

// ---------------------------------------------------------------------------
// Métricas
// ---------------------------------------------------------------------------

void PainelProgresso::mostrarMetricas(const Dados& d) {
    esvaziar(areaKpis_);
    const QDate hoje = d.hoje, inicio = hoje.addDays(-6);
    auto naJanela = [&](QDate x) { return x.isValid() && x >= inicio && x <= hoje; };

    auto kpi = [&](const QString& titulo, const QString& valor, const QString& sub, const QString& tendencia,
                   const char* tomTendencia, int barra) {
        auto* f = new QFrame(conteudo_);
        f->setProperty("classe", QStringLiteral("cartao"));
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(tema::esp(4), tema::esp(3) + 2, tema::esp(4), tema::esp(3) + 2);
        v->setSpacing(3);
        v->addWidget(rotulo(titulo.toUpper(), tema::Papel::Legenda, true, "secao", f));
        auto* val = new QLabel(valor, f);
        QFont fv = tema::fonte(tema::Papel::Titulo);
        fv.setWeight(QFont::Bold);
        val->setFont(fv);
        v->addWidget(val);
        auto* s = rotulo(sub, tema::Papel::Corpo, false, "nota", f);
        v->addWidget(s);
        if (!tendencia.isEmpty()) {
            auto* t = colorido(tendencia, tomTendencia, true, f);
            t->setWordWrap(true);
            v->addWidget(t);
        }
        if (barra >= 0) {
            auto* b = new QProgressBar(f);
            b->setRange(0, 100);
            b->setValue(std::min(barra, 100));
            b->setTextVisible(false);
            b->setFixedHeight(6);
            v->addSpacing(tema::esp(1));
            v->addWidget(b);
        }
        v->addStretch();
        areaKpis_->addWidget(f, 1);
    };

    // Seguiu o plano: das sessões dos últimos 7 dias que já deviam ter
    // acontecido (as de hoje ainda pendentes não contam contra), quanto foi
    // marcado como feito.
    int planejado = 0, feito = 0;
    for (const auto& s : d.sessoes) {
        const QDate dia = paraQ(s.dia);
        if (!naJanela(dia)) continue;
        if (s.feita || dia < hoje) planejado += s.minutos;
        if (s.feita) feito += s.minutos;
    }
    if (planejado > 0) {
        const int pct = 100 * feito / planejado;
        kpi(QStringLiteral("Seguiu o plano · 7 dias"), QStringLiteral("%1%").arg(pct),
            QStringLiteral("%1 de %2 planejadas").arg(dur(feito), dur(planejado)), QString(), "text-2", pct);
    } else {
        kpi(QStringLiteral("Seguiu o plano · 7 dias"), QStringLiteral("—"),
            QStringLiteral("Nenhuma sessão planejada nos últimos 7 dias"), QString(), "text-2", -1);
    }

    // Acerto: tudo que foi registrado; a tendência compara com o que havia
    // antes desta semana.
    int a = 0, n = 0, aAntes = 0, nAntes = 0;
    std::set<std::string> topicos;
    for (const auto& x : d.desempenhos) {
        a += x.acertos;
        n += x.total;
        topicos.insert(x.idTurma + "|" + util::dobrar(x.topico));
        if (paraQ(x.quando) < inicio) {
            aAntes += x.acertos;
            nAntes += x.total;
        }
    }
    if (n > 0) {
        const int pct = 100 * a / n;
        QString tend;
        const char* tom = "text-2";
        if (nAntes > 0 && nAntes < n) {
            const int delta = pct - 100 * aAntes / nAntes;
            tend = delta == 0 ? QStringLiteral("= estável desde %1").arg(inicio.toString(QStringLiteral("dd/MM")))
                              : QStringLiteral("%1 %2 pts desde %3")
                                    .arg(delta > 0 ? QStringLiteral("↑") : QStringLiteral("↓"))
                                    .arg(std::abs(delta))
                                    .arg(inicio.toString(QStringLiteral("dd/MM")));
            tom = delta > 0 ? "ok" : delta < 0 ? "danger" : "text-2";
        }
        kpi(QStringLiteral("Acerto nas questões"), QStringLiteral("%1%").arg(pct),
            QStringLiteral("%1 de %2 questões, em %3 tópico(s)").arg(a).arg(n).arg(topicos.size()), tend, tom, -1);
    } else {
        kpi(QStringLiteral("Acerto nas questões"), QStringLiteral("—"),
            QStringLiteral("O agente ainda não registrou exercícios"), QString(), "text-2", -1);
    }

    // Próxima prova: a(s) do dia mais próximo, e o que falta delas no plano.
    QDate prox;
    for (const auto& p : entradas_.provas) {
        const QDate dp = paraQ(p.data);
        if (dp.isValid() && dp >= hoje && (!prox.isValid() || dp < prox)) prox = dp;
    }
    if (prox.isValid()) {
        const qint64 dias = hoje.daysTo(prox);
        QStringList quais, faltas;
        for (const auto& p : entradas_.provas) {
            if (paraQ(p.data) != prox) continue;
            quais << q(p.descricao) + QStringLiteral(" de ") + d.nome(p.idTurma);
            int falta = 0;
            for (const auto& s : d.sessoes) {
                if (!s.feita && s.idTurma == p.idTurma && s.prova == p.descricao && paraQ(s.dia) >= hoje) {
                    falta += s.minutos;
                }
            }
            if (falta > 0) faltas << dur(falta) + QStringLiteral(" de ") + d.nome(p.idTurma);
        }
        kpi(QStringLiteral("Próxima prova"),
            dias == 0 ? QStringLiteral("hoje") : dias == 1 ? QStringLiteral("amanhã") : QStringLiteral("em %1 dias").arg(dias),
            quais.join(QStringLiteral(" e ")) + QStringLiteral(" · ") + diaCurto(prox),
            faltas.isEmpty() ? QStringLiteral("Nada pendente no plano") : QStringLiteral("Falta estudar ") + faltas.join(QStringLiteral(" e ")),
            "text-2", -1);
    } else {
        kpi(QStringLiteral("Próxima prova"), QStringLiteral("—"), QStringLiteral("Nenhuma prova pela frente"),
            QString(), "text-2", -1);
    }

    // --- por matéria ---
    porMateria_->setRowCount(static_cast<int>(entradas_.turmas.size()));
    int r = 0;
    for (const auto& [id, nomeTurma] : entradas_.turmas) {
        const int dif = static_cast<int>(d.prefs.dificuldadeDe(id));
        const bool doAgente = mcp::mudancaValendo(d.propostas, TipoProposta::Dificuldade, id, -1, dif).has_value();
        porMateria_->setItem(r, 0, celula(q(nomeTurma), nullptr, true));
        porMateria_->setItem(r, 1, celula(difTxt(dif) + (doAgente ? QStringLiteral("  ↑ agente") : QString()),
                                          doAgente ? "accent" : nullptr));

        int est = 0, plan = 0;
        for (const auto& s : d.sessoes) {
            const QDate dia = paraQ(s.dia);
            if (s.idTurma != id || !naJanela(dia)) continue;
            if (s.feita || dia < hoje) plan += s.minutos;
            if (s.feita) est += s.minutos;
        }
        for (const auto& x : d.estudos) {
            if (x.idTurma == id && naJanela(paraQ(x.quando))) est += x.minutos;
        }
        porMateria_->setItem(r, 2, plan ? celula(dur(est) + QStringLiteral(" de ") + dur(plan))
                                        : celula(est ? dur(est) + QStringLiteral(" · sem plano") : QStringLiteral("—"),
                                                 est ? nullptr : "text-3"));

        const auto* prova = proximaProva(entradas_.provas, id, hoje.addDays(-1));
        if (prova) {
            int falta = 0;
            for (const auto& s : d.sessoes) {
                if (!s.feita && s.idTurma == id && s.prova == prova->descricao && paraQ(s.dia) >= hoje) falta += s.minutos;
            }
            porMateria_->setItem(r, 3, celula((falta ? dur(falta) : QStringLiteral("nada no plano")) +
                                                  QStringLiteral(" · ") + nomeDaProva(*prova)));
        } else {
            porMateria_->setItem(r, 3, celula(QStringLiteral("—"), "text-3"));
        }

        int ac = 0, tot = 0, acAntes = 0, totAntes = 0;
        for (const auto& x : d.desempenhos) {
            if (x.idTurma != id) continue;
            ac += x.acertos;
            tot += x.total;
            if (paraQ(x.quando) < inicio) {
                acAntes += x.acertos;
                totAntes += x.total;
            }
        }
        if (tot > 0) {
            const int pct = 100 * ac / tot;
            porMateria_->setItem(r, 4, celula(simboloDoAcerto(pct) + QStringLiteral(" %1%").arg(pct), tomDoAcerto(pct), true));
        } else {
            porMateria_->setItem(r, 4, celula(QStringLiteral("sem registro"), "text-3"));
        }
        if (tot > 0 && totAntes > 0 && totAntes < tot) {
            const int delta = 100 * ac / tot - 100 * acAntes / totAntes;
            porMateria_->setItem(r, 5, celula(delta == 0  ? QStringLiteral("= estável")
                                              : delta > 0 ? QStringLiteral("↑ %1 pts").arg(delta)
                                                          : QStringLiteral("↓ %1 pts").arg(-delta),
                                              delta > 0 ? "ok" : delta < 0 ? "danger" : "text-2", delta != 0));
        } else {
            porMateria_->setItem(r, 5, celula(tot > 0 ? QStringLiteral("poucos dados") : QStringLiteral("—"), "text-3"));
        }
        ++r;
    }
    for (int col = 0; col < porMateria_->columnCount(); ++col) {
        porMateria_->horizontalHeader()->setSectionResizeMode(
            col, col == 0 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }
    ajustarAltura(porMateria_, 12);
}

// ---------------------------------------------------------------------------
// Focos
// ---------------------------------------------------------------------------

void PainelProgresso::mostrarFocos(const Dados& d) {
    esvaziar(areaFocos_);
    int abertos = 0;
    for (const auto& f : d.focos) {
        if (f.resolvido) continue;
        ++abertos;
        auto* cartao = new QFrame(conteudo_);
        cartao->setProperty("classe", QStringLiteral("cartao"));
        auto* h = new QHBoxLayout(cartao);
        h->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        h->setSpacing(tema::esp(4));
        const int n = std::clamp(f.nivel, 1, 3);
        const QString glifo = n == 3 ? QStringLiteral("●●●") : n == 2 ? QStringLiteral("●●○") : QStringLiteral("●○○");
        auto* nivel = colorido(glifo + QStringLiteral(" ") + nivelTxt(n), n >= 3 ? "danger" : "warn", true, cartao);
        nivel->setFixedWidth(120);
        h->addWidget(nivel, 0, Qt::AlignTop);
        auto* texto = new QVBoxLayout;
        texto->setSpacing(2);
        auto* t = puro(q(f.topico) + QStringLiteral(" — ") + d.nome(f.idTurma), cartao);
        QFont ft = t->font();
        ft.setWeight(QFont::Bold);
        t->setFont(ft);
        texto->addWidget(t);
        if (!f.motivo.empty()) texto->addWidget(puro(q(f.motivo), cartao));
        auto* origem = puro(QStringLiteral("marcado por %1 em %2").arg(q(f.origem), quandoCurto(f.atualizadoEm)), cartao);
        origem->setProperty("classe", QStringLiteral("nota"));
        origem->setFont(tema::fonte(tema::Papel::Legenda));
        texto->addWidget(origem);
        h->addLayout(texto, 1);
        auto* domino = botao(QStringLiteral("✓ Já domino"), "secundario", cartao);
        auto* apagar = botao(QStringLiteral("Apagar"), "discreto", cartao);
        h->addWidget(domino, 0, Qt::AlignVCenter);
        h->addWidget(apagar, 0, Qt::AlignVCenter);
        const estudo::PontoFoco copia = f;
        connect(domino, &QPushButton::clicked, this, [this, copia] {
            store::Database db(banco_.toStdString());
            db.resolverFoco(copia.id, "resolvido pelo aluno", agora());
            fechados_.push_back({copia, true});
            mudou();
        });
        connect(apagar, &QPushButton::clicked, this, [this, copia] {
            store::Database db(banco_.toStdString());
            db.apagarDoAgente(store::Database::TabelaAgente::Foco, copia.id);
            fechados_.push_back({copia, false});
            mudou();
        });
        areaFocos_->addWidget(cartao);
    }
    // Os fechados agora, com Desfazer até a página sair.
    for (size_t i = 0; i < fechados_.size(); ++i) {
        const auto& fe = fechados_[i];
        auto* cartao = new QFrame(conteudo_);
        cartao->setProperty("classe", QStringLiteral("cartaoApagado"));
        auto* h = new QHBoxLayout(cartao);
        h->setContentsMargins(tema::esp(4), tema::esp(2) + 2, tema::esp(4), tema::esp(2) + 2);
        auto* t = puro((fe.dominado ? QStringLiteral("✓ Marcado como dominado: ") : QStringLiteral("Apagado: ")) +
                           q(fe.foco.topico) + QStringLiteral(" — ") + d.nome(fe.foco.idTurma) +
                           QStringLiteral(". O plano vai se ajustar."),
                       cartao);
        t->setProperty("classe", QStringLiteral("nota"));
        h->addWidget(t, 1);
        auto* desfazer = botaoLink(QStringLiteral("Desfazer"), cartao);
        h->addWidget(desfazer);
        connect(desfazer, &QPushButton::clicked, this, [this, i] {
            if (i >= fechados_.size()) return;
            const Fechado fe = fechados_[i];
            fechados_.erase(fechados_.begin() + static_cast<std::ptrdiff_t>(i));
            store::Database db(banco_.toStdString());
            if (fe.dominado) {
                db.reabrirFoco(fe.foco.id, agora());
            } else {
                // Apagado volta como estava: mesma origem, nível e motivo.
                db.marcarFoco(fe.foco);
            }
            mudou();
        });
        areaFocos_->addWidget(cartao);
    }
    if (abertos == 0 && fechados_.empty()) {
        areaFocos_->addWidget(rotulo(QStringLiteral("Nenhum ponto de foco aberto."), tema::Papel::Corpo, false,
                                     "nota", conteudo_));
    }
}

// ---------------------------------------------------------------------------
// Desempenho e histórico
// ---------------------------------------------------------------------------

void PainelProgresso::mostrarDesempenho(const Dados& d) {
    std::map<std::pair<std::string, std::string>, std::pair<int, int>> acum;
    std::map<std::pair<std::string, std::string>, std::string> nomeTopico;
    for (const auto& x : d.desempenhos) {
        const auto k = std::make_pair(x.idTurma, util::dobrar(x.topico));
        acum[k].first += x.acertos;
        acum[k].second += x.total;
        if (!nomeTopico.count(k)) nomeTopico[k] = x.topico;
    }
    desempenho_->setRowCount(static_cast<int>(acum.size()));
    int r = 0;
    for (const auto& [k, v] : acum) {
        const int pct = v.second ? 100 * v.first / v.second : 0;
        desempenho_->setItem(r, 0, celula(q(nomeTopico[k])));
        desempenho_->setItem(r, 1, celula(d.nome(k.first), "text-2"));
        desempenho_->setItem(r, 2, celula(QStringLiteral("%1 de %2").arg(v.first).arg(v.second), "text-2"));
        auto* p = celula(simboloDoAcerto(pct) + QStringLiteral(" %1%").arg(pct), tomDoAcerto(pct), true);
        p->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        desempenho_->setItem(r, 3, p);
        ++r;
    }
    larguras(desempenho_, 0, 1);
    ajustarAltura(desempenho_, 10);
}

void PainelProgresso::mostrarHistorico(const Dados& d) {
    struct Linha {
        std::int64_t ordem;
        QStringList cols;
        store::Database::TabelaAgente tabela;
        std::int64_t id;
    };
    std::vector<Linha> linhas;
    auto quandoDe = [](const DateTime& dia, std::int64_t criado) {
        // O dia registrado, com a hora em que o agente gravou.
        const QDate x = paraQ(dia);
        return x.isValid() ? x.toString(QStringLiteral("dd/MM")) + QDateTime::fromSecsSinceEpoch(criado).toString(QStringLiteral(" HH:mm"))
                           : quandoCurto(criado);
    };
    for (const auto& x : d.estudos) {
        QString det = dur(x.minutos);
        if (!x.prova.empty()) det += QStringLiteral(" para ") + q(x.prova);
        if (!x.topicos.empty()) {
            QStringList ts;
            for (const auto& t : x.topicos) ts << q(t);
            det += QStringLiteral(": ") + ts.join(QStringLiteral(", "));
        }
        linhas.push_back({x.criadoEm, {quandoDe(x.quando, x.criadoEm), QStringLiteral("Estudo"), d.nome(x.idTurma), det, q(x.origem)},
                          store::Database::TabelaAgente::Estudo, x.id});
    }
    for (const auto& x : d.desempenhos) {
        linhas.push_back({x.criadoEm,
                          {quandoDe(x.quando, x.criadoEm), QStringLiteral("Desempenho"), d.nome(x.idTurma),
                           QStringLiteral("%1: %2 de %3 (%4)").arg(q(x.topico)).arg(x.acertos).arg(x.total).arg(q(x.tipo)),
                           q(x.origem)},
                          store::Database::TabelaAgente::Desempenho, x.id});
    }
    for (const auto& f : d.focos) {
        linhas.push_back({f.atualizadoEm,
                          {quandoCurto(f.atualizadoEm), f.resolvido ? QStringLiteral("Foco resolvido") : QStringLiteral("Foco"),
                           d.nome(f.idTurma), q(f.topico) + QStringLiteral(" · ") + nivelTxt(f.nivel) + QStringLiteral(": ") + q(f.motivo),
                           q(f.origem)},
                          store::Database::TabelaAgente::Foco, f.id});
    }
    for (const auto& p : d.propostas) {
        if (p.estado == EstadoProposta::Pendente) continue;
        QString oque;
        switch (p.tipo) {
            case TipoProposta::Horas:
                oque = QStringLiteral("Horas %1: %2 → %3").arg(noDia(p.diaSemana), horasTxt(p.de), horasTxt(p.para));
                break;
            case TipoProposta::Dificuldade:
                oque = QStringLiteral("Dificuldade: %1 → %2").arg(difTxt(p.de), difTxt(p.para));
                break;
            case TipoProposta::Sessao:
                oque = QStringLiteral("Sessão extra de %1: %2 em %3").arg(q(p.topico), dur(p.para), diaCurto(paraQ(p.dia)));
                break;
            case TipoProposta::Foco:
                oque = QStringLiteral("Foco em %1: %2").arg(q(p.topico), nivelTxt(p.para));
                break;
        }
        const QString como = p.estado == EstadoProposta::Aceita     ? QStringLiteral("aceita por você")
                             : p.estado == EstadoProposta::Aplicada ? QStringLiteral("aplicada pelo agente")
                             : p.estado == EstadoProposta::Recusada ? QStringLiteral("recusada por você")
                                                                    : QStringLiteral("desfeita por você");
        linhas.push_back({p.respondidaEm,
                          {quandoCurto(p.respondidaEm), QStringLiteral("Mudança"),
                           p.idTurma.empty() ? QStringLiteral("—") : d.nome(p.idTurma), oque + QStringLiteral(" · ") + como,
                           q(p.origem)},
                          store::Database::TabelaAgente::Proposta, p.id});
    }
    std::stable_sort(linhas.begin(), linhas.end(), [](const Linha& a, const Linha& b) { return a.ordem > b.ordem; });
    historico_->clearSelection();
    historico_->setRowCount(static_cast<int>(linhas.size()));
    for (int i = 0; i < static_cast<int>(linhas.size()); ++i) {
        const auto& l = linhas[static_cast<size_t>(i)];
        for (int col = 0; col < 5; ++col) {
            historico_->setItem(i, col, celula(l.cols[col], col == 0 || col == 2 || col == 4 ? "text-2" : nullptr, col == 1));
        }
        historico_->item(i, 0)->setData(PapelTabela, static_cast<int>(l.tabela));
        historico_->item(i, 0)->setData(PapelId, static_cast<qlonglong>(l.id));
        historico_->item(i, 3)->setToolTip(l.cols[3]);
    }
    larguras(historico_, 3, 2);
    ajustarAltura(historico_, 10);
    apagarSel_->setEnabled(false);

    // "Apagar tudo de um agente": um botão por origem, com quantos registros.
    esvaziar(chips_);
    std::map<std::string, int> porOrigem;
    for (const auto& x : d.estudos) ++porOrigem[x.origem];
    for (const auto& x : d.desempenhos) ++porOrigem[x.origem];
    for (const auto& x : d.focos) ++porOrigem[x.origem];
    for (const auto& [origem, n] : porOrigem) {
        auto* b = botao(QStringLiteral("%1 · %2 registro(s)").arg(q(origem)).arg(n), "chipPerigo", perigo_);
        const std::string o = origem;
        connect(b, &QPushButton::clicked, this, [this, o] {
            const auto r = QMessageBox::question(
                this, QStringLiteral("Apagar registros"),
                QStringLiteral("Apagar tudo que %1 registrou? Isto não pode ser desfeito.").arg(q(o)));
            if (r != QMessageBox::Yes) return;
            store::Database db(banco_.toStdString());
            db.apagarTudoDoAgente(o);
            mudou();
        });
        chips_->addWidget(b);
    }
    if (porOrigem.empty()) chips_->addWidget(new QLabel(QStringLiteral("Nenhum registro de agente."), perigo_));
    chips_->addStretch();
}

} // namespace sigaa::ui
