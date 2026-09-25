#include "ui/JanelaPrincipal.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTreeView>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTabWidget>
#include <QResizeEvent>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QItemSelectionModel>
#include <QToolBar>
#include <QPushButton>
#include <QAction>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>

#include "core/store/Database.h"
#include "platform/Credenciais.h"
#include "ui/CalendarioProvas.h"
#include "ui/DialogoAtualizar.h"
#include "ui/DialogoLogin.h"
#include "core/atualizacao/Atualizador.h"
#include "ui/DialogoOpcoes.h"
#include "ui/Copiar.h"
#include "ui/DialogosAvaliacao.h"
#include "ui/Distintivos.h"
#include "ui/Icones.h"
#include "ui/JanelaDiagnostico.h"
#include "ui/JanelaTurma.h"
#include "ui/Modelos.h"
#include "ui/Tema.h"
#include "ui/Trabalhador.h"
#include "ui_JanelaPrincipal.h"

namespace sigaa::ui {
namespace {

// Etiqueta com o servidor da universidade (docs/PLANO.md §2.4): 15–30 min,
// nunca abaixo de 5. O ciclo caro é raro de propósito — prova quase nunca muda,
// e entrar nas 7 turmas custa ~15 requisições contra 1 do portal.
// Os padrões da rotina automática, e o piso do que ela pode fazer.
//
// DUAS HORAS no portal: nada do que ele traz muda mais rápido que isso — prazo
// é publicado com dias de antecedência, notícia de turma não é jornal. UMA VEZ
// POR DIA nas turmas, porque o ciclo completo entra em cada uma (4 requisições
// por turma) e o que ele busca muda em escala de dias.
//
// Os dois têm de existir na lista de `DialogoOpcoes`, senão a normalização em
// `aplicarConfig` cairia num valor que a interface não sabe mostrar.
constexpr int kMinutosPortal = 120;
constexpr int kMinutosTurmas = 24 * 60;

// A página da agenda é a semana — a unidade em que a grade horária existe e em
// que o aluno pensa. Sete dias cabem na tela sem rolagem no tamanho padrão da
// janela, então "virar a página" mostra a página inteira de uma vez.
constexpr int kDiasPorPagina = 7;

// Um "clique" de roda são 120 oitavos de grau (QWheelEvent). Trackpad manda
// frações; o acumulador só vira a página quando somam um clique.
constexpr int kPassoRolagem = 120;

QDate segundaDaSemana(QDate d) {
    return d.isValid() ? d.addDays(-(d.dayOfWeek() - Qt::Monday)) : d;
}

// "10 – 16 de agosto", ou "31 de agosto – 6 de setembro" quando a semana cruza
// o mês. O ano fica de fora: a agenda é de um semestre, e repetir "2026" duas
// vezes por linha de título não informa nada.
QString faixaDaSemana(QDate inicio, QDate fim) {
    const QLocale l;
    if (inicio.month() == fim.month()) {
        return QStringLiteral("%1 – %2")
            .arg(inicio.day())
            .arg(l.toString(fim, QStringLiteral("d 'de' MMMM")));
    }
    return QStringLiteral("%1 – %2")
        .arg(l.toString(inicio, QStringLiteral("d 'de' MMMM")),
             l.toString(fim, QStringLiteral("d 'de' MMMM")));
}

// Troca o modelo da tabela preservando o proxy de ordenação, e destrói o
// modelo velho — sem isto cada sync vazaria um QStandardItemModel.
// Serve tabela e ÁRVORE: Prazos e Provas viraram árvores para ganhar as seções
// recolhíveis, e Turmas/Atualizações continuam tabelas. A troca de modelo é a
// mesma nos dois — quem muda é como se mede a largura das colunas.
void trocarModelo(QAbstractItemView* tv, QStandardItemModel* novo, int colunaPadrao,
                  int colunaElastica) {
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tv->model());
    if (!proxy) {
        proxy = new QSortFilterProxyModel(tv);
        proxy->setSortRole(PapelOrdenacao);
        tv->setModel(proxy);
    }
    auto* antigo = proxy->sourceModel();
    novo->setParent(proxy);
    proxy->setSourceModel(novo);
    delete antigo;

    if (auto* tabela = qobject_cast<QTableView*>(tv)) {
        tabela->sortByColumn(colunaPadrao, Qt::AscendingOrder);
        tabela->resizeColumnsToContents();
    } else if (auto* arvore = qobject_cast<QTreeView*>(tv)) {
        arvore->sortByColumn(colunaPadrao, Qt::AscendingOrder);

        // MEDIR COM TUDO ABERTO, e só então recolher: `resizeColumnToContents`
        // enxerga apenas as linhas visíveis, e medindo antes de expandir as
        // colunas saíam do tamanho dos cabeçalhos de seção — "12/09/2026…" e
        // "INTELIGÊN…" cortados enquanto sobrava meia tela à direita.
        arvore->expandAll();
        for (int c = 0; c < novo->columnCount(); ++c) arvore->resizeColumnToContents(c);

        // A ÚLTIMA seção é sempre o histórico (a chave de ordenação do grupo é
        // a posição). Ele existe para sair da frente; nascer aberto desfaria o
        // ponto inteiro.
        auto* mod = arvore->model();
        const int linhas = mod ? mod->rowCount() : 0;
        if (linhas > 1) arvore->setExpanded(mod->index(linhas - 1, 0), false);
    }
    // Depois do resize, e a cada troca de modelo: setSectionResizeMode morre
    // junto com o cabeçalho do modelo velho.
    tema::esticarColuna(tv, colunaElastica);
}

// Escala relativa à fonte do sistema, nunca em pt cravado: quem aumenta a
// fonte no Windows para enxergar espera que o app inteiro acompanhe.
void escalarFonte(QWidget* w, qreal fator, bool negrito = false) {
    QFont f = w->font();
    f.setPointSizeF(f.pointSizeF() * fator);
    f.setBold(negrito);
    w->setFont(f);
}

// Cinza de texto secundário. Não dá para fazer no QSS: o `palette(...)` do QSS
// só aceita a lista fechada de papéis do parser, e placeholder-text — o único
// que significa "cinza legível nos dois temas" — não está nela; a regra seria
// descartada em silêncio (ver a nota em estilo.qss).
void esmaecer(QWidget* w) {
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
    w->setPalette(p);
}

} // namespace

JanelaPrincipal::JanelaPrincipal(QWidget* pai)
    : QMainWindow(pai), formulario_(std::make_unique<Ui::JanelaPrincipal>()) {
    formulario_->setupUi(this);

    montarListas();
    montarAbaLembrada();
    montarAcoes();
    montarStatus();
    montarProvas();
    // DEPOIS de `montarProvas`, e a ordem não é estilo.
    //
    // `montarProvas` chama `tvProvas->setModel`, e `setModel` CRIA um modelo de
    // seleção novo — jogando fora qualquer conexão feita ao anterior. Com a
    // ordem invertida, a view nem tinha modelo ainda: `selectionModel()` era
    // nulo, o `connect` não fazia nada (silenciosamente), e "Corrigir data",
    // "Confirmar" e "Desfazer" ficavam presos em desabilitado para sempre.
    montarBotoesProva();
    montarTurmas();
    montarBarraAgenda();
    montarBandeja();

    if (!recarregarDoBanco()) {
        mostrar({});
        status(QStringLiteral("Banco indisponível — use Atualizar para buscar do SIGAA."));
    } else if (snapshot_.turmas.empty()) {
        status(QStringLiteral("Nenhuma coleta ainda. Clique em Atualizar."));
    } else {
        status(QStringLiteral("Último estado conhecido: %1 turmas, %2 atividades. "
                              "Atualize para buscar novidades.")
                   .arg(snapshot_.turmas.size())
                   .arg(snapshot_.atividades.size()));
    }

    // Onboarding depois que o laço de eventos começa. Abrir um diálogo modal
    // de dentro do construtor, antes de a janela existir, dá diálogo órfão e
    // ordem de destruição confusa.
    QTimer::singleShot(0, this, &JanelaPrincipal::aoAbrir);
}

JanelaPrincipal::~JanelaPrincipal() {
    // Encerra a sessão no SIGAA ao fechar o app.
    //
    // Não é higiene: sem isto a sessão fica viva no servidor por até 30
    // minutos depois que a janela some, e quem fecha e reabre o app nesse
    // intervalo cai exatamente no problema que a SessaoViva veio resolver —
    // um login novo com outra sessão ainda aberta na conta, que o SIGAA deixa
    // sem resposta até estourar o timeout.
    //
    // Custa uma requisição no fechamento. É o único momento em que deslogar
    // vale a pena: entre tarefas, reusar é sempre melhor.
    sessaoViva_.encerrar();
}

void JanelaPrincipal::changeEvent(QEvent* ev) {
    QMainWindow::changeEvent(ev);
    if (ev->type() == QEvent::PaletteChange || ev->type() == QEvent::ThemeChange) {
        aplicarIcones();
    }
}

void JanelaPrincipal::aplicarIcones() {
    // changeEvent é virtual e pode ser chamado durante setupUi(), quando as
    // ações ainda não existem — o polish do stylesheet dispara PaletteChange.
    if (!formulario_ || !formulario_->acAtualizar) return;

    formulario_->acAtualizar->setIcon(icone(QStringLiteral("atualizar"), this));
    formulario_->acAtualizarTudo->setIcon(
        icone(QStringLiteral("atualizar-tudo"), this));
    // `acAuto` saiu da barra: ligar e desligar a rotina é uma decisão que se
    // toma uma vez por semestre, e ela ocupava um lugar na barra ao lado de
    // ações que se usam todo dia. Vive em Opções, junto dos intervalos que
    // ela comanda — separar o interruptor da configuração dele era o que
    // fazia alguém ligar o automático sem nunca ver de quanto em quanto tempo
    // ele ia rodar.
    formulario_->acConta->setIcon(icone(QStringLiteral("conta"), this));
}

// Densidade e distintivos de todas as listas, num lugar só. Cada aba tem sua
// função de montagem, mas acabamento de lista não é assunto de aba: se cada
// uma decidisse por conta própria, a tabela de Prazos e a de Provas acabariam
// com alturas de linha diferentes e ninguém saberia dizer por quê.
void JanelaPrincipal::resizeEvent(QResizeEvent* ev) {
    QMainWindow::resizeEvent(ev);
    ajustarBarraAoEspaco();
    ajustarBarraProvasAoEspaco();
}

