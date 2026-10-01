#include "ui/Agentes.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>

#include "core/store/Database.h"
#include "mcp/Instalar.h"
#include "mcp/Leitura.h"
#include "core/util/Texto.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QLabel* nota(const QString& texto, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setWordWrap(true);
    l->setProperty("classe", QStringLiteral("nota"));
    return l;
}

QString quandoCurto(std::int64_t t) {
    return QDateTime::fromSecsSinceEpoch(t).toString(QStringLiteral("dd/MM HH:mm"));
}

QString duracao(int minutos) {
    const int h = minutos / 60, m = minutos % 60;
    if (h == 0) return QStringLiteral("%1 min").arg(m);
    return m ? QStringLiteral("%1h%2").arg(h).arg(m, 2, 10, QLatin1Char('0')) : QStringLiteral("%1h").arg(h);
}

QTableWidget* tabela(const QStringList& colunas, QWidget* pai) {
    auto* t = new QTableWidget(0, colunas.size(), pai);
    t->setHorizontalHeaderLabels(colunas);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setShowGrid(false);
    t->horizontalHeader()->setStretchLastSection(true);
    tema::ajustarLista(t);
    return t;
}

// Texto que veio do agente vai em célula de tabela, que é texto puro: nada do
// que ele grava é interpretado como HTML na tela do aluno (docs/MCP.md §7).
QTableWidgetItem* celula(const QString& s) { return new QTableWidgetItem(s); }

constexpr int PapelTabela = Qt::UserRole + 1;
constexpr int PapelId = Qt::UserRole + 2;

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
    raiz->addWidget(nota(
        QStringLiteral("Conecte o app ao agente que você usa (Claude, Codex, Gemini, Cursor...) "
                       "para estudar com seus tópicos, provas e PDFs sem arrastar arquivo. O "
                       "agente lê os dados deste computador pelo protocolo MCP e devolve o tempo "
                       "de estudo e os pontos em que você precisa focar, que aparecem em "
                       "Progresso e entram no Planejamento."),
        this));

    abas_ = new QTabWidget(this);
    abas_->addTab(montarPermissoes(), QStringLiteral("Permissões"));
    abas_->addTab(montarConexoes(), QStringLiteral("Conectar"));
    abas_->addTab(montarAtividade(), QStringLiteral("Atividade"));
    raiz->addWidget(abas_, 1);
    connect(abas_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 1) atualizarConexoes();
        if (i == 2) atualizarAtividade();
    });
    carregarPermissoes();
}

void PainelAgentes::mostrarConexao() { abas_->setCurrentIndex(1); }

void PainelAgentes::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // O agente pode ter lido ou gravado com a aba fechada; a permissão pode
    // ter mudado pelo terminal (`sigaa-cli mcp permitir`).
    carregarPermissoes();
    atualizarConexoes();
    atualizarAtividade();
}

// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarPermissoes() {
    auto* p = new QWidget(this);
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(3));
    v->setSpacing(tema::esp(2));

    auto item = [&](const QString& rotulo, const QString& explica) {
        auto* c = new QCheckBox(rotulo, p);
        QFont f = c->font();
        f.setWeight(QFont::DemiBold);
        c->setFont(f);
        v->addWidget(c);
        auto* l = nota(explica, p);
        l->setContentsMargins(tema::esp(7), 0, 0, tema::esp(2));
        v->addWidget(l);
        return c;
    };
    leitura_ = item(QStringLiteral("Ler meus dados acadêmicos"),
                    QStringLiteral("Turmas, tópicos de aula, provas, prazos, notícias do professor "
                                   "e frequência. Sem isto o agente não vê nada. A lista de "
                                   "participantes (seus colegas) nunca é enviada."));
    arquivos_ = item(QStringLiteral("Ler os materiais baixados"),
                     QStringLiteral("O caminho e o conteúdo dos PDFs e arquivos que o app já baixou "
                                    "do SIGAA."));
    escrita_ = item(QStringLiteral("Registrar estudo no app"),
                    QStringLiteral("O agente grava o tempo de estudo, seu desempenho em exercícios "
                                   "e os pontos em que você precisa focar. Tudo aparece em "
                                   "Progresso, e você apaga quando quiser."));
    rede_ = item(QStringLiteral("Buscar no SIGAA"),
                 QStringLiteral("O agente pode baixar um material que falta ou atualizar uma turma, "
                                "com no máximo algumas requisições por conversa. Usa sua senha "
                                "guardada no cofre, sem nunca mostrá-la ao agente."));
    v->addSpacing(tema::esp(2));
    auto* aviso = new QLabel(
        QStringLiteral("O que o agente lê vai para a empresa que o fornece (Anthropic, OpenAI, "
                       "Google...), como qualquer mensagem que você manda a ele. Libere só o que "
                       "você mandaria numa conversa."),
        p);
    aviso->setWordWrap(true);
    aviso->setProperty("classe", QStringLiteral("recado"));
    v->addWidget(aviso);
    v->addStretch();

    const std::pair<QCheckBox*, const char*> pares[] = {
        {leitura_, "leitura"}, {arquivos_, "arquivos"}, {escrita_, "escrita"}, {rede_, "rede"}};
    for (const auto& [c, nome] : pares) {
        connect(c, &QCheckBox::toggled, this, [this, nome = nome](bool sim) {
            gravarPermissao(nome, sim);
            // Sem leitura, as outras não têm efeito: o servidor recusa tudo.
            if (std::string_view(nome) == "leitura") {
                for (QCheckBox* x : {arquivos_, escrita_, rede_}) x->setEnabled(sim);
            }
        });
    }
    return p;
}

