#include "ui/DialogoAgentes.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
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

DialogoAgentes::DialogoAgentes(QString banco, QString materiais, QWidget* pai)
    : QDialog(pai), banco_(std::move(banco)), materiais_(std::move(materiais)) {
    setWindowTitle(QStringLiteral("Agentes de IA"));
    setModal(true);
    resize(860, 620);

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Agentes de IA"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    raiz->addWidget(nota(
        QStringLiteral("Conecte o app ao agente que você usa (Claude, Codex, Gemini, Cursor...) "
                       "para estudar com seus tópicos, provas e PDFs sem arrastar arquivo. O "
                       "agente lê os dados deste computador pelo protocolo MCP e pode devolver o "
                       "tempo de estudo e os pontos em que você precisa focar."),
        this));

    auto* abas = new QTabWidget(this);
    abas->addTab(montarPermissoes(), QStringLiteral("Permissões"));
    abas->addTab(montarConexoes(), QStringLiteral("Conectar"));
    abas->addTab(montarRegistros(), QStringLiteral("O que os agentes registraram"));
    abas->addTab(montarAtividade(), QStringLiteral("Atividade"));
    raiz->addWidget(abas, 1);
    // As tabelas leem o banco ao aparecer: o agente pode ter gravado algo
    // com esta janela aberta.
    connect(abas, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 2) atualizarRegistros();
        if (i == 3) atualizarAtividade();
        if (i == 1) atualizarConexoes();
    });

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Close, this);
    botoes->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fechar"));
    botoes->button(QDialogButtonBox::Close)->setAutoDefault(false);
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes);

    carregarPermissoes();
    atualizarConexoes();
    atualizarRegistros();
    atualizarAtividade();
}

// ---------------------------------------------------------------------------

QWidget* DialogoAgentes::montarPermissoes() {
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
                                   "e os pontos em que você precisa focar. Tudo aparece na aba "
                                   "\"O que os agentes registraram\", e você apaga quando quiser."));
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

void DialogoAgentes::carregarPermissoes() {
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

void DialogoAgentes::gravarPermissao(const char* nome, bool sim) {
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() ||
        !db.gravarMeta(std::string("mcp.") + nome, sim ? "1" : "0")) {
        QMessageBox::warning(this, QStringLiteral("Não salvo"),
                             QStringLiteral("Não consegui guardar a permissão."));
    }
}

// ---------------------------------------------------------------------------