void JanelaPrincipal::ajustarBarraProvasAoEspaco() {
    if (larguraBarraProvas_ <= 0) return;

    // A barra da aba Provas tem cinco botões, e junto com o calendário ao lado
    // eles fixavam o tamanho MÍNIMO da janela inteira em ~1300 px: a aba não
    // encolhia, e redimensionar não fazia nada.
    //
    // Abaixo do que cabe, os três botões que agem sobre a prova SELECIONADA
    // somem da barra — eles continuam no menu do botão direito, que é onde se
    // procura o que fazer com uma linha. Ficam "Nova prova" e "Histórico", que
    // não dependem de seleção e não teriam outro caminho óbvio.
    const bool cabe = width() >= larguraBarraProvas_ + kLarguraCalendario;
    for (QWidget* b : botoesProvaSecundarios_) {
        if (b) b->setVisible(cabe);
    }
}

void JanelaPrincipal::ajustarBarraAoEspaco() {
    auto* barra = formulario_->barraAcoes;
    if (larguraBarraComTexto_ <= 0) return;   // ainda não medida

    // Compara sempre contra a largura medida no estado COM texto, nunca contra
    // a do estado atual: usar a atual criaria uma realimentação — encolher para
    // só-ícone reduziria a largura pedida, o teste passaria a caber, o rótulo
    // voltaria, não caberia de novo, e a barra piscaria a cada pixel.
    const bool cabe = width() >= larguraBarraComTexto_;
    const auto desejado =
        cabe ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly;
    if (barra->toolButtonStyle() == desejado) return;

    barra->setToolButtonStyle(desejado);
    // O rótulo vira dica: em modo só-ícone, sem isto o aluno fica com seis
    // desenhos e nenhuma palavra.
    for (QAction* a : barra->actions()) {
        if (a->toolTip().isEmpty()) a->setToolTip(a->text());
    }
}


// ---------------------------------------------------------------------------
// Correções de data de prova
// ---------------------------------------------------------------------------

void JanelaPrincipal::recalcularProvas() {
    provas_ = avaliacao::efetivas(snapshot_.avaliacoes, ajustes_);
}

void JanelaPrincipal::recarregarAjustes() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) return;
    ajustes_ = db.carregarAjustes();
}

std::optional<avaliacao::Efetiva> JanelaPrincipal::provaSelecionada() const {
    const auto sel = formulario_->tvProvas->selectionModel();
    if (!sel || !sel->hasSelection()) return std::nullopt;

    // Pela CHAVE guardada na linha, nunca pelo índice: a tabela é ordenável, e
    // a linha 3 de agora não é a linha 3 de depois de um clique no cabeçalho.
    const QModelIndex idx = sel->selectedRows(0).value(0);
    if (!idx.isValid()) return std::nullopt;
    const std::string idTurma = idx.data(PapelIdTurmaProva).toString().toStdString();
    const std::string desc = idx.data(PapelDescricaoProva).toString().toStdString();

    for (const auto& p : provas_) {
        if (p.av.idTurma == idTurma && p.av.descricao == desc) return p;
    }
    return std::nullopt;
}

bool JanelaPrincipal::gravarAjuste(const avaliacao::Ajuste& a,
                                   avaliacao::TipoMudanca tipo, const std::string& de) {
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        status(QStringLiteral("Banco indisponível — a correção não foi salva."));
        return false;
    }
    if (!db.gravarAjuste(a)) {
        status(QStringLiteral("Não consegui salvar a correção: %1")
                   .arg(QString::fromStdString(db.erro())));
        return false;
    }

    // O histórico é gravado junto, sempre. Uma correção sem registro deixa a
    // pergunta "por que esta data mudou?" sem resposta três semanas depois —
    // que é exatamente o problema que este recurso existe para resolver.
    avaliacao::Mudanca m;
    m.idTurma = a.idTurma;
    m.turmaNome = a.turmaNome;
    m.descricao = a.descricao;
    m.tipo = tipo;
    m.de = de;
    m.para = a.quando.toIso();
    m.nota = a.nota;
    m.quando = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());
    db.registrarMudanca(m);

    recarregarAjustes();
    mostrar(snapshot_);
    return true;
}

void JanelaPrincipal::corrigirProva() {
    const auto sel = provaSelecionada();
    if (!sel) {
        status(QStringLiteral("Selecione a prova cuja data você quer corrigir."));
        return;
    }

    DialogoAvaliacao dlg(*sel, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto aj = dlg.resultado();
    if (!aj.quando.valid()) return;

    if (gravarAjuste(aj, avaliacao::TipoMudanca::AlunoCorrigiu, sel->av.quando.toIso())) {
        status(QStringLiteral("Data corrigida. Ela vale no app, no calendário e no "
                              ".ics — até o professor atualizar o SIGAA."));
    }
}

void JanelaPrincipal::confirmarProva() {
    const auto sel = provaSelecionada();
    if (!sel) {
        status(QStringLiteral("Selecione a prova que você quer confirmar."));
        return;
    }

    avaliacao::Ajuste aj;
    aj.idTurma = sel->av.idTurma;
    aj.descricao = sel->av.descricao;
    aj.turmaNome = sel->av.turmaNome;
    aj.confirmada = true;
    // Confirmar NÃO muda a data: guarda a mesma, e é isso que faz a prova sair
    // da lista de "confirme com o professor" sem mexer no que ela anuncia.
    aj.quando = sel->av.quando;
    aj.quandoSigaaNaEpoca = sel->quandoSigaa;
    aj.nota = sel->nota;

    if (gravarAjuste(aj, avaliacao::TipoMudanca::AlunoConfirmou, {})) {
        status(QStringLiteral("Confirmada. O app para de pedir confirmação para esta "
                              "prova."));
    }
}

void JanelaPrincipal::criarProva() {
    if (snapshot_.turmas.empty()) {
        status(QStringLiteral("Preciso conhecer suas turmas antes. Clique em Atualizar."));
        return;
    }

    DialogoAvaliacao dlg(snapshot_.turmas, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto aj = dlg.resultado();
    if (aj.descricao.empty() || !aj.quando.valid()) return;

    if (gravarAjuste(aj, avaliacao::TipoMudanca::AlunoCriou, {})) {
        status(QStringLiteral("Prova cadastrada. Ela entra no calendário e no .ics "
                              "como qualquer outra."));
    }
}

void JanelaPrincipal::desfazerCorrecao() {
    const auto sel = provaSelecionada();
    if (!sel) return;

    store::Database db;
    if (!db.aberto() || !db.migrar()) return;

    avaliacao::Mudanca m;
    m.idTurma = sel->av.idTurma;
    m.turmaNome = sel->av.turmaNome;
    m.descricao = sel->av.descricao;
    m.tipo = avaliacao::TipoMudanca::AlunoDesfez;
    m.de = sel->av.quando.toIso();
    m.para = sel->quandoSigaa.toIso();
    m.quando = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());
    // O histórico registra o desfazer ANTES de apagar a correção: depois do
    // DELETE não há mais de onde tirar a data que estava valendo.
    db.registrarMudanca(m);
    db.removerAjuste(sel->av.idTurma, sel->av.descricao);

    recarregarAjustes();
    mostrar(snapshot_);
    status(QStringLiteral("Correção desfeita. Voltou a valer o que o SIGAA diz."));
}

void JanelaPrincipal::verHistorico() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        status(QStringLiteral("Banco indisponível — não consigo ler o histórico."));
        return;
    }

    // Com uma prova selecionada, o histórico dela; sem seleção, o de todas. É
    // a diferença entre "por que ESTA data mudou?" e "o que andou mudando?".
    const auto sel = provaSelecionada();
    const auto mudancas = sel ? db.historico(sel->av.idTurma, sel->av.descricao)
                              : db.historico();
    const QString titulo = sel ? QStringLiteral("Histórico — %1")
                                     .arg(QString::fromStdString(sel->av.descricao))
                               : QStringLiteral("Histórico de datas de prova");

    DialogoHistorico(paraHistorico(mudancas), titulo,
                     QStringLiteral(
                         "Nenhuma mudança de data registrada ainda. O histórico "
                         "começa na sua primeira correção, ou na primeira vez que "
                         "um professor remarcar uma prova."),
                     this)
        .exec();
}

void JanelaPrincipal::avisarConflitos(const std::vector<avaliacao::Conflito>& cs) {
    if (cs.empty()) return;

    // Modal, e sem pedir desculpas por isso: o ciclo acabou de descartar uma
    // data que o usuário digitou à mão. É a única coisa que o sync faz com o
    // que ele escreveu, e uma linha na barra de status seria perdida.
    QString corpo;
    for (const auto& c : cs) {
        corpo += QStringLiteral("• %1 (%2)\n    sua data: %3\n    agora no SIGAA: %4\n\n")
                     .arg(QString::fromStdString(c.descricao),
                          QString::fromStdString(c.turmaNome),
                          QString::fromStdString(c.doAluno.toIso()),
                          QString::fromStdString(c.doSigaaAgora.toIso()));
    }

    QMessageBox cx(this);
    cx.setIcon(QMessageBox::Warning);
    cx.setWindowTitle(QStringLiteral("O professor mudou a data da prova"));
    cx.setText(cs.size() == 1
                   ? QStringLiteral("O professor atualizou o SIGAA e a data dele "
                                    "substituiu a sua correção.")
                   : QStringLiteral("O professor atualizou o SIGAA em %1 provas e as "
                                    "datas dele substituíram suas correções.")
                         .arg(cs.size()));
    cx.setInformativeText(
        corpo + QStringLiteral("A data do SIGAA passou a valer. Sua correção ficou no "
                               "histórico — se você souber que o professor está errado, "
                               "corrija de novo."));
    cx.addButton(QStringLiteral("Ver histórico"), QMessageBox::AcceptRole);
    auto* ok = cx.addButton(QStringLiteral("Entendi"), QMessageBox::RejectRole);
    cx.setDefaultButton(ok);
    cx.exec();
    if (cx.clickedButton() != ok) verHistorico();
}

