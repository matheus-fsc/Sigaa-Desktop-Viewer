#include "ui/Agentes.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

#include "core/store/Database.h"
#include "core/util/Texto.h"
#include "mcp/Instalar.h"
#include "mcp/Leitura.h"
#include "mcp/Propostas.h"
#include "ui/Controles.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

using estudo::TipoProposta;

QString q(const std::string& s) { return QString::fromStdString(s); }

QLabel* nota(const QString& texto, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setWordWrap(true);
    l->setProperty("classe", QStringLiteral("nota"));
    return l;
}

QLabel* negrito(const QString& texto, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

// "hoje, 04:03", "ontem, 22:40", "29/09, 21:15"
QString quandoRelativo(std::int64_t t) {
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(t);
    const qint64 dias = dt.date().daysTo(QDate::currentDate());
    const QString hora = dt.toString(QStringLiteral("HH:mm"));
    if (dias == 0) return QStringLiteral("hoje, ") + hora;
    if (dias == 1) return QStringLiteral("ontem, ") + hora;
    return dt.toString(QStringLiteral("dd/MM")) + QStringLiteral(", ") + hora;
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
    t->horizontalHeader()->setStretchLastSection(true);
    tema::ajustarLista(t);
    return t;
}

// Texto que veio do agente vai em célula de tabela, que é texto puro: nada do
// que ele grava é interpretado como HTML na tela do aluno (docs/MCP.md §7).
QTableWidgetItem* celula(const QString& s) { return new QTableWidgetItem(s); }

// O nome que cada agente manda no `initialize` (clientInfo.name) não é o id
// do instalador: o Claude Desktop diz "claude-ai", o VS Code diz "Visual
// Studio Code". Pedaços conhecidos de cada um, para achar o último acesso.
std::vector<std::string> nomesDoCliente(mcp::Cliente c) {
    switch (c) {
        case mcp::Cliente::ClaudeCode:    return {"claude-code"};
        case mcp::Cliente::ClaudeDesktop: return {"claude-ai", "claude desktop"};
        case mcp::Cliente::Codex:         return {"codex"};
        case mcp::Cliente::Gemini:        return {"gemini"};
        case mcp::Cliente::Antigravity:   return {"antigravity"};
        case mcp::Cliente::Cursor:        return {"cursor"};
        case mcp::Cliente::VSCode:        return {"visual studio code", "vscode"};
    }
    return {};
}

// Uma linha de lista dentro de um cartão: filete embaixo.
QFrame* linhaDeLista(QWidget* pai) {
    auto* f = new QFrame(pai);
    f->setProperty("classe", QStringLiteral("linha"));
    return f;
}

} // namespace

PainelAgentes::PainelAgentes(QString banco, QString materiais, QWidget* pai)
    : QWidget(pai), banco_(std::move(banco)), materiais_(std::move(materiais)) {
    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Agentes de IA"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    auto* intro = nota(QStringLiteral("Ligue o app a um agente (Claude Code, Codex, Gemini…) pelo protocolo "
                                      "MCP. Ele lê só o que você permitir, registra aqui o que estudou com "
                                      "você e pode propor mudanças no seu plano."),
                       this);
    intro->setMaximumWidth(820);
    raiz->addWidget(intro);

    abas_ = new QTabWidget(this);
    abas_->addTab(montarPermissoes(), QStringLiteral("Permissões"));
    abas_->addTab(montarConexoes(), QStringLiteral("Conectar"));
    abas_->addTab(montarAtividade(), QStringLiteral("Atividade"));
    raiz->addWidget(abas_, 1);
    connect(abas_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 0) carregarPermissoes();
        if (i == 1) atualizarConexoes();
        if (i == 2) atualizarAtividade();
    });
    carregarPermissoes();
}

void PainelAgentes::mostrarConexao() { abas_->setCurrentIndex(1); }
void PainelAgentes::mostrarPermissoes() { abas_->setCurrentIndex(0); }

void PainelAgentes::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // O agente pode ter lido ou gravado com a aba fechada; a permissão pode
    // ter mudado pelo terminal (`sigaa-cli mcp permitir`).
    carregarPermissoes();
    atualizarConexoes();
    atualizarAtividade();
}