void PainelAgentes::carregarPermissoes() {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar()) return;
    const std::pair<QCheckBox*, mcp::Permissao> pares[] = {
        {leitura_, mcp::Permissao::Leitura}, {arquivos_, mcp::Permissao::Arquivos},
        {escrita_, mcp::Permissao::Escrita}, {rede_, mcp::Permissao::Rede}};
    for (const auto& [c, perm] : pares) {
        QSignalBlocker b(c);
        c->setChecked(mcp::permitido(db, perm));
    }
    for (QCheckBox* x : {arquivos_, escrita_, rede_}) x->setEnabled(leitura_->isChecked());
}

void PainelAgentes::gravarPermissao(const char* nome, bool sim) {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() ||
        !db.gravarMeta(std::string("mcp.") + nome, sim ? "1" : "0")) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar a permissão."));
    }
}

// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarConexoes() {
    auto* p = new QWidget(this);
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(3));
    v->setSpacing(tema::esp(3));
    v->addWidget(nota(QStringLiteral("Conectar escreve a entrada \"sigaa\" na configuração do "
                                     "agente (com uma cópia .bak do arquivo antes) e não mexe no "
                                     "resto. Depois, reinicie o agente."),
                      p));

    conteudoConexao_ = new QWidget(p);
    linhasConexao_ = new QVBoxLayout(conteudoConexao_);
    linhasConexao_->setContentsMargins(0, 0, 0, 0);
    linhasConexao_->setSpacing(tema::esp(2));
    v->addWidget(conteudoConexao_);

    resultado_ = new QLabel(p);
    resultado_->setWordWrap(true);
    resultado_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(resultado_);

    v->addWidget(nota(QStringLiteral("Usa um chat na web (ChatGPT, claude.ai, Gemini)? Ele não "
                                     "conecta por aqui. Na janela de uma turma, \"Kit para IA\" "
                                     "junta numa pasta o resumo, a matéria e os PDFs para você "
                                     "arrastar de uma vez."),
                      p));
    v->addStretch();
    return p;
}