void JanelaPrincipal::montarBotoesProva() {
    auto* linha = formulario_->layoutFiltro;
    if (!linha) return;

    // QAction, e não QToolButton solto: a MESMA ação precisa aparecer em dois
    // lugares — na barra acima da lista e no menu do botão direito sobre a
    // prova. Com QAction, texto, dica e estado de habilitação existem uma vez
    // só; com dois botões, "Corrigir data" ficaria habilitado num lugar e
    // desabilitado no outro no dia em que alguém mexesse só num deles.
    auto nova = [this, linha](const QString& texto, const QString& dica,
                              void (JanelaPrincipal::*slot)(), bool secundario = false,
                              bool naBarra = true) {
        auto* a = new QAction(texto, this);
        a->setToolTip(dica);
        connect(a, &QAction::triggered, this, slot);
        if (naBarra) {
            auto* b = new QToolButton(this);
            b->setDefaultAction(a);
            linha->insertWidget(linha->count() - 1, b);
            if (secundario) botoesProvaSecundarios_.push_back(b);
        }
        formulario_->tvProvas->addAction(a);
        return a;
    };

    acCorrigirProva_ = nova(
        QStringLiteral("Corrigir data"),
        QStringLiteral("O professor remarcou e não atualizou o SIGAA? Ponha aqui a "
                       "data certa. Vale no app, no calendário e no .ics."),
        &JanelaPrincipal::corrigirProva, /*secundario=*/true);
    acConfirmarProva_ = nova(
        QStringLiteral("Confirmar"),
        QStringLiteral("Dar a data como boa. Some o aviso de \"inferido — confirme\"."),
        &JanelaPrincipal::confirmarProva, /*secundario=*/true);
    acDesfazerProva_ = nova(
        QStringLiteral("Desfazer correção"),
        QStringLiteral("Remover sua correção e voltar ao que o SIGAA diz."),
        &JanelaPrincipal::desfazerCorrecao, /*secundario=*/true);

    auto* sep = new QAction(this);
    sep->setSeparator(true);
    formulario_->tvProvas->addAction(sep);

    acNovaProva_ = nova(QStringLiteral("Nova prova"),
                        QStringLiteral("Cadastrar uma prova que o professor anunciou "
                                       "mas nunca colocou no SIGAA."),
                        &JanelaPrincipal::criarProva);
    acHistoricoProva_ = nova(QStringLiteral("Histórico"),
                             QStringLiteral("Todas as mudanças de data, suas e do "
                                            "professor, em ordem."),
                             &JanelaPrincipal::verHistorico);

    connect(formulario_->tvProvas->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this] { atualizarBotoesProva(); });
    atualizarBotoesProva();

    // Medida DEPOIS que o laço de eventos assentar o layout, e uma vez só.
    //
    // `sizeHint()` logo após os `insertWidget` devolve o tamanho de antes dos
    // botões: o layout ainda não recalculou. A primeira versão media aqui
    // mesmo, ficava com um número pequeno demais, e por isso os botões nunca
    // sumiam — a janela seguia sem conseguir encolher.
    //
    // Uma vez só porque comparar contra a largura corrente criaria a
    // realimentação que faz a barra piscar (a armadilha de
    // `ajustarBarraAoEspaco`).
    QTimer::singleShot(0, this, [this, linha] {
        larguraBarraProvas_ = linha->sizeHint().width();
        ajustarBarraProvasAoEspaco();
    });
}

void JanelaPrincipal::atualizarBotoesProva() {
    // Guarda contra ordem de montagem: se um dia alguém chamar isto antes de
    // `montarBotoesProva`, é melhor não fazer nada do que desreferenciar nulo.
    if (!acCorrigirProva_) return;

    const auto sel = provaSelecionada();
    const bool tem = sel.has_value();

    acCorrigirProva_->setEnabled(tem);
    // Confirmar só faz sentido no que ainda não foi confirmado nem corrigido.
    acConfirmarProva_->setEnabled(tem && sel->estado == avaliacao::Estado::Inferida);
    // Desfazer só onde há o que desfazer.
    acDesfazerProva_->setEnabled(
        tem && (sel->estado == avaliacao::Estado::Editada ||
                sel->estado == avaliacao::Estado::Confirmada ||
                sel->estado == avaliacao::Estado::Criada));
}

void JanelaPrincipal::montarAbaLembrada() {
    // A aba em que o app abre é a última em que o aluno estava.
    //
    // Quem entrou para conferir faltas volta ao app para conferir faltas; cair
    // sempre na Agenda obriga um clique que não informa nada. Uma linha no
    // QSettings, guardada na troca — barato, e some se o índice não existir
    // mais numa versão futura com outras abas.
    auto* abas = formulario_->abas;
    const int lembrada = QSettings().value(QStringLiteral("ui/aba"), 0).toInt();
    if (lembrada >= 0 && lembrada < abas->count()) abas->setCurrentIndex(lembrada);

    connect(abas, &QTabWidget::currentChanged, this, [](int i) {
        QSettings().setValue(QStringLiteral("ui/aba"), i);
    });
}

void JanelaPrincipal::montarListas() {
    auto* dist = new DelegadoDistintivo(this);

    for (QAbstractItemView* v : {static_cast<QAbstractItemView*>(formulario_->tvPrazos),
                                 static_cast<QAbstractItemView*>(formulario_->tvProvas),
                                 static_cast<QAbstractItemView*>(formulario_->tvTurmas),
                                 static_cast<QAbstractItemView*>(formulario_->tvAtualizacoes),
                                 static_cast<QAbstractItemView*>(formulario_->arvoreHoje)}) {
        tema::ajustarLista(v);
        // Ctrl+C em toda lista: o resumo do semestre é a coisa que mais se
        // quer levar para outro lugar — planilha, mensagem, prompt de IA.
        habilitarCopia(v);
    }
    // Coluna "Faltas" da agenda: o "n/k" vira distintivo quando o número
    // começa a importar. Ver celulaFaltas em Modelos.cpp.
    // Instalado na VIEW inteira, não só na coluna de faltas: além de desenhar
    // a etiqueta onde há uma, o delegate garante altura mínima em toda célula
    // — e é dele que a árvore tira o respiro que a tabela recebe do
    // `defaultSectionSize`. Célula sem distintivo cai no desenho padrão.
    formulario_->arvoreHoje->setItemDelegate(dist);

    // Só onde há coluna de estado. Instalar na tabela inteira é seguro (célula
    // sem PapelDistintivo cai no desenho padrão), mas restringir deixa claro
    // no código quais colunas carregam significado.
    formulario_->tvPrazos->setItemDelegate(dist);
    formulario_->tvProvas->setItemDelegate(dist);
    formulario_->tvTurmas->setItemDelegate(dist);
    formulario_->tvAtualizacoes->setItemDelegate(dist);
}

void JanelaPrincipal::montarAcoes() {
    aplicarIcones();

    // O atalho fica em código porque QKeySequence::Refresh é F5 no Windows e no
    // Linux mas Cmd+R no macOS; o .ui só aceita a tecla literal e cravaria F5
    // nos três.
    formulario_->acAtualizar->setShortcut(QKeySequence::Refresh);

    // "Atualizar" agora PERGUNTA o que buscar. O ciclo só-portal continua
    // existindo — é a opção marcada por padrão no diálogo — mas deixou de ser
    // a única coisa que este botão sabe fazer.
    connect(formulario_->acAtualizar, &QAction::triggered, this,
            &JanelaPrincipal::escolherEAtualizar);
    connect(formulario_->acOpcoes, &QAction::triggered, this,
            &JanelaPrincipal::abrirOpcoes);
    connect(formulario_->acAtualizarTudo, &QAction::triggered, this,
            [this] { sincronizar(true); });
    // `acRelatorio` e `acDiagnostico` saíram da barra: são ferramentas de
    // investigar o app, não de usá-lo, e ocupavam o lugar mais valioso da
    // janela com telas que ninguém abre duas vezes. Vivem em Opções agora.
    //
    // `acDiagnostico` continua existindo pelo ATALHO — Ctrl+D é o que alguém
    // no telefone com o suporte consegue seguir sem procurar menu.

    // Ctrl+D, e não F12: F12 é "ferramentas do desenvolvedor" em navegador, e
    // esta janela não é para desenvolvedor — é para o aluno responder "por que
    // o SIGAA me bloqueou?" com evidência na mão.
    formulario_->acDiagnostico->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(formulario_->acDiagnostico, &QAction::triggered, this,
            &JanelaPrincipal::abrirDiagnostico);

    connect(formulario_->acTrocarConta, &QAction::triggered, this,
            &JanelaPrincipal::trocarConta);
    connect(formulario_->acEsquecerConta, &QAction::triggered, this,
            &JanelaPrincipal::esquecerConta);

    auto* menuConta = new QMenu(this);
    menuConta->addAction(formulario_->acTrocarConta);
    menuConta->addAction(formulario_->acEsquecerConta);
    menuConta->addSeparator();
    // Onde a senha está guardada é informação do usuário, não detalhe interno:
    // ele precisa saber o que remover se quiser sair da máquina limpo. Texto
    // vem de plat::, então é montado aqui e não no Designer.
    menuConta->addAction(QStringLiteral("Cofre: %1")
                             .arg(QString::fromStdString(plat::backendCofre())))
        ->setEnabled(false);

    formulario_->acConta->setMenu(menuConta);
    if (auto* botao = qobject_cast<QToolButton*>(
            formulario_->barraAcoes->widgetForAction(formulario_->acConta))) {
        botao->setPopupMode(QToolButton::InstantPopup);
    }

    // Medida AQUI, com todas as ações já na barra e os ícones já aplicados, e
    // enquanto ela ainda está no estado "texto ao lado do ícone". É o número
    // contra o qual todo resize será comparado daqui em diante.
    larguraBarraComTexto_ = formulario_->barraAcoes->sizeHint().width();
    ajustarBarraAoEspaco();
}

void JanelaPrincipal::montarStatus() {
    rotulo_ = new QLabel;
    barra_ = new QProgressBar;
    barra_->setRange(0, 0);          // indeterminado: não dá para prever o SIGAA
    barra_->setMaximumWidth(140);
    barra_->setVisible(false);
    formulario_->statusbar->addWidget(rotulo_, 1);
    formulario_->statusbar->addPermanentWidget(barra_);
}