// ---------------------------------------------------------------------------
// Permissões
// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarPermissoes() {
    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    auto* p = new QWidget(rol);
    rol->setWidget(p);
    auto* fora = new QVBoxLayout(p);
    fora->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(4));
    auto* coluna = new QWidget(p);
    coluna->setMaximumWidth(760);
    fora->addWidget(coluna);
    fora->addStretch();
    auto* v = new QVBoxLayout(coluna);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(tema::esp(3));

    // --- os quatro interruptores ---
    auto* caixa = new QFrame(coluna);
    caixa->setProperty("classe", QStringLiteral("cartao"));
    auto* cv = new QVBoxLayout(caixa);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    auto item = [&](LinhaPermissao& lp, const char* nome, const QString& rotulo, const QString& explica,
                    bool ultima) {
        auto* linha = ultima ? new QFrame(caixa) : linhaDeLista(caixa);
        auto* h = new QHBoxLayout(linha);
        h->setContentsMargins(tema::esp(4), tema::esp(3) + 2, tema::esp(4), tema::esp(3) + 2);
        h->setSpacing(tema::esp(4));
        auto* texto = new QVBoxLayout;
        texto->setSpacing(3);
        texto->addWidget(negrito(rotulo, linha));
        texto->addWidget(nota(explica, linha));
        if (std::string_view(nome) != "leitura") {
            lp.precisa = nota(QStringLiteral("Precisa de “Ler meus dados acadêmicos”."), linha);
            lp.precisa->setFont(tema::fonte(tema::Papel::Legenda));
            texto->addWidget(lp.precisa);
        }
        h->addLayout(texto, 1);
        lp.chave = new Interruptor(linha);
        lp.chave->setAccessibleName(rotulo);
        h->addWidget(lp.chave, 0, Qt::AlignVCenter);
        lp.linha = linha;
        cv->addWidget(linha);
        connect(lp.chave, &QAbstractButton::toggled, this, [this, nome, rotulo](bool sim) {
            gravarPermissao(nome, sim, rotulo);
            carregarPermissoes();
        });
    };
    item(leitura_, "leitura", QStringLiteral("Ler meus dados acadêmicos"),
         QStringLiteral("Turmas, tópicos, provas, prazos, notícias do professor e frequência. Sem isto o "
                        "agente não vê nada. A lista de participantes (seus colegas) nunca é enviada."),
         false);
    item(arquivos_, "arquivos", QStringLiteral("Ler os materiais baixados"),
         QStringLiteral("O caminho e o conteúdo dos PDFs e arquivos que o app já baixou do SIGAA."), false);
    item(escrita_, "escrita", QStringLiteral("Registrar estudo no app"),
         QStringLiteral("O agente grava o tempo de estudo, seu desempenho em exercícios e os pontos em que "
                        "você precisa focar. Tudo aparece em Progresso, e você apaga quando quiser."),
         false);
    item(rede_, "rede", QStringLiteral("Buscar no SIGAA"),
         QStringLiteral("O agente baixa um material que falta ou atualiza uma turma, com no máximo algumas "
                        "requisições por conversa. Usa sua senha guardada no cofre, sem nunca mostrá-la a ele."),
         true);
    v->addWidget(caixa);
    estadoPermissoes_ = nota(QStringLiteral("Cada interruptor grava no clique."), coluna);
    estadoPermissoes_->setFont(tema::fonte(tema::Papel::Legenda));
    v->addWidget(estadoPermissoes_);

    // --- o que o agente pode mudar no plano ---
    caixaModos_ = new QFrame(coluna);
    caixaModos_->setProperty("classe", QStringLiteral("cartao"));
    auto* mv = new QVBoxLayout(caixaModos_);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(0);
    {
        auto* cab = linhaDeLista(caixaModos_);
        auto* ch = new QVBoxLayout(cab);
        ch->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        ch->setSpacing(3);
        auto* t = new QLabel(QStringLiteral("O que o agente pode mudar no seu plano"), cab);
        QFont f = tema::fonte(tema::Papel::Subtitulo);
        f.setWeight(QFont::Bold);
        t->setFont(f);
        ch->addWidget(t);
        auto* explica = new QLabel(QStringLiteral("<b>Propõe</b>: aparece em Progresso para você aceitar, ajustar "
                                                  "ou recusar. <b>Aplica e avisa</b>: muda na hora, e você pode "
                                                  "desfazer."),
                                   cab);
        explica->setTextFormat(Qt::RichText);
        explica->setWordWrap(true);
        explica->setProperty("classe", QStringLiteral("nota"));
        ch->addWidget(explica);
        precisaEscrita_ = nota(QStringLiteral("Precisa de “Registrar estudo no app”."), cab);
        precisaEscrita_->setFont(tema::fonte(tema::Papel::Legenda));
        ch->addWidget(precisaEscrita_);
        mv->addWidget(cab);
    }
    static const std::tuple<TipoProposta, const char*, const char*> kTipos[] = {
        {TipoProposta::Horas, "Tempo disponível por dia", "Aumentar ou reduzir as horas de um dia."},
        {TipoProposta::Dificuldade, "Dificuldade das matérias", "Subir ou baixar a dificuldade quando o desempenho pede."},
        {TipoProposta::Sessao, "Sessões extras", "Encaixar revisão de um ponto fraco no tempo livre."},
        {TipoProposta::Foco, "Pontos de foco", "Marcar tópicos em que você tem dificuldade."}};
    auto linhaModo = [&](const QString& rotulo, const QString& explica, Segmentado* seg) {
        auto* linha = linhaDeLista(caixaModos_);
        auto* h = new QHBoxLayout(linha);
        h->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        h->setSpacing(tema::esp(4));
        auto* texto = new QVBoxLayout;
        texto->setSpacing(2);
        texto->addWidget(negrito(rotulo, linha));
        auto* n = nota(explica, linha);
        n->setFont(tema::fonte(tema::Papel::Legenda));
        texto->addWidget(n);
        h->addLayout(texto, 1);
        seg->setParent(linha);
        seg->setAccessibleName(rotulo);
        h->addWidget(seg, 0, Qt::AlignVCenter);
        mv->addWidget(linha);
    };
    for (const auto& [tipo, rotulo, explica] : kTipos) {
        auto* seg = new Segmentado({QStringLiteral("Não pode"), QStringLiteral("Propõe"), QStringLiteral("Aplica e avisa")});
        const TipoProposta t = tipo;
        const QString nomeTipo = QString::fromUtf8(rotulo).toLower();
        seg->aoMudar = [this, t, nomeTipo](int i) {
            store::Database db(banco_.toStdString());
            if (!db.aberto() || !db.migrar()) return;
            const auto m = static_cast<mcp::Modo>(i);
            if (!mcp::gravarModo(db, t, m)) {
                QMessageBox::warning(this, QStringLiteral("Não salvo"), QStringLiteral("Não consegui guardar a escolha."));
                return;
            }
            int pendentes = 0;
            for (const auto& p : db.carregarPropostas()) {
                pendentes += p.tipo == t && p.estado == estudo::EstadoProposta::Pendente;
            }
            QString msg = QStringLiteral("✓ Gravado: %1 — %2.")
                              .arg(nomeTipo, m == mcp::Modo::NaoPode  ? QStringLiteral("o agente não pode mudar")
                                             : m == mcp::Modo::Propoe ? QStringLiteral("o agente propõe, você aprova")
                                                                      : QStringLiteral("o agente aplica e avisa"));
            // Aplica: o que esperava resposta vale agora. Não pode: some da
            // tela, mas fica guardado para quando o aluno voltar atrás.
            if (m == mcp::Modo::Aplica && pendentes) {
                const int n = mcp::aplicarPendentes(db, t, QDateTime::currentSecsSinceEpoch());
                if (n) msg += QStringLiteral(" %1 proposta(s) pendente(s) aplicada(s) agora.").arg(n);
                if (n < pendentes) msg += QStringLiteral(" %1 passa(m) do teto e continua(m) esperando.").arg(pendentes - n);
            } else if (m == mcp::Modo::NaoPode && pendentes) {
                msg += QStringLiteral(" %1 proposta(s) pendente(s) ficam escondidas.").arg(pendentes);
            }
            estadoPermissoes_->setText(msg);
            if (aoMudar) aoMudar();
        };
        modos_.push_back(seg);
        linhaModo(QString::fromUtf8(rotulo), QString::fromUtf8(explica), seg);
    }
    teto_ = new Segmentado({QStringLiteral("6 h"), QStringLiteral("8 h"), QStringLiteral("10 h"), QStringLiteral("12 h")});
    teto_->aoMudar = [this](int i) {
        store::Database db(banco_.toStdString());
        const int h = 6 + 2 * i;
        if (!db.aberto() || !db.migrar() || !mcp::gravarTeto(db, h)) return;
        estadoPermissoes_->setText(QStringLiteral("✓ Gravado: teto de %1 h por dia.").arg(h));
        if (aoMudar) aoMudar();
    };
    linhaModo(QStringLiteral("Teto de horas por dia"),
              QStringLiteral("O agente nunca propõe nem aplica mais que isso em um dia."), teto_);
    v->addWidget(caixaModos_);
    v->addWidget(nota(QStringLiteral("Toda mudança feita pelo agente fica no Histórico de Progresso e pode ser "
                                     "desfeita."),
                      coluna));

    auto* aviso = new QLabel(QStringLiteral("O que o agente lê vai para a empresa que o fornece (Anthropic, "
                                            "OpenAI, Google…), sob as regras de privacidade dela. A senha do "
                                            "SIGAA nunca é enviada, e a lista de colegas de turma nunca sai do app."),
                             coluna);
    aviso->setWordWrap(true);
    aviso->setProperty("classe", QStringLiteral("recado"));
    v->addWidget(aviso);
    return rol;
}