void PainelAgentes::atualizarConexoes() {
    while (QLayoutItem* it = linhasConexao_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    const mcp::Pastas pastas = mcp::pastasDoSistema();
    for (const auto& info : mcp::clientes()) {
        const bool conectado = mcp::instalado(info.cliente, pastas);
        const bool achado = conectado || mcp::detectado(info.cliente, pastas);

        auto* linha = new QWidget(conteudoConexao_);
        auto* h = new QHBoxLayout(linha);
        h->setContentsMargins(0, 0, 0, 0);
        auto* nome = new QLabel(QString::fromUtf8(info.nome), linha);
        nome->setMinimumWidth(160);
        QFont f = nome->font();
        f.setWeight(QFont::DemiBold);
        nome->setFont(f);
        h->addWidget(nome);
        auto* estado = new QLabel(conectado ? QStringLiteral("conectado")
                                  : achado  ? QStringLiteral("instalado")
                                            : QStringLiteral("não encontrado"),
                                  linha);
        estado->setProperty("classe", QStringLiteral("nota"));
        if (conectado) estado->setStyleSheet(QStringLiteral("color: %1").arg(tema::cor::sucesso().name()));
        h->addWidget(estado, 1);

        auto* conectar = new QPushButton(conectado ? QStringLiteral("Reconectar")
                                                   : QStringLiteral("Conectar"),
                                         linha);
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
                                             : QStringLiteral(" Libere \"Ler meus dados acadêmicos\" "
                                                              "em Permissões, ou o agente não vai "
                                                              "ver nada.")));
            } else {
                QMessageBox caixa(QMessageBox::Warning, QStringLiteral("Não conectado"), q(r.mensagem),
                                  QMessageBox::Ok, this);
                caixa.setDetailedText(q(r.trecho));
                caixa.exec();
                resultado_->setText(q(r.mensagem));
            }
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
                atualizarConexoes();
            });
        }
        linhasConexao_->addWidget(linha);
    }
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

QWidget* PainelAgentes::montarAtividade() {
    auto* p = new QWidget(this);
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(3));
    v->setSpacing(tema::esp(2));
    v->addWidget(nota(QStringLiteral("Cada vez que um agente leu ou gravou algo. Só o pedido fica "
                                     "aqui, nunca o conteúdo devolvido."),
                      p));
    atividade_ = tabela({QStringLiteral("Quando"), QStringLiteral("Agente"),
                         QStringLiteral("Pedido"), QStringLiteral("Turma"),
                         QStringLiteral("Resultado")},
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
        atividade_->setItem(r, 0, celula(quandoCurto(a.quando)));
        atividade_->setItem(r, 1, celula(q(a.origem)));
        atividade_->setItem(r, 2, celula(q(a.ferramenta)));
        atividade_->setItem(r, 3, celula(nomes.count(a.turma) ? nomes[a.turma] : q(a.turma)));
        auto* res = celula(a.ok ? QStringLiteral("ok") : QStringLiteral("recusado"));
        if (!a.ok) res->setForeground(tema::cor::atrasado());
        atividade_->setItem(r, 4, res);
    }
    atividade_->resizeColumnsToContents();
}


// ---------------------------------------------------------------------------
// Progresso
// ---------------------------------------------------------------------------