void JanelaPrincipal::montarProvas() {
    // O calendário tem tamanho natural (um mês cabe em ~330 px) e não ganha
    // nada com mais largura; a lista ganha tudo. Sem os fatores, o splitter
    // divide meio a meio e a coluna "Avaliação" fica com "Pro…" ao lado de um
    // calendário com meia tela de vazio embaixo.
    if (auto* div = formulario_->divisorProvas) {
        div->setStretchFactor(0, 0);   // painel do calendário
        div->setStretchFactor(1, 1);   // lista de provas
    }
    if (auto* painel = formulario_->painelCalendario) {
        painel->setMaximumWidth(380);
    }

    // O proxy desta tabela nasce aqui, e não no `trocarModelo`, porque ela
    // FILTRA além de ordenar — e o filtro precisa sobreviver à troca de modelo
    // que todo sync faz. Criar depois significaria perder o dia selecionado a
    // cada 20 minutos.
    auto* proxy = new QSortFilterProxyModel(formulario_->tvProvas);
    proxy->setSortRole(PapelOrdenacao);
    // Filtra pela chave ISO da coluna Data, não pelo texto "04/09/2026": o
    // texto é para o olho e muda com o locale; a chave é estável.
    proxy->setFilterRole(PapelOrdenacao);
    proxy->setFilterKeyColumn(0);
    // As provas agora moram dentro de seções. Sem filtragem recursiva o proxy
    // testaria só as linhas de topo — que são cabeçalhos sem data — e o filtro
    // por dia esvaziaria a aba inteira.
    proxy->setRecursiveFilteringEnabled(true);
    formulario_->tvProvas->setModel(proxy);

    for (QLabel* l : {formulario_->tituloCartaoProxima, formulario_->tituloCartaoTrinta,
                      formulario_->tituloCartaoConfirmar}) {
        escalarFonte(l, 0.82, /*negrito=*/true);
        esmaecer(l);
    }
    for (QLabel* l : {formulario_->valorCartaoProxima, formulario_->valorCartaoTrinta,
                      formulario_->valorCartaoConfirmar}) {
        escalarFonte(l, 1.45, /*negrito=*/true);
    }
    for (QLabel* l : {formulario_->detalheCartaoProxima, formulario_->detalheCartaoTrinta,
                      formulario_->detalheCartaoConfirmar, formulario_->legendaCalendario,
                      formulario_->rotuloFiltro}) {
        escalarFonte(l, 0.9);
        esmaecer(l);
    }

    // O calendário tem largura natural; a lista é quem deve engolir a sobra ao
    // maximizar a janela.
    formulario_->divisorProvas->setStretchFactor(0, 0);
    formulario_->divisorProvas->setStretchFactor(1, 1);
    formulario_->divisorProvas->setSizes({340, 660});

    connect(formulario_->calProvas, &QCalendarWidget::clicked, this, [this](QDate d) {
        // Clicar de novo no mesmo dia desfaz o filtro. É o gesto que as pessoas
        // tentam antes de procurar um botão, e negá-lo faz o botão parecer a
        // única saída de um beco.
        filtrarProvasPorDia(d == diaFiltrado_ ? QDate() : d);
    });
    connect(formulario_->botaoTodasProvas, &QToolButton::clicked, this,
            [this] { filtrarProvasPorDia(QDate()); });
}

void JanelaPrincipal::atualizarResumoProvas(const Snapshot& s) {
    const ResumoProvas r = resumoProvas(provas_);
    formulario_->calProvas->definirProvas(provasPorDia(provas_));

    auto pintar = [](QLabel* l, const QColor& c) {
        QPalette p = l->palette();
        p.setColor(QPalette::WindowText, c);
        l->setPalette(p);
    };

    if (r.data.isValid()) {
        formulario_->valorCartaoProxima->setText(
            r.emDias == 0   ? QStringLiteral("é hoje")
            : r.emDias == 1 ? QStringLiteral("é amanhã")
                            : QStringLiteral("em %1 dias").arg(r.emDias));
        formulario_->detalheCartaoProxima->setText(
            QStringLiteral("%1 · %2 · %3")
                .arg(r.descricao, r.turma, r.data.toString(QStringLiteral("dd/MM"))));
        pintar(formulario_->valorCartaoProxima,
               r.emDias <= 7 ? cor::urgente()
                             : palette().color(QPalette::WindowText));
    } else {
        formulario_->valorCartaoProxima->setText(QStringLiteral("nenhuma"));
        // Distinguir "acabou o semestre" de "nunca entrei nas turmas" importa:
        // as duas telas são idênticas e só uma delas é problema do usuário.
        formulario_->detalheCartaoProxima->setText(
            r.total > 0
                ? QStringLiteral("as %1 conhecidas já passaram").arg(r.total)
                : QStringLiteral("nenhuma prova coletada — use Atualizar tudo"));
        pintar(formulario_->valorCartaoProxima, palette().color(QPalette::PlaceholderText));
    }

    formulario_->valorCartaoTrinta->setText(QString::number(r.proximos30));
    formulario_->detalheCartaoTrinta->setText(
        r.proximos30 == 1 ? QStringLiteral("prova marcada")
                          : QStringLiteral("provas marcadas"));
    pintar(formulario_->valorCartaoTrinta, palette().color(QPalette::WindowText));

    formulario_->valorCartaoConfirmar->setText(QString::number(r.inferidas));
    formulario_->detalheCartaoConfirmar->setText(
        r.inferidas == 0
            ? QStringLiteral("todas vieram do painel do professor")
            : QStringLiteral("deduzidas de tópico de aula — confirme a data"));
    pintar(formulario_->valorCartaoConfirmar,
           r.inferidas > 0 ? cor::inferido() : palette().color(QPalette::PlaceholderText));
}

void JanelaPrincipal::filtrarProvasPorDia(QDate dia) {
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(formulario_->tvProvas->model());
    if (!proxy) return;

    // Dia vazio não vira lista vazia. Filtrar para zero linhas deixaria o
    // usuário olhando uma tabela em branco sem entender que foi ele que pediu;
    // melhor recusar o filtro e dizer por quê.
    if (dia.isValid() && formulario_->calProvas->provasEm(dia) == 0) {
        filtrarProvasPorDia(QDate());
        formulario_->rotuloFiltro->setText(
            QStringLiteral("%1 não tem prova — mostrando todas")
                .arg(QLocale().toString(dia, QStringLiteral("d 'de' MMMM"))));
        return;
    }

    diaFiltrado_ = dia;
    // Prefixo ISO: a chave da coluna Data é "2026-09-04" ou "2026-09-04T15:45",
    // e o "contém" do proxy pega os dois sem precisar de regex.
    proxy->setFilterFixedString(dia.isValid() ? dia.toString(Qt::ISODate) : QString());
    formulario_->botaoTodasProvas->setEnabled(dia.isValid());
    for (int c = 0; c < proxy->columnCount(); ++c) {
        formulario_->tvProvas->resizeColumnToContents(c);
    }
    // Expandir SÓ quando há filtro por dia. Sem filtro, `expandAll` desfazia o
    // recolhimento que `trocarModelo` aplica ao histórico — "Provas antigas"
    // nascia aberta, que é o oposto do ponto de existir uma seção para ela.
    if (dia.isValid()) formulario_->tvProvas->expandAll();

    // Conta as FOLHAS, não as linhas do topo: desde que as provas ganharam
    // seções ("Próximas", "Antigas"), `rowCount()` no topo devolve o número de
    // seções — e o rótulo diria "2 prova(s)" para um dia com dez.
    int n = 0;
    for (int g = 0; g < proxy->rowCount(); ++g) {
        n += proxy->rowCount(proxy->index(g, 0));
    }
    formulario_->rotuloFiltro->setText(
        dia.isValid()
            ? QStringLiteral("%1 prova(s) em %2")
                  .arg(n)
                  .arg(QLocale().toString(dia, QStringLiteral("d 'de' MMMM")))
            : QStringLiteral("%1 prova(s) no semestre").arg(n));
}

void JanelaPrincipal::montarTurmas() {
    escalarFonte(formulario_->rotuloTurmas, 0.9);
    esmaecer(formulario_->rotuloTurmas);

    connect(formulario_->botaoEntrarTurma, &QPushButton::clicked, this,
            &JanelaPrincipal::abrirTurma);
    connect(formulario_->tvTurmas, &QTableView::doubleClicked, this,
            [this] { abrirTurma(); });
    connect(formulario_->arvoreHoje, &QTreeView::doubleClicked, this,
            [this] { abrirTurmaDaAgenda(); });

    // A aula é o que a pessoa veio ver; o prazo é referência. Sem os fatores,
    // o QSplitter dividiria meio a meio e a lista de prazos empurraria as
    // aulas para uma faixa de três linhas.
    formulario_->divisorHoje->setStretchFactor(0, 3);
    formulario_->divisorHoje->setStretchFactor(1, 2);
    formulario_->divisorHoje->setSizes({420, 260});

    // O botão só liga quando há linha selecionada: um "Entrar na turma" sempre
    // clicável que responde com um aviso é pior do que um botão desligado, que
    // já diz o que falta.
    connect(formulario_->tvTurmas, &QTableView::clicked, this, [this] {
        formulario_->botaoEntrarTurma->setEnabled(true);
    });
}

void JanelaPrincipal::abrirTurma() {
    const auto sel = formulario_->tvTurmas->selectionModel();
    if (!sel || !sel->hasSelection()) {
        status(QStringLiteral("Selecione uma turma na lista."));
        return;
    }
    const int linha = sel->selectedRows(0).first().row();
    // A ordem da tabela é a do proxy (o usuário pode ter reordenado); o nome é
    // a ponte de volta para o Snapshot.
    const QString nome = sel->selectedRows(0).first().data().toString();

    const Turma* alvo = nullptr;
    for (const auto& t : snapshot_.turmas) {
        if (QString::fromStdString(t.nome).simplified() == nome) alvo = &t;
    }
    if (!alvo) {
        status(QStringLiteral("Não encontrei a turma da linha %1.").arg(linha + 1));
        return;
    }
    abrirJanelaDaTurma(*alvo);
}