void PainelAgentes::carregarPermissoes() {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar()) return;
    const std::pair<LinhaPermissao*, mcp::Permissao> pares[] = {
        {&leitura_, mcp::Permissao::Leitura}, {&arquivos_, mcp::Permissao::Arquivos},
        {&escrita_, mcp::Permissao::Escrita}, {&rede_, mcp::Permissao::Rede}};
    for (const auto& [lp, perm] : pares) {
        QSignalBlocker b(lp->chave);
        lp->chave->setChecked(mcp::permitido(db, perm));
    }
    // Sem leitura, as outras não têm efeito: o servidor recusa tudo.
    const bool le = leitura_.chave->isChecked();
    for (LinhaPermissao* lp : {&arquivos_, &escrita_, &rede_}) {
        lp->linha->setEnabled(le);
        lp->precisa->setVisible(!le);
    }
    const TipoProposta tipos[] = {TipoProposta::Horas, TipoProposta::Dificuldade, TipoProposta::Sessao,
                                  TipoProposta::Foco};
    for (size_t i = 0; i < modos_.size(); ++i) modos_[i]->definir(static_cast<int>(mcp::modo(db, tipos[i])));
    teto_->definir(std::clamp((mcp::tetoHoras(db) - 6) / 2, 0, 3));
    // Mudar o plano é gravar: sem "Registrar estudo", o agente não grava nada.
    const bool grava = le && escrita_.chave->isChecked();
    for (Segmentado* w : modos_) w->setEnabled(grava);
    teto_->setEnabled(grava);
    precisaEscrita_->setVisible(!grava);
    if (semLeitura_) semLeitura_->setVisible(!le);
}