PainelProgresso::PainelProgresso(QString banco, QWidget* pai)
    : QWidget(pai), banco_(std::move(banco)) {
    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Progresso"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    resumo_ = nota(QString(), this);
    raiz->addWidget(resumo_);

    // Estado vazio: o que é esta página e como enchê-la.
    vazio_ = new QWidget(this);
    {
        auto* v = new QVBoxLayout(vazio_);
        v->setContentsMargins(0, tema::esp(4), 0, 0);
        auto* t = new QLabel(
            QStringLiteral("Aqui aparece o que você estudou com um agente de IA: o tempo de cada "
                           "sessão, quantas questões acertou em cada tópico e os pontos em que o "
                           "agente viu que você precisa focar. O Planejamento usa tudo isso: o "
                           "estudo conta como feito, e cada dificuldade reserva mais tempo para a "
                           "prova."),
            vazio_);
        t->setWordWrap(true);
        t->setProperty("classe", QStringLiteral("recado"));
        v->addWidget(t);
        auto* linha = new QHBoxLayout;
        auto* b = new QPushButton(QStringLiteral("Conectar um agente"), vazio_);
        b->setProperty("papel", QStringLiteral("primario"));
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this] {
            if (aoPedirConexao) aoPedirConexao();
        });
        linha->addWidget(b);
        linha->addStretch();
        v->addLayout(linha);
        v->addStretch();
    }
    raiz->addWidget(vazio_, 1);

    conteudo_ = new QWidget(this);
    auto* c = new QVBoxLayout(conteudo_);
    c->setContentsMargins(0, 0, 0, 0);
    c->setSpacing(tema::esp(3));

    auto secao = [&](const QString& t) {
        auto* l = new QLabel(t, conteudo_);
        QFont f = tema::fonte(tema::Papel::Legenda);
        f.setWeight(QFont::DemiBold);
        l->setFont(f);
        l->setProperty("classe", QStringLiteral("secao"));
        c->addWidget(l);
    };

    secao(QStringLiteral("PONTOS DE FOCO"));
    caixaFocos_ = new QWidget(conteudo_);
    areaFocos_ = new QVBoxLayout(caixaFocos_);
    areaFocos_->setContentsMargins(0, 0, 0, 0);
    areaFocos_->setSpacing(tema::esp(2));
    c->addWidget(caixaFocos_);

    auto* colunas = new QHBoxLayout;
    colunas->setSpacing(tema::esp(5));
    auto* esq = new QVBoxLayout;
    auto* dir = new QVBoxLayout;
    {
        auto* l = new QLabel(QStringLiteral("DESEMPENHO POR TÓPICO"), conteudo_);
        QFont f = tema::fonte(tema::Papel::Legenda);
        f.setWeight(QFont::DemiBold);
        l->setFont(f);
        l->setProperty("classe", QStringLiteral("secao"));
        esq->addWidget(l);
        desempenho_ = tabela({QStringLiteral("Tópico"), QStringLiteral("Turma"),
                              QStringLiteral("Acertos"), QStringLiteral("%")},
                             conteudo_);
        esq->addWidget(desempenho_, 1);
    }
    {
        auto* l = new QLabel(QStringLiteral("HISTÓRICO"), conteudo_);
        QFont f = tema::fonte(tema::Papel::Legenda);
        f.setWeight(QFont::DemiBold);
        l->setFont(f);
        l->setProperty("classe", QStringLiteral("secao"));
        dir->addWidget(l);
        historico_ = tabela({QStringLiteral("Quando"), QStringLiteral("O quê"),
                             QStringLiteral("Turma"), QStringLiteral("Detalhe"),
                             QStringLiteral("Agente")},
                            conteudo_);
        dir->addWidget(historico_, 1);
        auto* linha = new QHBoxLayout;
        auto* apagar = new QPushButton(QStringLiteral("Apagar o selecionado"), conteudo_);
        auto* apagarOrigem = new QPushButton(QStringLiteral("Apagar tudo de um agente…"), conteudo_);
        for (QPushButton* b : {apagar, apagarOrigem}) {
            b->setAutoDefault(false);
            b->setProperty("papel", QStringLiteral("secundario"));
            linha->addWidget(b);
        }
        linha->addStretch();
        dir->addLayout(linha);
        connect(apagar, &QPushButton::clicked, this, [this] {
            const int r = historico_->currentRow();
            if (r < 0) return;
            const auto* it = historico_->item(r, 0);
            store::Database db(banco_.toStdString());
            db.apagarDoAgente(static_cast<store::Database::TabelaAgente>(it->data(PapelTabela).toInt()),
                              it->data(PapelId).toLongLong());
            atualizar();
            if (aoMudar) aoMudar();
        });
        connect(apagarOrigem, &QPushButton::clicked, this, [this] {
            std::set<QString> origens;
            for (int r = 0; r < historico_->rowCount(); ++r) origens.insert(historico_->item(r, 4)->text());
            if (origens.empty()) return;
            const QStringList lista(origens.begin(), origens.end());
            bool ok = false;
            const QString escolha = QInputDialog::getItem(
                this, QStringLiteral("Apagar registros"),
                QStringLiteral("Apagar tudo que este agente registrou:"), lista, 0, false, &ok);
            if (!ok) return;
            store::Database db(banco_.toStdString());
            db.apagarTudoDoAgente(escolha.toStdString());
            atualizar();
            if (aoMudar) aoMudar();
        });
    }
    colunas->addLayout(esq, 2);
    colunas->addLayout(dir, 3);
    c->addLayout(colunas, 1);
    raiz->addWidget(conteudo_, 1);
}

void PainelProgresso::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    atualizar();
}