void JanelaPrincipal::abrirTurmaDaAgenda() {
    const auto sel = formulario_->arvoreHoje->selectionModel();
    if (!sel || !sel->hasSelection()) return;

    // Linha de grupo ("Hoje — quarta…") e a linha "Nenhuma aula registrada"
    // não têm turma. Silêncio é a resposta certa: a pessoa clicou duas vezes
    // para expandir, não para abrir nada.
    const QString id = sel->selectedRows(0).first().data(PapelIdTurma).toString();
    if (id.isEmpty()) return;

    const Turma* alvo = nullptr;
    for (const auto& t : snapshot_.turmas) {
        if (QString::fromStdString(t.idTurma) == id) alvo = &t;
    }
    if (!alvo) {
        status(QStringLiteral("Esta aula é de uma turma que não está mais na "
                              "sua lista."));
        return;
    }
    abrirJanelaDaTurma(*alvo);
}

void JanelaPrincipal::abrirJanelaDaTurma(const Turma& turma) {
    const Turma* alvo = &turma;
    if (alvo->frontEndId.empty()) {
        QMessageBox::information(
            this, QStringLiteral("Turma sem endereço"),
            QStringLiteral(
                "Esta coleta não trouxe o identificador da Turma Virtual desta "
                "turma, então não dá para entrar nela.\n\nUse \"Atualizar tudo\" "
                "e tente de novo."));
        return;
    }

    // A turma sai do snapshot que já está na memória: é o mesmo que o banco
    // guardou no último ciclo com turmas, e é o que faz a janela abrir pintada
    // em vez de com um "Entrando na turma…".
    std::vector<TopicoAula> topicos;
    for (const auto& t : snapshot_.topicos) {
        if (t.idTurma == alvo->idTurma) topicos.push_back(t);
    }
    std::vector<ArquivoTurma> arquivos;
    for (const auto& a : snapshot_.arquivos) {
        if (a.idTurma == alvo->idTurma) arquivos.push_back(a);
    }
    std::vector<Participante> participantes;
    for (const auto& p : snapshot_.participantes) {
        if (p.idTurma == alvo->idTurma) participantes.push_back(p);
    }

    // As credenciais vão junto mesmo sem uso imediato: a janela só fala com o
    // SIGAA se a pessoa pedir um arquivo que não está no disco, e pedi-las
    // naquele momento interromperia o clique com um diálogo de login.
    std::string login, senha;
    if (!obterCredenciais(login, senha)) return;

    // Modal: a janela da turma pode abrir sessão própria com o SIGAA, e deixar
    // o usuário disparar um "Atualizar" por trás dela criaria duas navegações
    // concorrentes na mesma conta — que é justamente o que o SIGAA pune
    // invalidando a view (RECON §2.2).
    JanelaTurma d(*alvo, std::move(topicos), std::move(arquivos),
                  std::move(participantes), std::move(login), std::move(senha), this);
    d.exec();

    // A janela pode ter baixado material; o ✓ da aba Agenda vem do disco.
    montarAgenda();
}

void JanelaPrincipal::abrirDiagnostico() {
    // NÃO modal, e uma só: a janela serve justamente enquanto a coleta roda, e
    // duas cópias registrariam dois observadores no mesmo tráfego.
    if (!diagnostico_) {
        diagnostico_ = new JanelaDiagnostico(this);
        diagnostico_->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    diagnostico_->show();
    diagnostico_->raise();
    diagnostico_->activateWindow();
}

void JanelaPrincipal::montarBandeja() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    // Colorido, não tingido: na bandeja o ícone concorre com dezenas de outros
    // e é a identidade do app que precisa ser reconhecível, não o tema.
    bandeja_ = new QSystemTrayIcon(iconeApp(), this);
    bandeja_->setToolTip(QStringLiteral("SIGAA Viewer"));

    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("Abrir"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(formulario_->acAtualizar);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Sair"), qApp, &QApplication::quit);
    bandeja_->setContextMenu(menu);

    connect(bandeja_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r != QSystemTrayIcon::Trigger) return;
                showNormal();
                raise();
                activateWindow();
            });
    bandeja_->show();
}

bool JanelaPrincipal::recarregarDoBanco() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) return false;
    snapshot_ = db.carregarUltimo();
    // As correções vêm do banco junto com o snapshot, e ANTES de `mostrar`:
    // exibir a lista de provas sem elas mostraria por um instante a data que o
    // aluno já corrigiu — e essa é a data errada.
    ajustes_ = db.carregarAjustes();
    mostrar(snapshot_);
    return true;
}

void JanelaPrincipal::mostrar(const Snapshot& s) {
    auto* abas = formulario_->abas;
    // A lista resolvida é recalculada aqui, um lugar só, antes de qualquer
    // tela tocá-la. Espalhar `efetivas()` pelas quatro telas que precisam dela
    // é como a tabela e o calendário passariam a discordar sobre uma data.
    recalcularProvas();
    // A coluna elástica é sempre a que carrega o texto livre — o título da
    // atividade, a descrição da prova, o nome da turma. É o que o aluno lê; as
    // outras têm largura previsível e não ganham nada com espaço extra.
    trocarModelo(formulario_->tvPrazos, modeloPrazos(s, formulario_->tvPrazos), 0, 3);
    trocarModelo(formulario_->tvProvas, modeloProvas(provas_, formulario_->tvProvas), 0, 2);

    // "SIGAA diz" só existe quando o SIGAA discorda de você — o que é raro. Uma
    // coluna vazia ocupando um sexto da largura empurrava "Avaliação" para
    // "Pro…" enquanto a tela tinha espaço de sobra à direita.
    bool algumaDiscorda = false;
    for (const auto& p : provas_) {
        if (p.quandoSigaa.valid() && p.quandoSigaa.toIso() != p.av.quando.toIso()) {
            algumaDiscorda = true;
            break;
        }
    }
    formulario_->tvProvas->setColumnHidden(kColunaSigaaDiz, !algumaDiscorda);
    trocarModelo(formulario_->tvTurmas, modeloTurmas(s, formulario_->tvTurmas), 0, 0);
    trocarModelo(formulario_->tvAtualizacoes,
                 modeloAtualizacoes(s, formulario_->tvAtualizacoes), 0, 2);

    // Cartões e calendário depois de trocar o modelo: o filtro por dia é
    // reaplicado sobre o modelo novo, e precisa dele no lugar.
    atualizarResumoProvas(s);
    filtrarProvasPorDia(diaFiltrado_);
    montarAgenda();

    // A contagem na aba continua sendo a de HOJE mesmo com a agenda paginada
    // para outra semana: o número na aba é o que se lê sem entrar nela, e "3"
    // ali precisa querer dizer "três aulas hoje" sempre.
    abas->setTabText(0, QStringLiteral("Agenda (%1 hoje)")
                            .arg(resumoDia(s, QDate::currentDate()).aulasHoje));
    abas->setTabText(2, QStringLiteral("Turmas (%1)").arg(s.turmas.size()));
    abas->setTabText(3, QStringLiteral("Atualizações (%1)").arg(s.atualizacoes.size()));
    // A aba de provas usa a contagem já mesclada, não o vetor cru — senão o
    // número na aba discorda da quantidade de linhas na tabela. E vem do modelo
    // FONTE, não do proxy: com um dia filtrado o proxy contaria só aquele dia, e
    // a aba passaria a dizer "Provas (1)" com 14 provas no semestre.
    abas->setTabText(1, QStringLiteral("Provas (%1)").arg(resumoProvas(provas_).total));
}

void JanelaPrincipal::montarBarraAgenda() {
    // A faixa de datas é o título da tela agora que ela pagina: sem peso
    // tipográfico ela se perde entre dois botões e a pessoa não repara que
    // mudou de semana.
    escalarFonte(formulario_->rotuloDia, 1.1, /*negrito=*/true);
    escalarFonte(formulario_->rotuloResumoDia, 0.9);
    esmaecer(formulario_->rotuloResumoDia);

    // Alt+← / Alt+→: é o par que o sistema já reserva para "voltar/avançar", e
    // aqui a semana é exatamente isso. Vem de QKeySequence e não de teclas
    // cravadas porque no macOS o par é Cmd+[ e Cmd+].
    formulario_->botaoSemanaAnterior->setShortcut(QKeySequence::Back);
    formulario_->botaoSemanaSeguinte->setShortcut(QKeySequence::Forward);

    connect(formulario_->botaoSemanaAnterior, &QToolButton::clicked, this,
            [this] { deslocarAgenda(-1); });
    connect(formulario_->botaoSemanaSeguinte, &QToolButton::clicked, this,
            [this] { deslocarAgenda(1); });
    connect(formulario_->botaoHoje, &QToolButton::clicked, this,
            [this] { irParaSemana(QDate::currentDate()); });

    // No viewport, não na árvore: é ele que recebe a roda. Filtrar na árvore
    // pegaria o evento só quando ela tivesse foco de teclado.
    formulario_->arvoreHoje->viewport()->installEventFilter(this);
}

bool JanelaPrincipal::eventFilter(QObject* alvo, QEvent* ev) {
    if (alvo != formulario_->arvoreHoje->viewport() || ev->type() != QEvent::Wheel) {
        return QMainWindow::eventFilter(alvo, ev);
    }

    auto* roda = static_cast<QWheelEvent*>(ev);
    // Shift+roda vertical é o que um mouse comum tem: o Qt não converte
    // sozinho, quem quer rolagem horizontal a implementa. A roda vertical pura
    // continua rolando a lista, que é o que ela deve fazer.
    const int delta = roda->angleDelta().x() != 0
                          ? roda->angleDelta().x()
                          : (roda->modifiers().testFlag(Qt::ShiftModifier)
                                 ? roda->angleDelta().y()
                                 : 0);
    if (delta == 0) return QMainWindow::eventFilter(alvo, ev);

    // Zera ao inverter o sentido: senão um resto acumulado para a direita
    // atrasaria a primeira página para a esquerda.
    if ((delta > 0) != (rolagemAgenda_ > 0)) rolagemAgenda_ = 0;
    rolagemAgenda_ += delta;

    while (rolagemAgenda_ >= kPassoRolagem) {
        rolagemAgenda_ -= kPassoRolagem;
        deslocarAgenda(-1);   // roda para a direita = voltar no tempo
    }
    while (rolagemAgenda_ <= -kPassoRolagem) {
        rolagemAgenda_ += kPassoRolagem;
        deslocarAgenda(1);
    }
    return true;
}