void PainelAgentes::gravarPermissao(const char* nome, bool sim, const QString& rotulo) {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() ||
        !db.gravarMeta(std::string("mcp.") + nome, sim ? "1" : "0")) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar a permissão."));
        return;
    }
    estadoPermissoes_->setText(QStringLiteral("✓ Gravado: %1 %2.")
                                   .arg(rotulo.toLower(), sim ? QStringLiteral("ligado") : QStringLiteral("desligado")));
}

// ---------------------------------------------------------------------------
// Conectar
// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarConexoes() {
    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(true);
    rol->setFrameShape(QFrame::NoFrame);
    auto* p = new QWidget(rol);
    rol->setWidget(p);
    auto* fora = new QVBoxLayout(p);
    fora->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(4));
    auto* coluna = new QWidget(p);
    coluna->setMaximumWidth(820);
    fora->addWidget(coluna);
    fora->addStretch();
    auto* v = new QVBoxLayout(coluna);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(tema::esp(3));

    semLeitura_ = new QFrame(coluna);
    semLeitura_->setProperty("classe", QStringLiteral("alertaFaixa"));
    {
        auto* h = new QHBoxLayout(semLeitura_);
        h->setContentsMargins(tema::esp(3) + 2, tema::esp(3), tema::esp(3) + 2, tema::esp(3));
        h->setSpacing(tema::esp(2) + 2);
        auto* l = new QLabel(QStringLiteral("! “Ler meus dados acadêmicos” está desligado: um agente conectado não "
                                            "vai ver nada."),
                             semLeitura_);
        l->setWordWrap(true);
        h->addWidget(l, 1);
        auto* ver = new QPushButton(QStringLiteral("Ver permissões"), semLeitura_);
        ver->setAutoDefault(false);
        ver->setProperty("papel", QStringLiteral("secundario"));
        connect(ver, &QPushButton::clicked, this, [this] { mostrarPermissoes(); });
        h->addWidget(ver);
    }
    semLeitura_->hide();
    v->addWidget(semLeitura_);

    v->addWidget(nota(QStringLiteral("Conectar escreve a entrada \"sigaa\" na configuração do agente (com uma "
                                     "cópia .bak do arquivo antes) e não mexe no resto. Depois, reinicie o "
                                     "agente."),
                      coluna));

    conteudoConexao_ = new QFrame(coluna);
    conteudoConexao_->setProperty("classe", QStringLiteral("cartao"));
    linhasConexao_ = new QVBoxLayout(conteudoConexao_);
    linhasConexao_->setContentsMargins(0, 0, 0, 0);
    linhasConexao_->setSpacing(0);
    v->addWidget(conteudoConexao_);

    resultado_ = new QLabel(coluna);
    resultado_->setWordWrap(true);
    resultado_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultado_->setProperty("classe", QStringLiteral("recado"));
    resultado_->hide();
    v->addWidget(resultado_);

    auto* web = new QLabel(QStringLiteral("Usa um chat na web (ChatGPT, claude.ai, Gemini) sem MCP? Na janela de "
                                          "uma turma, <b>Kit para IA</b> junta numa pasta o resumo, a matéria e os "
                                          "PDFs para você arrastar de uma vez."),
                           coluna);
    web->setTextFormat(Qt::RichText);
    web->setWordWrap(true);
    web->setProperty("classe", QStringLiteral("recado"));
    v->addWidget(web);
    return rol;
}