QWidget* DialogoAgentes::montarConexoes() {
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

void DialogoAgentes::atualizarConexoes() {
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

QWidget* DialogoAgentes::montarRegistros() {
    auto* p = new QWidget(this);
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(tema::esp(3), tema::esp(4), tema::esp(3), tema::esp(3));
    v->setSpacing(tema::esp(2));
    resumoRegistros_ = new QLabel(p);
    resumoRegistros_->setWordWrap(true);
    v->addWidget(resumoRegistros_);

    registros_ = tabela({QStringLiteral("Quando"), QStringLiteral("O quê"), QStringLiteral("Turma"),
                         QStringLiteral("Detalhe"), QStringLiteral("Agente")},
                        p);
    v->addWidget(registros_, 1);

    auto* linha = new QHBoxLayout;
    auto* apagar = new QPushButton(QStringLiteral("Apagar o selecionado"), p);
    auto* apagarOrigem = new QPushButton(QStringLiteral("Apagar tudo de um agente…"), p);
    for (QPushButton* b : {apagar, apagarOrigem}) {
        b->setAutoDefault(false);
        b->setProperty("papel", QStringLiteral("secundario"));
        linha->addWidget(b);
    }
    linha->addStretch();
    v->addLayout(linha);

    connect(apagar, &QPushButton::clicked, this, [this] {
        const int r = registros_->currentRow();
        if (r < 0) return;
        const auto* it = registros_->item(r, 0);
        store::Database db(banco_.toStdString());
        db.apagarDoAgente(static_cast<store::Database::TabelaAgente>(it->data(PapelTabela).toInt()),
                          it->data(PapelId).toLongLong());
        atualizarRegistros();
    });
    connect(apagarOrigem, &QPushButton::clicked, this, [this] {
        std::set<QString> origens;
        for (int r = 0; r < registros_->rowCount(); ++r) origens.insert(registros_->item(r, 4)->text());
        if (origens.empty()) return;
        QStringList lista(origens.begin(), origens.end());
        bool ok = false;
        const QString escolha = QInputDialog::getItem(
            this, QStringLiteral("Apagar registros"),
            QStringLiteral("Apagar tudo que este agente registrou:"), lista, 0, false, &ok);
        if (!ok) return;
        store::Database db(banco_.toStdString());
        const int n = db.apagarTudoDoAgente(escolha.toStdString());
        resumoRegistros_->setText(QStringLiteral("%1 registro(s) de %2 apagado(s).").arg(n).arg(escolha));
        atualizarRegistros();
    });
    return p;
}

void DialogoAgentes::atualizarRegistros() {
    store::Database db(banco_.toStdString());
    registros_->setRowCount(0);
    if (!db.aberto() || !db.migrar()) return;
    std::map<std::string, QString> nomes;
    for (const auto& t : db.carregarUltimo().turmas) nomes[t.idTurma] = q(t.nome);

    struct Linha {
        QString ordem;   // para ordenar por data, mais recente primeiro
        QStringList cols;
        int tabela;
        qint64 id;
    };
    std::vector<Linha> linhas;
    int semana = 0;
    const QDate hoje = QDate::currentDate();
    for (const auto& r : db.carregarRegistrosEstudo()) {
        const QDate d(r.quando.year, r.quando.month, r.quando.day);
        if (d.daysTo(hoje) < 7) semana += r.minutos;
        QString det = duracao(r.minutos);
        if (!r.topicos.empty()) {
            QStringList ts;
            for (const auto& t : r.topicos) ts << q(t);
            det += QStringLiteral(": ") + ts.join(QStringLiteral(", "));
        }
        linhas.push_back({d.toString(Qt::ISODate) + QString::number(r.criadoEm),
                          {d.toString(QStringLiteral("dd/MM")), QStringLiteral("Estudo"),
                           nomes[r.idTurma], det, q(r.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Estudo), r.id});
    }
    for (const auto& x : db.carregarDesempenho()) {
        const QDate d(x.quando.year, x.quando.month, x.quando.day);
        linhas.push_back({d.toString(Qt::ISODate) + QString::number(x.criadoEm),
                          {d.toString(QStringLiteral("dd/MM")), QStringLiteral("Desempenho"),
                           nomes[x.idTurma],
                           QStringLiteral("%1: %2 de %3 (%4)")
                               .arg(q(x.topico)).arg(x.acertos).arg(x.total).arg(q(x.tipo)),
                           q(x.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Desempenho), x.id});
    }
    int abertos = 0;
    for (const auto& f : db.carregarFocos()) {
        if (!f.resolvido) ++abertos;
        static const char* niveis[] = {"", "atenção", "dificuldade", "crítico"};
        linhas.push_back({QDateTime::fromSecsSinceEpoch(f.atualizadoEm).date().toString(Qt::ISODate) +
                              QString::number(f.atualizadoEm),
                          {quandoCurto(f.atualizadoEm),
                           f.resolvido ? QStringLiteral("Foco resolvido") : QStringLiteral("Foco"),
                           nomes[f.idTurma],
                           QStringLiteral("%1 (%2): %3")
                               .arg(q(f.topico), QString::fromUtf8(niveis[std::clamp(f.nivel, 1, 3)]),
                                    q(f.motivo)),
                           q(f.origem)},
                          static_cast<int>(store::Database::TabelaAgente::Foco), f.id});
    }
    std::stable_sort(linhas.begin(), linhas.end(),
                     [](const Linha& a, const Linha& b) { return a.ordem > b.ordem; });
    registros_->setRowCount(static_cast<int>(linhas.size()));
    for (int r = 0; r < static_cast<int>(linhas.size()); ++r) {
        for (int c = 0; c < 5; ++c) registros_->setItem(r, c, celula(linhas[r].cols[c]));
        registros_->item(r, 3)->setToolTip(linhas[r].cols[3]);
        registros_->item(r, 0)->setData(PapelTabela, linhas[r].tabela);
        registros_->item(r, 0)->setData(PapelId, linhas[r].id);
    }
    registros_->resizeColumnsToContents();
    // "Detalhe" absorve a sobra e corta o excesso: esticada pelo conteúdo,
    // empurrava a coluna "Agente" — quem escreveu — para fora da tela.
    tema::esticarColuna(registros_, 3);
    registros_->horizontalHeader()->setStretchLastSection(false);
    resumoRegistros_->setText(
        linhas.empty()
            ? QStringLiteral("Nada registrado ainda. Com \"Registrar estudo no app\" liberado, o "
                             "agente grava aqui o que você estudou com ele.")
            : QStringLiteral("%1 de estudo nos últimos 7 dias · %2 ponto(s) de foco aberto(s)")
                  .arg(duracao(semana))
                  .arg(abertos));
}

// ---------------------------------------------------------------------------

QWidget* DialogoAgentes::montarAtividade() {
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

void DialogoAgentes::atualizarAtividade() {
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

} // namespace sigaa::ui