void PainelProgresso::atualizar() {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar()) return;
    std::map<std::string, QString> nomes;
    for (const auto& t : db.carregarUltimo().turmas) nomes[t.idTurma] = q(t.nome);

    const auto estudos = db.carregarRegistrosEstudo();
    const auto desempenhos = db.carregarDesempenho();
    const auto focos = db.carregarFocos();
    const bool nada = estudos.empty() && desempenhos.empty() && focos.empty();
    vazio_->setVisible(nada);
    conteudo_->setVisible(!nada);

    // Resumo: tempo nos últimos 7 dias, por turma.
    const QDate hoje = QDate::currentDate();
    std::map<std::string, int> semana;
    int total = 0;
    for (const auto& r : estudos) {
        const QDate d(r.quando.year, r.quando.month, r.quando.day);
        if (d.daysTo(hoje) < 7) {
            semana[r.idTurma] += r.minutos;
            total += r.minutos;
        }
    }
    QStringList partes;
    for (const auto& [id, m] : semana) partes << QStringLiteral("%1 %2").arg(nomes[id], duracao(m));
    int abertos = 0;
    for (const auto& f : focos) abertos += f.resolvido ? 0 : 1;
    resumo_->setText(nada ? QString()
                          : QStringLiteral("%1 de estudo com agentes nos últimos 7 dias%2 · %3 ponto(s) de foco aberto(s)")
                                .arg(duracao(total))
                                .arg(partes.isEmpty() ? QString() : QStringLiteral(" (") + partes.join(QStringLiteral(", ")) + QStringLiteral(")"))
                                .arg(abertos));

    // --- focos abertos, como cartões com ações ---
    while (QLayoutItem* it = areaFocos_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    static const char* niveis[] = {"", "atenção", "dificuldade", "crítico"};
    for (const auto& f : focos) {
        if (f.resolvido) continue;
        auto* cartao = new QFrame(caixaFocos_);
        cartao->setProperty("classe", QStringLiteral("cartao"));
        auto* h = new QHBoxLayout(cartao);
        h->setContentsMargins(tema::esp(3), tema::esp(2), tema::esp(3), tema::esp(2));
        auto* nivel = new QLabel(QString::fromUtf8(niveis[std::clamp(f.nivel, 1, 3)]), cartao);
        nivel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600")
                                 .arg((f.nivel >= 3 ? tema::cor::atrasado() : tema::cor::urgente()).name()));
        nivel->setMinimumWidth(80);
        h->addWidget(nivel);
        auto* texto = new QLabel(cartao);
        texto->setTextFormat(Qt::PlainText);   // veio do agente: nunca HTML
        texto->setWordWrap(true);
        texto->setText(q(f.topico) + QStringLiteral(" — ") + nomes[f.idTurma] +
                       (f.motivo.empty() ? QString() : QStringLiteral("\n") + q(f.motivo)) +
                       QStringLiteral("\nmarcado por ") + q(f.origem) + QStringLiteral(" em ") +
                       quandoCurto(f.atualizadoEm));
        h->addWidget(texto, 1);
        auto* resolver = new QPushButton(QStringLiteral("Já domino"), cartao);
        auto* apagar = new QPushButton(QStringLiteral("Apagar"), cartao);
        resolver->setProperty("papel", QStringLiteral("secundario"));
        apagar->setProperty("papel", QStringLiteral("discreto"));
        for (QPushButton* b : {resolver, apagar}) {
            b->setAutoDefault(false);
            h->addWidget(b, 0, Qt::AlignVCenter);
        }
        const qint64 id = f.id;
        connect(resolver, &QPushButton::clicked, this, [this, id] {
            store::Database d(banco_.toStdString());
            d.resolverFoco(id, "resolvido pelo aluno", QDateTime::currentSecsSinceEpoch());
            atualizar();
            if (aoMudar) aoMudar();
        });
        connect(apagar, &QPushButton::clicked, this, [this, id] {
            store::Database d(banco_.toStdString());
            d.apagarDoAgente(store::Database::TabelaAgente::Foco, id);
            atualizar();
            if (aoMudar) aoMudar();
        });
        areaFocos_->addWidget(cartao);
    }
    if (abertos == 0) {
        areaFocos_->addWidget(nota(QStringLiteral("Nenhum ponto de foco aberto."), caixaFocos_));
    }

    // --- desempenho por tópico, somado ---
    std::map<std::pair<std::string, std::string>, std::pair<int, int>> acum;
    std::map<std::pair<std::string, std::string>, std::string> nomeTopico;
    for (const auto& d : desempenhos) {
        const auto k = std::make_pair(d.idTurma, util::dobrar(d.topico));
        acum[k].first += d.acertos;
        acum[k].second += d.total;
        if (!nomeTopico.count(k)) nomeTopico[k] = d.topico;
    }
    desempenho_->setRowCount(static_cast<int>(acum.size()));
    int r = 0;
    for (const auto& [k, v] : acum) {
        const int pct = v.second ? 100 * v.first / v.second : 0;
        desempenho_->setItem(r, 0, celula(q(nomeTopico[k])));
        desempenho_->setItem(r, 1, celula(nomes[k.first]));
        desempenho_->setItem(r, 2, celula(QStringLiteral("%1 de %2").arg(v.first).arg(v.second)));
        auto* p = celula(QStringLiteral("%1%").arg(pct));
        p->setForeground(pct < 50 ? tema::cor::atrasado() : pct < 70 ? tema::cor::urgente() : tema::cor::sucesso());
        desempenho_->setItem(r, 3, p);
        ++r;
    }
    // Pelo modo da seção, e não por largura calculada: a tabela nasce escondida
    // (estado vazio) e mediria zero de largura no primeiro cálculo.
    for (int col = 0; col < desempenho_->columnCount(); ++col) {
        desempenho_->horizontalHeader()->setSectionResizeMode(
            col, col == 0 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }

    // --- histórico: tudo, do mais novo ao mais velho ---
    struct Linha {
        QString ordem;
        QStringList cols;
        int tabela;
        qint64 id;
    };
    std::vector<Linha> linhas;
    for (const auto& x : estudos) {
        const QDate d(x.quando.year, x.quando.month, x.quando.day);
        QString det = duracao(x.minutos);
        if (!x.prova.empty()) det += QStringLiteral(" para ") + q(x.prova);
        if (!x.topicos.empty()) {
            QStringList ts;
            for (const auto& t : x.topicos) ts << q(t);
            det += QStringLiteral(": ") + ts.join(QStringLiteral(", "));
        }
        linhas.push_back({d.toString(Qt::ISODate) + QString::number(x.criadoEm),
                          {d.toString(QStringLiteral("dd/MM")), QStringLiteral("Estudo"),
                           nomes[x.idTurma], det, q(x.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Estudo), x.id});
    }
    for (const auto& x : desempenhos) {
        const QDate d(x.quando.year, x.quando.month, x.quando.day);
        linhas.push_back({d.toString(Qt::ISODate) + QString::number(x.criadoEm),
                          {d.toString(QStringLiteral("dd/MM")), QStringLiteral("Desempenho"),
                           nomes[x.idTurma],
                           QStringLiteral("%1: %2 de %3 (%4)").arg(q(x.topico)).arg(x.acertos).arg(x.total).arg(q(x.tipo)),
                           q(x.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Desempenho), x.id});
    }
    for (const auto& f : focos) {
        linhas.push_back({QDateTime::fromSecsSinceEpoch(f.atualizadoEm).date().toString(Qt::ISODate) +
                              QString::number(f.atualizadoEm),
                          {QDateTime::fromSecsSinceEpoch(f.atualizadoEm).toString(QStringLiteral("dd/MM")),
                           f.resolvido ? QStringLiteral("Foco resolvido") : QStringLiteral("Foco"),
                           nomes[f.idTurma], q(f.topico) + QStringLiteral(": ") + q(f.motivo), q(f.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Foco), f.id});
    }
    std::stable_sort(linhas.begin(), linhas.end(),
                     [](const Linha& a, const Linha& b) { return a.ordem > b.ordem; });
    historico_->setRowCount(static_cast<int>(linhas.size()));
    for (int i = 0; i < static_cast<int>(linhas.size()); ++i) {
        for (int col = 0; col < 5; ++col) historico_->setItem(i, col, celula(linhas[i].cols[col]));
        historico_->item(i, 0)->setData(PapelTabela, linhas[i].tabela);
        historico_->item(i, 0)->setData(PapelId, linhas[i].id);
        historico_->item(i, 3)->setToolTip(linhas[i].cols[3]);
    }
    for (int col = 0; col < historico_->columnCount(); ++col) {
        historico_->horizontalHeader()->setSectionResizeMode(
            col, col == 3 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }
}

} // namespace sigaa::ui