void PainelAgentes::atualizarConexoes() {
    while (QLayoutItem* it = linhasConexao_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    // O último acesso de cada agente, pela auditoria.
    std::vector<store::Database::AcessoMcp> acessos;
    {
        store::Database db(banco_.toStdString());
        if (db.aberto() && db.migrar()) {
            acessos = db.ultimosAcessosMcp(500);
            if (semLeitura_) semLeitura_->setVisible(!mcp::permitido(db, mcp::Permissao::Leitura));
        }
    }
    const mcp::Pastas pastas = mcp::pastasDoSistema();
    const auto& lista = mcp::clientes();
    for (size_t i = 0; i < lista.size(); ++i) {
        const auto& info = lista[i];
        const bool conectado = mcp::instalado(info.cliente, pastas);
        const bool achado = conectado || mcp::detectado(info.cliente, pastas);
        std::int64_t ultimo = 0;
        for (const auto& a : acessos) {
            const std::string o = util::dobrar(a.origem);
            for (const auto& n : nomesDoCliente(info.cliente)) {
                if (o.find(n) != std::string::npos) ultimo = std::max(ultimo, a.quando);
            }
        }

        auto* linha = i + 1 < lista.size() ? linhaDeLista(conteudoConexao_) : new QFrame(conteudoConexao_);
        auto* h = new QHBoxLayout(linha);
        h->setContentsMargins(tema::esp(4), tema::esp(3), tema::esp(4), tema::esp(3));
        h->setSpacing(tema::esp(4));
        auto* nome = negrito(QString::fromUtf8(info.nome), linha);
        nome->setFixedWidth(160);
        h->addWidget(nome);
        auto* estado = new QVBoxLayout;
        estado->setSpacing(1);
        auto* e = new QLabel(conectado ? QStringLiteral("● conectado")
                             : achado  ? QStringLiteral("○ instalado, não conectado")
                                       : QStringLiteral("○ não encontrado neste computador"),
                             linha);
        if (conectado) {
            e->setStyleSheet(QStringLiteral("color: %1").arg(tema::cor::sucesso().name()));
            QFont f = e->font();
            f.setWeight(QFont::Bold);
            e->setFont(f);
        } else {
            e->setProperty("classe", QStringLiteral("nota"));
        }
        estado->addWidget(e);
        if (ultimo) {
            auto* u = nota(QStringLiteral("último acesso ") + quandoRelativo(ultimo), linha);
            u->setFont(tema::fonte(tema::Papel::Legenda));
            estado->addWidget(u);
        }
        h->addLayout(estado, 1);

        auto* conectar = new QPushButton(conectado ? QStringLiteral("Reconectar") : QStringLiteral("Conectar"), linha);
        conectar->setAutoDefault(false);
        conectar->setProperty("papel", QStringLiteral("secundario"));
        h->addWidget(conectar);
        const mcp::Cliente cli = info.cliente;
        connect(conectar, &QPushButton::clicked, this, [this, cli] {
            const auto l = mcp::lancamento(banco_.toStdString(), materiais_.toStdString());
            const auto r = mcp::instalar(cli, l, false, mcp::pastasDoSistema());
            if (r.ok) {
                store::Database db(banco_.toStdString());
                const bool leitura = db.aberto() && db.migrar() && mcp::permitido(db, mcp::Permissao::Leitura);
                resultado_->setText(q(r.mensagem) +
                                    (leitura ? QString()
                                             : QStringLiteral(" Ligue “Ler meus dados acadêmicos” em Permissões, "
                                                              "senão o agente não vai ver nada.")));
            } else {
                QMessageBox caixa(QMessageBox::Warning, QStringLiteral("Não conectado"), q(r.mensagem),
                                  QMessageBox::Ok, this);
                caixa.setDetailedText(q(r.trecho));
                caixa.exec();
                resultado_->setText(q(r.mensagem));
            }
            resultado_->show();
            atualizarConexoes();
        });

        if (conectado) {
            auto* desligar = new QPushButton(QStringLiteral("Desconectar"), linha);
            desligar->setAutoDefault(false);
            desligar->setProperty("papel", QStringLiteral("discreto"));
            h->addWidget(desligar);
            connect(desligar, &QPushButton::clicked, this, [this, cli] {
                const auto r = mcp::remover(cli, mcp::pastasDoSistema());
                resultado_->setText(q(r.mensagem));
                resultado_->show();
                atualizarConexoes();
            });
        }
        linhasConexao_->addWidget(linha);
    }
}

// ---------------------------------------------------------------------------
// Atividade
// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarAtividade() {
    auto* p = new QWidget(this);
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(3));
    v->setSpacing(tema::esp(2) + 2);
    v->addWidget(nota(QStringLiteral("Os 100 últimos acessos. Só o pedido fica aqui, nunca o conteúdo devolvido."), p));
    atividade_ = tabela({QStringLiteral("Quando"), QStringLiteral("Agente"), QStringLiteral("Pedido"),
                         QStringLiteral("Turma"), QStringLiteral("Resultado")},
                        p);
    v->addWidget(atividade_, 1);
    return p;
}