void JanelaPrincipal::deslocarAgenda(int semanas) {
    if (!inicioAgenda_.isValid()) inicioAgenda_ = segundaDaSemana(QDate::currentDate());
    irParaSemana(inicioAgenda_.addDays(qint64(semanas) * kDiasPorPagina));
}

void JanelaPrincipal::irParaSemana(QDate dia) {
    const QDate nova = segundaDaSemana(dia.isValid() ? dia : QDate::currentDate());
    if (nova == inicioAgenda_) return;
    inicioAgenda_ = nova;
    montarAgenda();
}

void JanelaPrincipal::montarAgenda() {
    const Snapshot& s = snapshot_;
    const QDate hoje = QDate::currentDate();
    if (!inicioAgenda_.isValid()) inicioAgenda_ = segundaDaSemana(hoje);

    const QDate inicio = inicioAgenda_;
    const QDate fim = inicio.addDays(kDiasPorPagina - 1);
    auto* arvore = formulario_->arvoreHoje;

    auto* antigo = arvore->model();
    arvore->setModel(modeloAgenda(s, inicio, fim, hoje, arvore));
    delete antigo;

    // Expandir só os dias com aula: sete grupos abertos, cinco deles com uma
    // linha "Nenhuma aula registrada", empurrariam as aulas de verdade para
    // fora da tela — a rolagem viraria requisito para ler a quarta-feira.
    for (int i = 0; i < arvore->model()->rowCount(); ++i) {
        const QModelIndex idx = arvore->model()->index(i, 0);
        if (idx.data(PapelOrdenacao).toInt() > 0) arvore->expand(idx);
    }
    arvore->resizeColumnToContents(0);
    arvore->resizeColumnToContents(1);
    // "Aula" absorve a sobra: é a coluna de texto livre, e era ela que ficava
    // espremida enquanto metade da largura da janela sobrava à direita.
    tema::esticarColuna(arvore, 1);

    // Título: a faixa de datas, e "esta semana" só quando for verdade. Um
    // rótulo que diz sempre a mesma coisa não avisa que a pessoa está paginada
    // três semanas à frente — e é aí que ela lê "nenhuma aula" e se assusta.
    const qint64 delta = segundaDaSemana(hoje).daysTo(inicio) / kDiasPorPagina;
    QString titulo = faixaDaSemana(inicio, fim);
    if (delta == 0) titulo += QStringLiteral(" · esta semana");
    else if (delta == 1) titulo += QStringLiteral(" · semana que vem");
    else if (delta == -1) titulo += QStringLiteral(" · semana passada");
    formulario_->rotuloDia->setText(titulo);

    const ResumoDia r = resumoDia(s, hoje);
    const int naSemana = aulasEntre(s, inicio, fim);
    const FaixaAgenda faixa = faixaAgenda(s);

    if (r.semDados) {
        // Distinguir "não há aula" de "nunca coletei aula" é o ponto: o
        // primeiro é informação, o segundo é a tela dizendo que não sabe. Sem
        // isso, o aluno concluiria que não tem aula nenhuma.
        formulario_->rotuloResumoDia->setText(QStringLiteral(
            "Ainda não coletei as aulas. Use “Atualizar tudo” — só esse ciclo "
            "entra em cada turma e lê a linha do tempo do professor."));
    } else if (delta == 0) {
        formulario_->rotuloResumoDia->setText(
            QStringLiteral("%1 hoje · %2 amanhã · %3 nesta semana")
                .arg(r.aulasHoje == 1 ? QStringLiteral("1 aula")
                                      : QStringLiteral("%1 aulas").arg(r.aulasHoje))
                .arg(r.aulasAmanha == 1 ? QStringLiteral("1 aula")
                                        : QStringLiteral("%1 aulas").arg(r.aulasAmanha))
                .arg(naSemana));
    } else if (naSemana == 0 && faixa.valida() && (fim < faixa.primeiro || inicio > faixa.ultimo)) {
        // Fora do que a coleta cobre. Dizer "nenhuma aula" aqui seria afirmar
        // algo que não sabemos — o professor pode ter publicado a aula e nós
        // simplesmente não temos esse pedaço do semestre.
        formulario_->rotuloResumoDia->setText(
            QStringLiteral("Fora do período coletado (%1 a %2).")
                .arg(QLocale().toString(faixa.primeiro, QLocale::ShortFormat),
                     QLocale().toString(faixa.ultimo, QLocale::ShortFormat)));
    } else {
        formulario_->rotuloResumoDia->setText(
            naSemana == 0 ? QStringLiteral("Nenhuma aula registrada nesta semana.")
            : naSemana == 1 ? QStringLiteral("1 aula nesta semana.")
                            : QStringLiteral("%1 aulas nesta semana.").arg(naSemana));
    }

    // As bordas da paginação saem do que a coleta conhece, estendido até a
    // semana de hoje — quem abre o app fora do semestre ainda precisa poder
    // chegar às aulas que existem.
    QDate limiteInicio = segundaDaSemana(hoje);
    QDate limiteFim = limiteInicio;
    if (faixa.valida()) {
        limiteInicio = (std::min)(limiteInicio, segundaDaSemana(faixa.primeiro));
        limiteFim = (std::max)(limiteFim, segundaDaSemana(faixa.ultimo));
    }
    formulario_->botaoSemanaAnterior->setEnabled(inicio > limiteInicio);
    formulario_->botaoSemanaSeguinte->setEnabled(inicio < limiteFim);
    formulario_->botaoHoje->setEnabled(delta != 0);
}

void JanelaPrincipal::escolherEAtualizar() {
    if (trabalho_) return;

    // Sem turma conhecida não há o que escolher: o diálogo abriria vazio e o
    // aluno teria que adivinhar que precisa de uma coleta antes. Cai direto no
    // ciclo completo, que é o que ele faria de qualquer jeito.
    if (snapshot_.turmas.empty()) {
        sincronizar(/*comTurmas=*/true);
        return;
    }

    DialogoAtualizar dlg(snapshot_.turmas, ultimaEscolha_, this);
    if (dlg.exec() != QDialog::Accepted) return;

    ultimaEscolha_ = dlg.escolha();
    sincronizar(ultimaEscolha_);
}

void JanelaPrincipal::sincronizar(bool comTurmas) {
    // Atalho para quem não passou pelo diálogo (a bandeja, o automático, a
    // primeira coleta): tudo ligado, todas as turmas.
    DialogoAtualizar::Escolha e;
    e.entrarNasTurmas = comTurmas;
    sincronizar(e);
}

void JanelaPrincipal::sincronizar(const DialogoAtualizar::Escolha& escolha) {
    if (trabalho_) return;   // IgnoreNew: duas coletas em paralelo invalidam o
                             // ViewState do SIGAA (docs/RECON.md §2.2)

    const bool comTurmas = escolha.entrarNasTurmas;

    servico::Opcoes op;
    op.incluirTurmas = comTurmas;
    op.incluirArquivos = escolha.arquivos;
    op.incluirFrequencia = escolha.frequencia;
    op.apenasTurmas = escolha.turmas;
    // O ciclo completo já está dentro de cada turma e já sabe o que falta no
    // disco: baixar aqui é uma requisição por arquivo novo, e é o que faz a
    // janela da turma abrir sem rede depois. O ciclo só-portal não baixa —
    // ele nem sabe que existe arquivo.
    if (comTurmas && escolha.baixarMateriais) {
        op.pastaMateriais = pastaBaseMateriais().toStdString();
    }
    if (!obterCredenciais(op.login, op.senha)) return;
    // A sessão do app. Se a verificação de senha já abriu uma, o ciclo
    // continua nela em vez de pagar outro login.
    op.sessao = &sessaoViva_;

    ocupado(true);
    if (!comTurmas) {
        status(QStringLiteral("Consultando o portal…"));
    } else if (escolha.turmas.empty()) {
        status(QStringLiteral("Entrando nas turmas…"));
    } else if (escolha.turmas.size() == 1) {
        status(QStringLiteral("Entrando em 1 turma…"));
    } else {
        status(QStringLiteral("Entrando em %1 turmas…").arg(escolha.turmas.size()));
    }

    trabalho_ = new Trabalhador(std::move(op), this);
    connect(trabalho_, &Trabalhador::passo, this, &JanelaPrincipal::status);
    connect(trabalho_, &Trabalhador::aviso, this, [this](const QString& m) {
        status(QStringLiteral("aviso: %1").arg(m));
    });
    connect(trabalho_, &Trabalhador::concluido, this, &JanelaPrincipal::aoConcluir);
    trabalho_->start();
}