void PainelAgentes::atualizarAtividade() {
    store::Database db(banco_.toStdString());
    atividade_->setRowCount(0);
    if (!db.aberto() || !db.migrar()) return;
    std::map<std::string, QString> nomes;
    for (const auto& t : db.carregarUltimo().turmas) nomes[t.idTurma] = q(t.nome);
    const auto log = db.ultimosAcessosMcp(100);
    atividade_->setRowCount(static_cast<int>(log.size()));
    for (int r = 0; r < static_cast<int>(log.size()); ++r) {
        const auto& a = log[static_cast<size_t>(r)];
        auto* quando = celula(quandoRelativo(a.quando));
        quando->setForeground(tema::token("text-2"));
        atividade_->setItem(r, 0, quando);
        auto* ag = celula(q(a.origem));
        ag->setForeground(tema::token("text-2"));
        atividade_->setItem(r, 1, ag);
        atividade_->setItem(r, 2, celula(q(a.ferramenta)));
        atividade_->setItem(r, 3, celula(a.turma.empty() ? QStringLiteral("—")
                                         : nomes.count(a.turma) ? nomes[a.turma]
                                                                : q(a.turma)));
        // O motivo é a mensagem que o agente recebeu: mostra a primeira linha
        // e o resto na dica.
        QString motivo = q(a.motivo).section(QLatin1Char('\n'), 0, 0);
        auto* res = celula(a.ok ? QStringLiteral("✓ ok")
                                : QStringLiteral("✕ recusado") +
                                      (motivo.isEmpty() ? QString() : QStringLiteral(" · ") + motivo));
        if (!a.ok) {
            res->setForeground(tema::cor::atrasado());
            if (!a.motivo.empty()) res->setToolTip(q(a.motivo));
        } else {
            res->setForeground(tema::token("text-2"));
        }
        atividade_->setItem(r, 4, res);
    }
    for (int col = 0; col < atividade_->columnCount(); ++col) {
        atividade_->horizontalHeader()->setSectionResizeMode(
            col, col == 4 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }
}

} // namespace sigaa::ui