void JanelaPrincipal::aoConcluir() {
    // Cópia: o Trabalhador é destruído logo abaixo.
    const servico::Resultado r = trabalho_->resultado();
    trabalho_->wait();
    trabalho_->deleteLater();
    trabalho_ = nullptr;
    ocupado(false);

    if (r.falha == servico::Falha::ColetaSuspeita) {
        // O pior modo de falha do app é o usuário concluir que não há prazos
        // porque o parser quebrou. Aqui a interrupção é obrigatória: um texto
        // discreto na barra de status seria exatamente o silêncio perigoso.
        // Nada foi gravado e o snapshot antigo continua na tela, de propósito.
        status(QStringLiteral("Coleta suspeita — nada foi gravado."));
        QMessageBox::critical(
            this, QStringLiteral("Não consegui ler seus prazos"),
            QStringLiteral(
                "A coleta veio vazia. O provável é o SIGAA ter mudado o HTML e "
                "o leitor ter quebrado — não que os seus prazos tenham sumido."
                "\n\nO que está na tela é o último estado conhecido, e pode "
                "estar vencido. Confira direto no SIGAA."));
        return;
    }

    if (!r.ok()) {
        status(QStringLiteral("Falhou: %1").arg(QString::fromStdString(r.erro)));
        QMessageBox::warning(this, QStringLiteral("Não consegui sincronizar"),
                             QString::fromStdString(r.erro));
        return;
    }

    // NÃO mostrar r.snapshot: ele é só o que ESTA coleta trouxe.
    //
    // "Atualizar" (sem turmas) não visita turma nenhuma, então volta sem
    // avaliação alguma — e pintar a tela com ele apagaria as provas conhecidas,
    // dando ao aluno a impressão de que não tem prova marcada. É o mesmo modo de
    // falha que o resto do sistema já evita: o banco faz upsert e não apaga
    // provas, e o DiffEngine ignora avaliações quando a coleta veio sem turmas.
    // A tela precisa obedecer à mesma regra, e a forma de garantir isso é ler do
    // banco — que é onde as coletas parciais se acumulam numa foto completa.
    if (!r.bancoDisponivel || !recarregarDoBanco()) {
        snapshot_ = r.snapshot;
        mostrar(snapshot_);
    }

    relatorio_ = QString::fromStdString(r.relatorio);

    // O que foi para o disco entra no mesmo texto: "baixei 3 arquivos" é a
    // parte que explica por que o ciclo demorou, e é o que diz ao aluno que a
    // turma agora abre sem internet.
    QString material;
    if (r.materiaisBaixados > 0) {
        material = QStringLiteral(" %1 arquivo(s) novo(s) salvo(s) para uso offline.")
                       .arg(r.materiaisBaixados);
    } else if (r.materiaisPendentes > 0) {
        material = QStringLiteral(" Não consegui baixar %1 arquivo(s).")
                       .arg(r.materiaisPendentes);
    }

    if (r.diff.primeiraExecucao) {
        status(QStringLiteral("Primeira coleta: linha de base registrada, sem alertas.") +
               material);
    } else if (r.diff.eventos.empty()) {
        status(QStringLiteral("Nada novo desde a última verificação.") + material);
    } else {
        status(QStringLiteral("%1 novidade(s).").arg(r.diff.eventos.size()) + material);
    }

    // Um aviso só, agregado, como no CLI — a política mora em core/notify e a
    // UI não a reimplementa. Só interrompe se a janela não estiver na frente:
    // com ela visível, a tabela já mudou na cara do usuário.
    if (r.aviso && bandeja_ && !isActiveWindow()) {
        bandeja_->showMessage(QString::fromStdString(r.aviso->titulo),
                              QString::fromStdString(r.aviso->corpo),
                              r.aviso->urgente ? QSystemTrayIcon::Warning
                                               : QSystemTrayIcon::Information,
                              20000);
    }

    // POR ÚLTIMO, e modal: o ciclo acabou de descartar uma data de prova que o
    // usuário digitou à mão. É a única coisa que o sync faz com o que ele
    // escreveu, e vem depois do resto para não competir com a notificação
    // comum — quando há conflito, é ele que a pessoa precisa ler.
    avisarConflitos(r.conflitosAvaliacao);
}

bool JanelaPrincipal::obterCredenciais(std::string& login, std::string& senha) {
    auto res = plat::resolverCredenciais();
    if (res.ok()) {
        login = std::move(res.cred.login);
        senha = std::move(res.cred.senha);
        return true;
    }
    // Quem entrou sem marcar "guardar" continua funcionando nesta execução.
    if (!sessao_.login.empty() && !sessao_.senha.empty()) {
        login = sessao_.login;
        senha = sessao_.senha;
        return true;
    }

    DialogoLogin d(this);
    d.usarSessao(&sessaoViva_);
    if (d.exec() != QDialog::Accepted) return false;

    sessao_.login = d.login().toStdString();
    sessao_.senha = d.senha().toStdString();
    login = sessao_.login;
    senha = sessao_.senha;
    return true;
}

void JanelaPrincipal::procurarAtualizacao(DialogoOpcoes* dlg, bool silencioso) {
    if (!silencioso && dlg) dlg->procurandoAtualizacao();

    // Fora da thread da interface: é uma ida ao GitHub, e travar a janela numa
    // conexão ruim seria transformar "procurar atualização" em "o app pendurou".
    auto achado = std::make_shared<std::optional<atualizacao::Lancamento>>();
    auto erro = std::make_shared<std::string>();

    auto* th = QThread::create([achado, erro] {
        *achado = atualizacao::ultimoLancamento(erro.get());
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    connect(th, &QThread::finished, this, [this, dlg, silencioso, achado, erro] {
        const std::string atual = atualizacao::versaoAtual();

        if (!*achado) {
            // Silencioso é silencioso mesmo no erro: quem abriu o app não
            // pediu para saber que o GitHub estava fora do ar.
            if (!silencioso && dlg) {
                dlg->mostrarResultadoAtualizacao(
                    QStringLiteral("Não consegui verificar: %1")
                        .arg(QString::fromStdString(*erro)),
                    false);
            }
            return;
        }

        const auto& l = **achado;
        if (!atualizacao::maisNova(l.versao, atual)) {
            if (!silencioso && dlg) {
                dlg->mostrarResultadoAtualizacao(
                    QStringLiteral("Você já está na versão mais recente."), false);
            }
            return;
        }

        lancamentoNovo_ = l;
        const QString texto =
            QStringLiteral("Versão %1 disponível (você tem a %2).")
                .arg(QString::fromStdString(l.versao), QString::fromStdString(atual));

        if (dlg) {
            dlg->mostrarResultadoAtualizacao(texto, true);
        } else if (silencioso) {
            // Na abertura, sem diálogo aberto: a barra de status avisa sem
            // interromper. Um modal na inicialização por causa de atualização
            // é o tipo de coisa que ensina a fechar sem ler.
            status(texto + QStringLiteral(" Veja em Opções."));
        }
    });
    th->start();
}

void JanelaPrincipal::instalarAtualizacao(DialogoOpcoes* dlg) {
    if (!lancamentoNovo_) return;
    const auto l = *lancamentoNovo_;

    const auto modo = atualizacao::comoInstalar();

    // Instalação que não é nossa para mexer — pacote da distro, ou pasta sem
    // permissão de escrita. Passar por cima do gerenciador de pacotes é como
    // se quebra um sistema.
    if (modo == atualizacao::Instalacao::Manual) {
        QDesktopServices::openUrl(QUrl(QString::fromStdString(l.paginaUrl)));
        if (dlg) {
            // A razão MUDA com a plataforma, e dizer a errada é pior do que
            // não dizer nenhuma: no Mac não há pacote de sistema nenhum, e
            // quem lesse isso iria procurar um problema que não existe.
#ifdef Q_OS_MACOS
            const QString porque = QStringLiteral(
                "No macOS a troca é sua: abri a página da release no navegador. "
                "Baixe o .dmg e arraste o app para a pasta Aplicativos, por "
                "cima do antigo.");
#else
            const QString porque = QStringLiteral(
                "Não dá para trocar esta instalação automaticamente (ela veio de "
                "um pacote do sistema, ou a pasta não aceita escrita). Abri a "
                "página da release no navegador.");
#endif
            dlg->mostrarResultadoAtualizacao(porque, false);
        }
        return;
    }

    // No Windows a troca exige fechar o app: o sistema não deixa sobrescrever
    // DLL carregada. Confirmar ANTES de baixar 40 MB é o mínimo — quem não
    // pode fechar agora não deve gastar a banda.
    if (modo == atualizacao::Instalacao::FecharParaTrocar) {
        QMessageBox cx(this);
        cx.setIcon(QMessageBox::Question);
        cx.setWindowTitle(QStringLiteral("Atualizar para a versão %1")
                              .arg(QString::fromStdString(l.versao)));
        cx.setText(QStringLiteral("O app precisa fechar para trocar os arquivos."));
        cx.setInformativeText(
            QStringLiteral("Vou baixar o pacote, conferir a soma e só então fechar. "
                           "O app reabre sozinho quando a troca terminar. Se algo "
                           "falhar, a versão atual continua instalada."));
        auto* seguir = cx.addButton(QStringLiteral("Baixar e atualizar"),
                                    QMessageBox::AcceptRole);
        cx.addButton(QStringLiteral("Agora não"), QMessageBox::RejectRole);
        cx.exec();
        if (cx.clickedButton() != seguir) return;
    }

    if (dlg) {
        dlg->mostrarResultadoAtualizacao(QStringLiteral("Baixando %1…")
                                             .arg(QString::fromStdString(l.arquivoNome)),
                                         false);
    }

    const QString destino =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QString::fromStdString(l.arquivoNome));

    auto ok = std::make_shared<bool>(false);
    auto erro = std::make_shared<std::string>();
    auto* th = QThread::create([this, l, destino, modo, ok, erro] {
        atualizacao::Progresso p = [this](std::int64_t agora, std::int64_t total) {
            if (total <= 0) return;
            // Da thread de download para a da interface, por fila.
            QMetaObject::invokeMethod(
                this,
                [this, agora, total] {
                    status(QStringLiteral("Baixando atualização: %1%")
                               .arg(agora * 100 / total));
                },
                Qt::QueuedConnection);
        };
        *ok = atualizacao::baixarEVerificar(l, destino.toStdString(), p, erro.get());
        if (!*ok) return;

        // A soma bateu. Só agora o arquivo baixado pode virar programa.
        *ok = modo == atualizacao::Instalacao::TrocaDireta
                  ? atualizacao::instalarAppImage(destino.toStdString(), erro.get())
                  : atualizacao::agendarTrocaWindows(destino.toStdString(), erro.get());
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    connect(th, &QThread::finished, this, [this, dlg, ok, erro, l, modo] {
        if (!*ok) {
            const QString m = QStringLiteral("A atualização falhou: %1")
                                  .arg(QString::fromStdString(*erro));
            if (dlg) dlg->mostrarResultadoAtualizacao(m, true);
            status(m);
            return;
        }
        lancamentoNovo_.reset();

        if (modo == atualizacao::Instalacao::FecharParaTrocar) {
            // O auxiliar já está de pé, esperando este processo sumir para
            // trocar os arquivos e reabrir o app. Fechar aqui é o passo dele.
            status(QStringLiteral("Fechando para aplicar a versão %1…")
                       .arg(QString::fromStdString(l.versao)));
            if (dlg) dlg->accept();
            close();
            qApp->quit();
            return;
        }

        // AppImage: a troca já aconteceu com o app de pé. NÃO reiniciamos
        // sozinhos — trocar o arquivo é reversível enquanto o processo velho
        // vive; matá-lo no meio de uma coleta não é.
        const QString m =
            QStringLiteral("Versão %1 instalada. Feche e abra o app para usá-la — "
                           "a anterior ficou guardada ao lado, com final "
                           "“.anterior”.")
                .arg(QString::fromStdString(l.versao));
        if (dlg) dlg->mostrarResultadoAtualizacao(m, false);
        status(m);
    });
    th->start();
}

void JanelaPrincipal::abrirOpcoes() {
    DialogoOpcoes dlg(configAtual(), this);
    connect(&dlg, &DialogoOpcoes::pediuProcurarAtualizacao, this,
            [this, &dlg] { procurarAtualizacao(&dlg, /*silencioso=*/false); });
    connect(&dlg, &DialogoOpcoes::pediuInstalarAtualizacao, this,
            [this, &dlg] { instalarAtualizacao(&dlg); });

    // O diagnóstico e o relatório abrem SOBRE o diálogo, sem fechá-lo: quem
    // foi ali investigar um problema quase sempre volta para mexer na rotina.
    connect(&dlg, &DialogoOpcoes::pediuDiagnostico, this, [this, &dlg] {
        JanelaDiagnostico(&dlg).exec();
    });
    connect(&dlg, &DialogoOpcoes::pediuRelatorio, this, [this] {
        if (relatorio_.isEmpty()) {
            status(QStringLiteral("Nenhum relatório ainda — atualize primeiro."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(relatorio_));
    });

    if (dlg.exec() != QDialog::Accepted) return;
    aplicarConfig(dlg.config());
}

DialogoOpcoes::Config JanelaPrincipal::configAtual() const {
    QSettings cfg;
    DialogoOpcoes::Config c;
    c.automatico = cfg.value(QStringLiteral("sync/automatico"), true).toBool();
    c.minutosPortal =
        cfg.value(QStringLiteral("sync/minutosPortal"), kMinutosPortal).toInt();
    c.minutosCompleto =
        cfg.value(QStringLiteral("sync/minutosCompleto"), kMinutosTurmas).toInt();
    c.arquivos = cfg.value(QStringLiteral("sync/arquivos"), true).toBool();
    c.frequencia = cfg.value(QStringLiteral("sync/frequencia"), true).toBool();
    c.baixarMateriais = cfg.value(QStringLiteral("sync/baixar"), true).toBool();
    c.verificarAtualizacao =
        cfg.value(QStringLiteral("app/verificarAtualizacao"), true).toBool();
    return c;
}

void JanelaPrincipal::aplicarConfig(const DialogoOpcoes::Config& c) {
    QSettings cfg;
    // Os intervalos são normalizados contra a lista fechada ANTES de gravar.
    // Um valor editado à mão no arquivo de configuração não pode virar um
    // relógio de um minuto: o bloqueio cairia sobre a conta do aluno, e ele
    // não teria como ligar uma coisa à outra.
    const int portal = DialogoOpcoes::intervalosPortal().contains(c.minutosPortal)
                           ? c.minutosPortal
                           : kMinutosPortal;
    const int completo =
        DialogoOpcoes::intervalosCompleto().contains(c.minutosCompleto)
            ? c.minutosCompleto
            : kMinutosTurmas;

    cfg.setValue(QStringLiteral("sync/minutosPortal"), portal);
    cfg.setValue(QStringLiteral("sync/minutosCompleto"), completo);
    cfg.setValue(QStringLiteral("sync/arquivos"), c.arquivos);
    cfg.setValue(QStringLiteral("sync/frequencia"), c.frequencia);
    cfg.setValue(QStringLiteral("sync/baixar"), c.baixarMateriais);
    cfg.setValue(QStringLiteral("app/verificarAtualizacao"), c.verificarAtualizacao);

    // A escolha da rotina é também a escolha padrão do diálogo de Atualizar:
    // duas telas que configuram a mesma coisa e discordam seriam pior que uma.
    ultimaEscolha_.arquivos = c.arquivos;
    ultimaEscolha_.frequencia = c.frequencia;
    ultimaEscolha_.baixarMateriais = c.baixarMateriais;

    ligarAutomatico(c.automatico);
    status(c.automatico
               ? QStringLiteral("Rotina automática: portal a cada %1 min, turmas a "
                                "cada %2 h.")
                     .arg(portal)
                     .arg(completo / 60)
               : QStringLiteral("Rotina automática desligada."));
}

void JanelaPrincipal::ligarAutomatico(bool sim) {
    QSettings cfg;
    cfg.setValue(QStringLiteral("sync/automatico"), sim);

    if (!sim) {
        relogioPortal_->stop();
        relogioTurmas_->stop();
        return;
    }
    const auto c = configAtual();
    relogioPortal_->start(c.minutosPortal * 60 * 1000);
    relogioTurmas_->start(c.minutosCompleto * 60 * 1000);
}

void JanelaPrincipal::agendarProxima() {
    relogioPortal_ = new QTimer(this);
    relogioTurmas_ = new QTimer(this);

    // Um sync em cima do outro invalidaria o ViewState do SIGAA (RECON §2.2);
    // sincronizar() já ignora o pedido quando há coleta em andamento, então
    // aqui basta não insistir.
    connect(relogioPortal_, &QTimer::timeout, this, [this] { sincronizar(false); });
    connect(relogioTurmas_, &QTimer::timeout, this, [this] {
        // Com o que foi configurado em Opções, não com tudo ligado: quem
        // desmarcou "baixar material" não quer que a rotina baixe às 3h da
        // manhã justamente porque ele desmarcou.
        DialogoAtualizar::Escolha e;
        e.entrarNasTurmas = true;
        const auto c = configAtual();
        e.arquivos = c.arquivos;
        e.frequencia = c.frequencia;
        e.baixarMateriais = c.baixarMateriais;
        sincronizar(e);
    });

    QSettings cfg;
    const bool auto_ = cfg.value(QStringLiteral("sync/automatico"), true).toBool();
    ligarAutomatico(auto_);

    // Ao abrir, buscar já: é para isso que o usuário abriu a janela. Coleta
    // completa se ainda não há nada no banco (é a única forma de trazer provas);
    // senão o portal, que é uma requisição só.
    if (auto_) sincronizar(/*comTurmas=*/snapshot_.turmas.empty());
}

void JanelaPrincipal::aoAbrir() {
    // Procura silenciosa por versão nova. Uma requisição ao GitHub, em outra
    // thread, e sem dizer nada quando não há novidade — nem quando o GitHub
    // está fora do ar. Quem abriu o app queria ver os prazos.
    if (configAtual().verificarAtualizacao) {
        procurarAtualizacao(nullptr, /*silencioso=*/true);
    }

    const auto res = plat::resolverCredenciais();

    if (!res.ok()) {
        // Primeira execução: pedir a conta e já mostrar resultado. Um app que
        // abre vazio e só diz "configure em algum lugar" é abandonado ali.
        DialogoLogin d(this);
    d.usarSessao(&sessaoViva_);
        if (d.exec() != QDialog::Accepted) {
            status(QStringLiteral("Sem conta configurada — use Conta ▸ Entrar."));
            return;
        }
        sessao_.login = d.login().toStdString();
        sessao_.senha = d.senha().toStdString();
        agendarProxima();   // já dispara a primeira coleta, completa
        return;
    }

    if (res.origem == plat::Origem::DotEnv && plat::cofreDisponivel()) {
        const auto r = QMessageBox::question(
            this, QStringLiteral("Mover a senha para o cofre?"),
            QStringLiteral(
                "Sua senha está em texto puro no arquivo <b>.env</b>. Posso "
                "movê-la para o %1, onde fica cifrada com a chave do seu logon."
                "<br><br>Isso evita que ela vaze junto com o arquivo — backup, "
                "pasta sincronizada, repositório. Não protege contra um programa "
                "malicioso já rodando na sua conta.")
                .arg(QString::fromStdString(plat::backendCofre())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (r == QMessageBox::Yes) {
            std::string erro;
            if (plat::guardarNoCofre(res.cred.login, res.cred.senha, &erro)) {
                QMessageBox::information(
                    this, QStringLiteral("Movida"),
                    QStringLiteral(
                        "Guardada no cofre.\n\nApague as linhas SIGAA_LOGIN e "
                        "SIGAA_SENHA do .env — enquanto a cópia em texto puro "
                        "existir, o cofre não ajuda em nada."));
            } else {
                QMessageBox::warning(this, QStringLiteral("Não consegui guardar"),
                                     QString::fromStdString(erro));
            }
        }
    }

    agendarProxima();
}

void JanelaPrincipal::trocarConta() {
    DialogoLogin d(this);
    d.usarSessao(&sessaoViva_);
    if (d.exec() != QDialog::Accepted) return;
    sessao_.login = d.login().toStdString();
    sessao_.senha = d.senha().toStdString();
    status(QStringLiteral("Conta atualizada."));
}

void JanelaPrincipal::esquecerConta() {
    const auto r = QMessageBox::question(
        this, QStringLiteral("Esquecer credenciais?"),
        QStringLiteral(
            "Remove o login e a senha do cofre deste computador. Os prazos já "
            "coletados continuam no banco local; só a sincronização vai voltar a "
            "pedir a senha.\n\nIsto não apaga um .env, se você tiver um."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    std::string erro;
    if (!plat::apagarDoCofre(&erro)) {
        QMessageBox::warning(this, QStringLiteral("Não consegui remover"),
                             QString::fromStdString(erro));
        return;
    }
    plat::limparSegredo(sessao_.senha);
    sessao_.login.clear();
    status(QStringLiteral("Credenciais removidas do cofre."));
}

void JanelaPrincipal::ocupado(bool sim) {
    formulario_->acAtualizar->setEnabled(!sim);
    formulario_->acAtualizarTudo->setEnabled(!sim);
    barra_->setVisible(sim);
}

void JanelaPrincipal::status(const QString& msg) { rotulo_->setText(msg); }

} // namespace sigaa::ui
