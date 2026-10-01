#include "ui/JanelaPrincipal.h"

#include <QAction>
#include <QScreen>
#include <QDialogButtonBox>
#include <QDialog>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTabWidget>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTime>
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
#include "ui/Cabecalho.h"
#include "ui/CargaSemanal.h"
#include "core/sync/Baixador.h"
#include "ui/CalendarioProvas.h"
#include "ui/DialogoAtualizar.h"
#include "ui/DialogoLogin.h"
#include "core/atualizacao/Atualizador.h"
#include "core/config/Instituicao.h"
#include "ui/DialogoOpcoes.h"
#include "ui/Copiar.h"
#include "ui/DialogosAvaliacao.h"
#include "ui/EntrarNoSigaa.h"
#include "ui/Distintivos.h"
#include "ui/Icones.h"
#include "ui/JanelaDiagnostico.h"
#include "ui/JanelaTurma.h"
#include "ui/Mobile.h"
#include "ui/Modelos.h"
#include "ui/Planejamento.h"
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

// Respiro das abas, como a página do protótipo: o conteúdo não encosta na
// borda da janela. Em código, e não nos .ui, para que as quatro abas saiam da
// mesma escala (tema::esp) em vez de quatro números escolhidos um a um — foi
// exatamente assim que a aba Provas ficou com 12 px e a Agenda com 0.
void respirarAbas(QTabWidget* abas) {
    for (int i = 0; i < abas->count(); ++i) {
        if (auto* l = abas->widget(i)->layout()) {
            l->setContentsMargins(tema::esp(5), tema::esp(4), tema::esp(5), tema::esp(4));
            l->setSpacing(tema::esp(3));
        }
    }
}

// Rótulo de seção do design system ("AULAS", "PRAZOS"): 12/700 em caixa-alta,
// espaçado, no cinza de rótulo. A cor vem de estilo.qss (classe "secao").
void rotuloDeSecao(QLabel* l) {
    l->setProperty("classe", QStringLiteral("secao"));
    // A propriedade chega depois do polish do setupUi: sem repolir, o seletor
    // [classe="secao"] do QSS não casa e o rótulo fica na cor do texto comum.
    l->style()->unpolish(l);
    l->style()->polish(l);

    // A fonte DEPOIS do polish, e a ordem não é estilo: repolir remonta a
    // fonte do rótulo a partir da folha, e negrito e caixa-alta definidos
    // antes sumiam sem aviso.
    QFont f = tema::fonte(tema::Papel::Legenda);
    f.setWeight(QFont::Bold);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::PercentageSpacing, 107);
    l->setFont(f);
    l->setContentsMargins(0, tema::esp(3), 0, tema::esp(1));
}

// "qui 01/10". Sem o ponto que o locale põe em "qui." — numa coluna de datas
// ele é só ruído entre o dia e o número.
QString diaCurto(QDate d) {
    QString dia = QLocale(QLocale::Portuguese, QLocale::Brazil).toString(d, QStringLiteral("ddd"));
    dia.remove(QLatin1Char('.'));
    return dia + d.toString(QStringLiteral(" dd/MM"));
}

// Corta com reticências. Rich text não elide sozinho, e um nome de turma de
// sessenta letras quebraria a linha do cartão em três.
QString cortar(const QString& s, int max) {
    return s.size() <= max ? s : s.left(max - 1).trimmed() + QChar(0x2026);
}

// Texto de uma linha a partir do que veio do SIGAA (quebras e espaços
// repetidos viram um espaço).
QString umaLinhaQt(const std::string& s) {
    return QString::fromStdString(s).simplified();
}

} // namespace

JanelaPrincipal::JanelaPrincipal(QWidget* pai)
    : QMainWindow(pai), formulario_(std::make_unique<Ui::JanelaPrincipal>()) {
    formulario_->setupUi(this);

    montarListas();
    montarEstudo();         // antes da aba lembrada e da navegação: é uma aba
    montarMobile();         // idem, e depois da Estudo: as abas são lembradas por índice
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
    respirarAbas(formulario_->abas);
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
    // Voltou para o app: um agente de IA pode ter gravado no banco enquanto
    // isso (docs/MCP.md §9). Barato — uma leitura de `meta` — e só recarrega
    // quando a marca mudou e não há coleta em andamento, que recarrega sozinha.
    if (ev->type() == QEvent::ActivationChange && isActiveWindow() && navegacao_ &&
        !(barra_ && barra_->isVisible())) {
        store::Database db;
        if (db.aberto()) {
            const std::string marca = db.lerMeta("mcp.alteracao").value_or("");
            if (marcaMcp_.has_value() && marca != *marcaMcp_) {
                recarregarDoBanco();
                status(QStringLiteral("Atualizado com o que o agente de IA gravou."));
            }
            marcaMcp_ = marca;
        }
    }
    if (ev->type() == QEvent::PaletteChange || ev->type() == QEvent::ThemeChange) {
        aplicarIcones();
        // O título e o resumo da agenda levam as cores dentro do HTML; sem
        // refazer, a troca de tema deixaria o cinza do tema antigo neles.
        // `navegacao_` só existe depois da montagem — antes disso não há o
        // que refazer, e o setupUi também dispara PaletteChange.
        if (navegacao_) {
            montarAgenda();
            atualizarResumoProvas(snapshot_);
        }
    }
}

void JanelaPrincipal::aplicarIcones() {
    // changeEvent é virtual e pode ser chamado durante setupUi(), quando as
    // ações ainda não existem — o polish do stylesheet dispara PaletteChange.
    if (!formulario_ || !formulario_->acAtualizar) return;

    // Sobre o azul do botão primário: tinta de HighlightedText, não a do texto
    // comum, ou o desenho escuro sumiria no tema claro.
    formulario_->acAtualizar->setIcon(icone(
        QStringLiteral("atualizar"), palette().color(QPalette::HighlightedText)));
    formulario_->acAtualizarTudo->setIcon(
        icone(QStringLiteral("atualizar-tudo"), this));
    // `acAuto` saiu da barra: ligar e desligar a rotina é uma decisão que se
    // toma uma vez por semestre, e ela ocupava um lugar na barra ao lado de
    // ações que se usam todo dia. Vive em Opções, junto dos intervalos que
    // ela comanda — separar o interruptor da configuração dele era o que
    // fazia alguém ligar o automático sem nunca ver de quanto em quanto tempo
    // ele ia rodar.
    // O avatar também: as cores dele saem do tema em vigor.
    const QIcon avatar = avatarConta(loginAvatar_, devicePixelRatioF());
    formulario_->acConta->setIcon(avatar.isNull() ? icone(QStringLiteral("conta"), this)
                                                  : avatar);
    formulario_->acEntrarSigaa->setIcon(icone(QStringLiteral("abrir-sigaa"), this));
}

void JanelaPrincipal::atualizarAvatar(const QString& login) {
    loginAvatar_ = login;
    aplicarIcones();
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
    corrigir(*sel);
}

void JanelaPrincipal::corrigir(const avaliacao::Efetiva& prova) {
    const auto sel = std::optional<avaliacao::Efetiva>(prova);
    // Pai: o diálogo de confirmação, se estiver aberto — senão o editor abriria
    // atrás dele.
    QWidget* pai = dlgConfirmar_ ? static_cast<QWidget*>(dlgConfirmar_) : this;
    DialogoAvaliacao dlg(*sel, pai);
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
    confirmar(*sel);
}

void JanelaPrincipal::confirmar(const avaliacao::Efetiva& prova) {
    const auto* sel = &prova;
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

void JanelaPrincipal::descartar(const avaliacao::Efetiva& prova) {
    avaliacao::Ajuste aj;
    aj.idTurma = prova.av.idTurma;
    aj.descricao = prova.av.descricao;
    aj.turmaNome = prova.av.turmaNome;
    // A data fica guardada mesmo sem valer: é o que a lista de removidas
    // mostra, e o que o histórico registra como "de".
    aj.quando = prova.av.quando;
    aj.quandoSigaaNaEpoca = prova.quandoSigaa;
    aj.descartada = true;
    aj.editadoEm = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());

    if (gravarAjuste(aj, avaliacao::TipoMudanca::AlunoDescartou, prova.av.quando.toIso())) {
        status(QStringLiteral("“%1” saiu das provas. Dá para restaurar em Datas a confirmar.")
                   .arg(umaLinhaQt(prova.av.descricao)));
    }
}

void JanelaPrincipal::restaurar(const avaliacao::Ajuste& ajuste) {
    store::Database db;
    if (!db.aberto() || !db.migrar()) {
        status(QStringLiteral("Banco indisponível — não consegui restaurar."));
        return;
    }
    avaliacao::Mudanca m;
    m.idTurma = ajuste.idTurma;
    m.turmaNome = ajuste.turmaNome;
    m.descricao = ajuste.descricao;
    m.tipo = avaliacao::TipoMudanca::AlunoDesfez;
    m.para = ajuste.quando.toIso();
    m.quando = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());
    // Registra ANTES de apagar, como o desfazer da correção: depois do DELETE
    // não há de onde tirar o que estava valendo.
    db.registrarMudanca(m);
    db.removerAjuste(ajuste.idTurma, ajuste.descricao);

    recarregarAjustes();
    mostrar(snapshot_);
    status(QStringLiteral("“%1” voltou como data a confirmar.")
               .arg(umaLinhaQt(ajuste.descricao)));
}

void JanelaPrincipal::abrirConfirmarDatas() {
    DialogoConfirmarDatas::Acoes acoes;
    acoes.confirmar = [this](const avaliacao::Efetiva& p) { confirmar(p); };
    acoes.editar = [this](const avaliacao::Efetiva& p) { corrigir(p); };
    acoes.descartar = [this](const avaliacao::Efetiva& p) { descartar(p); };
    acoes.restaurar = [this](const avaliacao::Ajuste& a) { restaurar(a); };

    DialogoConfirmarDatas dlg(std::move(acoes), this);
    dlgConfirmar_ = &dlg;
    atualizarDialogoConfirmar();
    dlg.exec();
    dlgConfirmar_ = nullptr;
}

void JanelaPrincipal::atualizarDialogoConfirmar() {
    if (!dlgConfirmar_) return;
    std::vector<avaliacao::Efetiva> pendentes;
    for (const auto& p : provas_) {
        if (p.estado == avaliacao::Estado::Inferida) pendentes.push_back(p);
    }
    std::vector<avaliacao::Ajuste> descartadas;
    for (const auto& a : ajustes_) {
        if (a.ativo && a.descartada) descartadas.push_back(a);
    }
    std::sort(descartadas.begin(), descartadas.end(),
              [](const avaliacao::Ajuste& x, const avaliacao::Ajuste& y) {
                  return x.quando < y.quando;
              });
    dlgConfirmar_->mostrar(pendentes, descartadas);
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

    connect(formulario_->acEntrarSigaa, &QAction::triggered, this,
            &JanelaPrincipal::entrarNoSigaa);
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

    // O cabeçalho do protótipo, numa linha só: abas à esquerda e as ações
    // empurradas para a direita. As abas saem do QTabWidget e vêm para cá —
    // ver ui/Cabecalho.h para o porquê. Sem a marca do protótipo: o nome e o
    // ícone do app já estão na barra de título, e repeti-los aqui só gastaria
    // a largura de que as abas precisam.
    auto* barra = formulario_->barraAcoes;
    barra->setIconSize(QSize(16, 16));
    // Altura e respiro lateral em código: o `padding` e o `min-height` do QSS
    // pintam a barra mas não chegam ao layout dela, que posiciona os itens
    // pelas margens do widget. 56 e 20 são as medidas da referência.
    barra->setFixedHeight(56);
    barra->setContentsMargins(tema::esp(5) - tema::esp(3), 0, tema::esp(5), 0);

    formulario_->abas->tabBar()->hide();
    navegacao_ = new NavegacaoAbas(formulario_->abas, barra);

    auto* mola = new QWidget(barra);
    mola->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    for (QWidget* w : {static_cast<QWidget*>(navegacao_), mola}) {
        barra->insertWidget(formulario_->acAtualizar, w);
    }
    // O separador do .ui separava grupos que o protótipo não separa: ali o
    // peso visual (primário, secundário, discreto) já faz esse papel.
    for (QAction* a : barra->actions()) {
        if (a->isSeparator()) barra->removeAction(a);
    }

    // Bugs & sugestões: o caminho de volta do aluno até quem mantém o app.
    // Discreto, ao lado de Opções — está sempre à mão, mas não disputa atenção
    // com as ações do dia. Abre as issues do repositório no navegador; sem
    // formulário próprio, porque um relato precisa de conta e de conversa, e o
    // GitHub já dá as duas.
    auto* acReportar = new QAction(QStringLiteral("Bugs && sugestões"), this);  // "&&": um "&" sozinho vira atalho de teclado e some do rótulo
    // Em <p>: dica em rich text o Qt quebra em linhas; em texto puro ela sai
    // numa faixa única da largura da tela.
    acReportar->setToolTip(QStringLiteral(
        "<p>Encontrou um erro ou tem uma ideia? Abre a página de issues do projeto "
        "no GitHub. Ajuda muito dizer a versão (Opções) e o que você fez antes.</p>"));
    connect(acReportar, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/issues")));
    });
    barra->insertAction(formulario_->acOpcoes, acReportar);

    // Um único primário por área: "Atualizar" é o que se faz todo dia.
    // "Atualizar tudo" e "Conta" são secundários (com moldura); o resto é
    // discreto. O papel vira seletor em estilo.qss.
    auto marcar = [barra](QAction* a, const char* papel) {
        if (auto* w = barra->widgetForAction(a)) w->setProperty("papel", papel);
    };
    marcar(formulario_->acAtualizar, "primario");
    marcar(formulario_->acAtualizarTudo, "secundario");
    marcar(formulario_->acConta, "conta");
    if (auto* botao = barra->widgetForAction(formulario_->acConta)) {
        qobject_cast<QToolButton*>(botao)->setIconSize(QSize(26, 26));
    }
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
    barra_->setFixedSize(120, 6);
    barra_->setTextVisible(false);
    barra_->setVisible(false);

    // A barra vai COLADA ao texto, e não como widget permanente: permanente, o
    // QStatusBar a joga para a borda direita, longe do "Sincronizando…" que ela
    // acompanha. No Windows, além disso, ela herdava a altura da linha e
    // aparecia como um bloco solto no canto. Altura fixa, centrada na vertical.
    auto* linha = new QWidget;
    auto* h = new QHBoxLayout(linha);
    h->setContentsMargins(tema::esp(2), 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(barra_, 0, Qt::AlignVCenter);
    h->addWidget(rotulo_, 1, Qt::AlignVCenter);
    formulario_->statusbar->addWidget(linha, 1);
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

    // UM painel, duas colunas — e não três cartões iguais.
    //
    // Três caixas do mesmo tamanho diziam que as três coisas pesam o mesmo, e
    // não pesam: a próxima prova é a manchete, o que vem depois é a pauta, e
    // "quantas datas conferir" é uma nota de rodapé. Aqui as duas primeiras
    // viram metades de uma superfície só, separadas por um filete, e a
    // terceira sai do painel (ver `montarPilulaConfirmar`).
    //
    // Os QFrames do .ui são reaproveitados como colunas: perdem a moldura de
    // cartão e passam a morar dentro do painel.
    auto* painel = new QFrame(formulario_->abaProvas);
    painel->setObjectName(QStringLiteral("painelResumoProvas"));
    painel->setProperty("classe", QStringLiteral("cartao"));
    auto* colunas = new QHBoxLayout(painel);
    colunas->setContentsMargins(0, 0, 0, 0);
    colunas->setSpacing(0);
    auto* filete = new QFrame(painel);
    filete->setObjectName(QStringLiteral("fileteVertical"));
    filete->setFixedWidth(1);
    for (QFrame* c : {formulario_->cartaoProxima, formulario_->cartaoTrinta,
                      formulario_->cartaoConfirmar}) {
        formulario_->layoutCartoes->removeWidget(c);
        c->setProperty("classe", QString());
        c->setFrameShape(QFrame::NoFrame);
        c->style()->unpolish(c);
        c->style()->polish(c);
        c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        // O conteúdo encosta no topo; a sobra de altura vira espaço no pé da
        // coluna, e não vão entre título, valor e lista.
        if (auto* v = qobject_cast<QVBoxLayout*>(c->layout())) {
            v->setContentsMargins(tema::esp(5), tema::esp(4), tema::esp(5), tema::esp(4));
            v->addStretch(1);
        }
    }
    colunas->addWidget(formulario_->cartaoProxima, 1);
    colunas->addWidget(filete);
    // Coluna do meio: "Em seguida", ao lado da próxima prova e não embaixo
    // dela — as duas respondem "o que vem aí", e lado a lado se leem juntas
    // sem empurrar a matéria para baixo.
    auto* fileteSeguintes = new QFrame(painel);
    fileteSeguintes->setObjectName(QStringLiteral("fileteVertical"));
    fileteSeguintes->setFixedWidth(1);
    colunaSeguintes_ = new QWidget(painel);
    {
        // Mesma anatomia da coluna da esquerda: título, prova em destaque,
        // detalhe em rich text e o botão da turma.
        auto* v = new QVBoxLayout(colunaSeguintes_);
        v->setContentsMargins(tema::esp(5), tema::esp(4), tema::esp(5), tema::esp(4));
        v->setSpacing(formulario_->cartaoProxima->layout()->spacing());
        auto* tituloS = new QLabel(colunaSeguintes_);
        tituloS->setObjectName(QStringLiteral("tituloSeguintes"));
        tituloS->setTextFormat(Qt::RichText);
        auto* valorS = new QLabel(colunaSeguintes_);
        valorS->setObjectName(QStringLiteral("valorSeguintes"));
        valorS->setWordWrap(true);
        auto* lista = new QLabel(colunaSeguintes_);
        lista->setObjectName(QStringLiteral("listaSeguintes"));
        lista->setTextFormat(Qt::RichText);
        lista->setWordWrap(true);
        lista->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto* botaoS = new QPushButton(QStringLiteral("Abrir turma"), colunaSeguintes_);
        botaoS->setAutoDefault(false);
        botaoS->setToolTip(QStringLiteral("Abre a turma desta prova: aulas, arquivos e o Baixar tudo."));
        connect(botaoS, &QPushButton::clicked, this, [this] {
            for (const auto& t : snapshot_.turmas) {
                if (t.idTurma == idTurmaSeguinte_) {
                    abrirJanelaDaTurma(t);
                    return;
                }
            }
        });
        v->addWidget(tituloS);
        v->addWidget(valorS);
        v->addWidget(lista);
        v->addWidget(botaoS, 0, Qt::AlignLeft);
        v->addStretch(1);
    }
    colunas->insertWidget(colunas->indexOf(filete), fileteSeguintes);
    colunas->insertWidget(colunas->indexOf(filete), colunaSeguintes_, 1);
    fileteSeguintes_ = fileteSeguintes;
    colunas->addWidget(formulario_->cartaoTrinta, 1);
    formulario_->cartaoConfirmar->hide();
    formulario_->layoutCartoes->addWidget(painel);

    // A linha do painel não cresce com a janela: a altura que sobra é do
    // calendário e da tabela, que é onde há o que ler.
    if (auto* aba = formulario_->layoutProvas) {
        aba->setStretch(aba->indexOf(formulario_->layoutCartoes), 0);
        aba->setStretch(aba->indexOf(formulario_->divisorProvas), 1);
    }

    // "A confirmar" vira pílula na linha de controle da tabela: é ali, na
    // lista, que a data se confirma, e é ali que o lembrete serve.
    // Clicável: abre "Datas a confirmar", com as três respostas por prova.
    auto* pilula = new QToolButton(formulario_->abaProvas);
    pilula->setObjectName(QStringLiteral("pilulaConfirmar"));
    pilula->setProperty("classe", QStringLiteral("pilulaAviso"));
    pilula->setCursor(Qt::PointingHandCursor);
    pilula->setToolTip(QStringLiteral(
        "Datas deduzidas de tópico de aula, não cadastradas pelo professor. "
        "Clique para confirmar, corrigir ou remover cada uma."));
    connect(pilula, &QToolButton::clicked, this, &JanelaPrincipal::abrirConfirmarDatas);
    pilula->hide();
    pilula->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    formulario_->layoutFiltro->insertWidget(
        formulario_->layoutFiltro->indexOf(formulario_->rotuloFiltro) + 1, pilula, 0,
        Qt::AlignVCenter);

    // As fontes DEPOIS de montar o painel: repolir as colunas (acima)
    // remonta a fonte dos rótulos filhos a partir da folha, e o que viesse
    // antes — caixa-alta do título, o valor grande — sumia sem aviso.
    for (QLabel* l : {formulario_->tituloCartaoProxima, formulario_->tituloCartaoTrinta,
                      formulario_->tituloCartaoConfirmar,
                      colunaSeguintes_->findChild<QLabel*>(QStringLiteral("tituloSeguintes"))}) {
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

    // --- Coluna esquerda: a próxima prova, com o que estudar ---------------
    // Título ("PRÓXIMA PROVA · em 7 dias"), nome da prova no lugar do número
    // grande, e o detalhe em rich text: quando/onde, origem, matéria.
    formulario_->tituloCartaoProxima->setTextFormat(Qt::RichText);
    {
        QFont f = tema::fonte(tema::Papel::Subtitulo);
        f.setWeight(QFont::Bold);
        formulario_->valorCartaoProxima->setFont(f);
        formulario_->valorCartaoProxima->setWordWrap(true);
        // A coluna "Em seguida" tem a mesma anatomia, e as mesmas fontes.
        colunaSeguintes_->findChild<QLabel*>(QStringLiteral("valorSeguintes"))->setFont(f);
        auto* listaSeg = colunaSeguintes_->findChild<QLabel*>(QStringLiteral("listaSeguintes"));
        listaSeg->setFont(tema::fonte(tema::Papel::Corpo));
        QPalette p = listaSeg->palette();
        p.setColor(QPalette::WindowText, tema::token("text-2"));
        listaSeg->setPalette(p);
        colunaSeguintes_->findChild<QLabel*>(QStringLiteral("tituloSeguintes"))
            ->setTextFormat(Qt::RichText);
    }
    auto* detalhe = formulario_->detalheCartaoProxima;
    detalhe->setTextFormat(Qt::RichText);
    detalhe->setWordWrap(true);
    detalhe->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    detalhe->setFont(tema::fonte(tema::Papel::Corpo));
    {
        QPalette p = detalhe->palette();
        p.setColor(QPalette::WindowText, tema::token("text-2"));
        detalhe->setPalette(p);
    }
    // O material fica na janela da turma, onde "Baixar tudo" já existe — o
    // botão leva até lá em vez de duplicar o download aqui.
    botaoTurmaProxima_ = new QPushButton(QStringLiteral("Abrir turma"), formulario_->cartaoProxima);
    botaoTurmaProxima_->setAutoDefault(false);
    botaoTurmaProxima_->setToolTip(QStringLiteral(
        "Abre a turma desta prova: aulas, arquivos e o Baixar tudo."));
    botaoTurmaProxima_->hide();
    if (auto* v = qobject_cast<QVBoxLayout*>(formulario_->cartaoProxima->layout())) {
        v->insertWidget(v->count() - 1, botaoTurmaProxima_, 0, Qt::AlignLeft);
    }
    connect(botaoTurmaProxima_, &QPushButton::clicked, this, [this] {
        for (const auto& t : snapshot_.turmas) {
            if (t.idTurma == idTurmaProxima_) {
                abrirJanelaDaTurma(t);
                return;
            }
        }
    });

    // --- Coluna direita: carga por semana --------------------------------
    formulario_->tituloCartaoTrinta->setTextFormat(Qt::RichText);
    formulario_->valorCartaoTrinta->hide();
    formulario_->detalheCartaoTrinta->hide();
    carga_ = new CargaSemanal(formulario_->cartaoTrinta);
    auto* legenda = new QLabel(formulario_->cartaoTrinta);
    legenda->setTextFormat(Qt::RichText);
    legenda->setFont(tema::fonte(tema::Papel::Legenda));
    legenda->setObjectName(QStringLiteral("legendaCarga"));
    if (auto* v = qobject_cast<QVBoxLayout*>(formulario_->cartaoTrinta->layout())) {
        // O gráfico ocupa a altura que a coluna tiver — as outras duas colunas
        // ditam a altura do painel, e um gráfico de altura fixa deixava meio
        // painel vazio embaixo. A mola do fim sai; quem estica é o gráfico.
        if (auto* item = v->itemAt(v->count() - 1); item && item->spacerItem()) {
            delete v->takeAt(v->count() - 1);
        }
        v->addWidget(carga_, 1);
        v->addWidget(legenda);
    }
    // Título, setas e "Período inteiro" na mesma linha. As setas andam de
    // janela em janela (6 semanas) — de uma em uma, chegar em dezembro seriam
    // dez cliques. O botão abre o período letivo inteiro num diálogo, para
    // quem quer se organizar olhando o semestre de uma vez.
    if (auto* v = qobject_cast<QVBoxLayout*>(formulario_->cartaoTrinta->layout())) {
        auto* linha = new QHBoxLayout;
        linha->setSpacing(tema::esp(1));
        const int i = v->indexOf(formulario_->tituloCartaoTrinta);
        v->removeWidget(formulario_->tituloCartaoTrinta);
        linha->addWidget(formulario_->tituloCartaoTrinta, 1);
        cargaAnterior_ = new QToolButton(formulario_->cartaoTrinta);
        cargaSeguinte_ = new QToolButton(formulario_->cartaoTrinta);
        cargaAnterior_->setText(QStringLiteral("‹"));
        cargaSeguinte_->setText(QStringLiteral("›"));
        cargaAnterior_->setToolTip(QStringLiteral("6 semanas antes"));
        cargaSeguinte_->setToolTip(QStringLiteral("6 semanas depois"));
        auto* completa = new QToolButton(formulario_->cartaoTrinta);
        completa->setText(QStringLiteral("Período inteiro"));
        completa->setToolTip(QStringLiteral("Todas as semanas do período, com provas e entregas."));
        for (QToolButton* b : {cargaAnterior_, cargaSeguinte_}) {
            b->setObjectName(QStringLiteral("setaCarga"));
            b->setFixedSize(26, 26);
            b->setCursor(Qt::PointingHandCursor);
        }
        completa->setObjectName(QStringLiteral("botaoCargaCompleta"));
        completa->setCursor(Qt::PointingHandCursor);
        linha->addWidget(cargaAnterior_);
        linha->addWidget(cargaSeguinte_);
        linha->addSpacing(tema::esp(1));
        linha->addWidget(completa);
        v->insertLayout(i, linha);

        constexpr int kPasso = 6;
        connect(cargaAnterior_, &QToolButton::clicked, this, [this] {
            deslocCarga_ -= kPasso;
            atualizarCarga();
        });
        connect(cargaSeguinte_, &QToolButton::clicked, this, [this] {
            deslocCarga_ += kPasso;
            atualizarCarga();
        });
        connect(completa, &QToolButton::clicked, this, &JanelaPrincipal::abrirCargaCompleta);
    }

    connect(carga_, &CargaSemanal::semanaClicada, this, [this](QDate inicio) {
        // Clicar de novo na mesma semana desfaz — o mesmo gesto do calendário.
        filtrarProvasPorSemana(inicio == semanaFiltrada_ ? QDate() : inicio);
    });

    // O calendário tem largura natural; a lista é quem deve engolir a sobra ao
    // maximizar a janela.
    formulario_->divisorProvas->setStretchFactor(0, 0);
    formulario_->divisorProvas->setStretchFactor(1, 1);
    formulario_->divisorProvas->setSizes({340, 660});

    // --- O mês seguinte, embaixo do calendário -----------------------------
    //
    // A coluna do calendário é alta e o mês cabe em ~330 px: esticar o mês
    // para ocupar tudo deixava cada dia do tamanho de um botão e a grade
    // ilegível. O espaço vai para o MÊS SEGUINTE — é o que se consulta na
    // virada do mês, quando a próxima prova está a dias mas já no outro mês.
    // Acompanha a navegação do primeiro; a própria barra de mês fica escondida
    // (um título simples basta, e duas setas duplicadas confundiriam).
    {
        auto* coluna = formulario_->layoutPainelCalendario;
        tituloCalSeguinte_ = new QLabel(formulario_->painelCalendario);
        tituloCalSeguinte_->setAlignment(Qt::AlignHCenter);
        QFont f = tema::fonte(tema::Papel::Corpo);
        f.setWeight(QFont::Bold);
        tituloCalSeguinte_->setFont(f);
        tituloCalSeguinte_->setContentsMargins(0, tema::esp(3), 0, tema::esp(1));
        calSeguinte_ = new CalendarioProvas(formulario_->painelCalendario);
        calSeguinte_->setNavigationBarVisible(false);
        calSeguinte_->naoPosicionarSozinho();
        const int depoisDoPrincipal = coluna->indexOf(formulario_->calProvas) + 1;
        coluna->insertWidget(depoisDoPrincipal, tituloCalSeguinte_);
        coluna->insertWidget(depoisDoPrincipal + 1, calSeguinte_);
        // Os dois meses crescem juntos até um teto: abaixo dele a grade fica
        // espremida (linhas de 20 px), acima vira o calendário esticado. O
        // que passar do teto vai para a mola do fim, no pé da coluna.
        constexpr int kAlturaMaxMes = 300;
        for (QCalendarWidget* c : {static_cast<QCalendarWidget*>(formulario_->calProvas),
                                   static_cast<QCalendarWidget*>(calSeguinte_)}) {
            c->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            c->setMaximumHeight(kAlturaMaxMes);
            coluna->setStretchFactor(c, 1);
        }
        coluna->addStretch(0);
        // O Qt sempre desenha a data selecionada — e o 30/09 do mês principal
        // aparecia destacado na grade de outubro, entre os dias "de fora". No
        // segundo mês o destaque só vale quando o dia filtrado é dele (ver
        // filtrarProvasPorDia).
        calSeguinte_->mostrarSelecao(false);

        auto sincronizar = [this](int ano, int mes) {
            const QDate seguinte = QDate(ano, mes, 1).addMonths(1);
            calSeguinte_->setCurrentPage(seguinte.year(), seguinte.month());
            tituloCalSeguinte_->setText(
                QLocale(QLocale::Portuguese, QLocale::Brazil)
                    .toString(seguinte, QStringLiteral("MMMM 'de' yyyy")));
        };
        connect(formulario_->calProvas, &QCalendarWidget::currentPageChanged, this, sincronizar);
        sincronizar(formulario_->calProvas->yearShown(), formulario_->calProvas->monthShown());
        connect(calSeguinte_, &QCalendarWidget::clicked, this, [this](QDate d) {
            filtrarProvasPorDia(d == diaFiltrado_ ? QDate() : d);
        });
        // Altura: os dois meses só quando cabem. Medido no painel, e não na
        // janela, porque é o divisor quem decide a altura dele.
        formulario_->painelCalendario->installEventFilter(this);
    }

    connect(formulario_->calProvas, &QCalendarWidget::clicked, this, [this](QDate d) {
        // Clicar de novo no mesmo dia desfaz o filtro. É o gesto que as pessoas
        // tentam antes de procurar um botão, e negá-lo faz o botão parecer a
        // única saída de um beco.
        filtrarProvasPorDia(d == diaFiltrado_ ? QDate() : d);
    });
    connect(formulario_->botaoTodasProvas, &QToolButton::clicked, this,
            [this] { filtrarProvasPorDia(QDate()); });
}

void JanelaPrincipal::atualizarCarga() {
    constexpr int kSemanas = 6;
    const QDateTime agora = QDateTime::currentDateTime();
    const QDate hoje = segundaDe(agora.date());

    // As setas param nas bordas do período: antes da primeira e depois da
    // última prova/entrega só haveria semanas "livres", que não informam nada.
    const auto [ini, fim] = periodoDaCarga(provas_, snapshot_.atividades);
    if (ini.isValid()) {
        const int minimo = static_cast<int>(hoje.daysTo(ini) / 7);
        const int maximo = std::max(0, static_cast<int>(hoje.daysTo(fim) / 7) - kSemanas + 1);
        deslocCarga_ = std::clamp(deslocCarga_, std::min(0, minimo), std::max(0, maximo));
        cargaAnterior_->setEnabled(deslocCarga_ > std::min(0, minimo));
        cargaSeguinte_->setEnabled(deslocCarga_ < std::max(0, maximo));
    } else {
        deslocCarga_ = 0;
        cargaAnterior_->setEnabled(false);
        cargaSeguinte_->setEnabled(false);
    }

    const QDate primeira = hoje.addDays(7 * deslocCarga_);
    const QDate ultima = primeira.addDays(7 * (kSemanas - 1));
    // Fora da janela de hoje, o título diz ONDE se está — sem isso, "5 out"
    // na primeira coluna parece a semana atual.
    const QString onde = deslocCarga_ == 0
        ? QStringLiteral("provas + entregas")
        : QStringLiteral("%1 – %2").arg(primeira.toString(QStringLiteral("dd/MM")),
                                        ultima.addDays(6).toString(QStringLiteral("dd/MM")));
    formulario_->tituloCartaoTrinta->setText(
        QStringLiteral("CARGA POR SEMANA&nbsp;&nbsp;<span style=\"font-weight:400\">%1</span>")
            .arg(onde));
    carga_->definir(cargaPorSemana(provas_, snapshot_.atividades, agora, primeira, kSemanas));
    carga_->destacar(semanaFiltrada_);
}

void JanelaPrincipal::abrirCargaCompleta() {
    // Largura de cada semana: cabe "29 set – 5 out" e a fileira dos 7 dias.
    static constexpr int kColunaPeriodo = 112;
    const auto [ini, fim] = periodoDaCarga(provas_, snapshot_.atividades);
    if (!ini.isValid()) {
        status(QStringLiteral("Nenhuma prova ou entrega coletada ainda."));
        return;
    }
    const int semanas = static_cast<int>(ini.daysTo(fim) / 7) + 1;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Carga do período"));
    auto* raiz = new QVBoxLayout(&dlg);
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));
    raiz->setSpacing(tema::esp(3));

    auto* titulo = new QLabel(QStringLiteral("Carga do período"), &dlg);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    auto* explica = new QLabel(
        QStringLiteral("%1 semanas, de %2 a %3. Cada coluna é uma semana, de segunda a "
                       "domingo, com as provas e entregas dela; os quadradinhos embaixo são "
                       "os dias. Passe o mouse num dia para ver o que cai nele, e clique "
                       "numa semana para ver as provas dela na lista.")
            .arg(semanas)
            .arg(ini.toString(QStringLiteral("dd/MM")), fim.addDays(6).toString(QStringLiteral("dd/MM"))),
        &dlg);
    explica->setWordWrap(true);
    explica->setProperty("classe", QStringLiteral("nota"));
    raiz->addWidget(explica);

    // Largura fixa por semana e rolagem horizontal: um semestre tem ~20
    // semanas, e espremê-las na largura da tela faria colunas de 30 px.
    auto* grafico = new CargaSemanal(&dlg);
    grafico->definir(cargaPorSemana(provas_, snapshot_.atividades, QDateTime::currentDateTime(),
                                    ini, semanas));
    grafico->destacar(semanaFiltrada_);
    grafico->marcarSemanaAtual(true);
    grafico->mostrarDias(true);
    grafico->setMinimumWidth(semanas * kColunaPeriodo);
    grafico->setMinimumHeight(300);
    auto* rolagem = new QScrollArea(&dlg);
    rolagem->setWidget(grafico);
    rolagem->setWidgetResizable(true);
    rolagem->setFrameShape(QFrame::NoFrame);
    rolagem->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    raiz->addWidget(rolagem, 1);

    if (auto* legendaPainel = findChild<QLabel*>(QStringLiteral("legendaCarga"))) {
        auto* legenda = new QLabel(legendaPainel->text(), &dlg);
        legenda->setTextFormat(Qt::RichText);
        legenda->setFont(legendaPainel->font());
        raiz->addWidget(legenda);
    }
    {
        // A legenda dos quadradinhos, que só existem aqui.
        const QString laranja = tema::cor::urgente().name();
        const QString contorno = tema::token("line-strong").name();
        const QString accent = tema::token("accent").name();
        auto* dias = new QLabel(
            QStringLiteral("Dias (S T Q Q S S D): "
                           "<span style='color:%1'>■</span> prova · "
                           "<span style='color:%1'>■</span> com ponto = 2 ou mais provas no dia · "
                           "<span style='color:%2'>□</span> livre · "
                           "<span style='color:%3'>○</span> hoje")
                .arg(laranja, contorno, accent),
            &dlg);
        dias->setTextFormat(Qt::RichText);
        if (auto* l = findChild<QLabel*>(QStringLiteral("legendaCarga"))) dias->setFont(l->font());
        raiz->addWidget(dias);
    }

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    botoes->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fechar"));
    botoes->button(QDialogButtonBox::Close)->setAutoDefault(false);
    botoes->button(QDialogButtonBox::Close)->setIcon(QIcon());
    connect(botoes, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // O passo seguinte a "onde aperta": como distribuir o estudo até lá.
    auto* planejar = botoes->addButton(QStringLiteral("Ver planejamento"),
                                       QDialogButtonBox::ActionRole);
    planejar->setProperty("papel", QStringLiteral("primario"));
    planejar->setAutoDefault(false);
    planejar->setToolTip(QStringLiteral("Distribui suas horas livres de estudo entre as "
                                        "provas que vêm, e mostra onde o semestre aperta."));
    connect(planejar, &QPushButton::clicked, &dlg, [this, &dlg] {
        dlg.accept();
        irParaPlanejamento();
    });
    raiz->addWidget(botoes);

    // Clicar numa semana filtra a lista e fecha: o diálogo é para achar a
    // semana, e a lista é onde ela se lê.
    connect(grafico, &CargaSemanal::semanaClicada, &dlg, [this, &dlg](QDate inicio) {
        filtrarProvasPorSemana(inicio);
        dlg.accept();
    });

    const QRect tela = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    dlg.resize(std::min(semanas * kColunaPeriodo + tema::esp(10), tela.width() - 80), 540);
    // Abre com a semana de hoje à vista, não no começo do semestre.
    const QDate hoje = segundaDe(QDate::currentDate());
    const int colunaHoje = static_cast<int>(ini.daysTo(hoje) / 7);
    QTimer::singleShot(0, &dlg, [rolagem, colunaHoje] {
        rolagem->horizontalScrollBar()->setValue(std::max(0, colunaHoje - 2) * kColunaPeriodo);
    });
    dlg.exec();
}

EntradasEstudo JanelaPrincipal::entradasDoPlanejamento() const {
    EntradasEstudo e;
    const QDateTime agora = QDateTime::currentDateTime();
    for (const auto& p : provas_) {
        if (jaPassou(p.av.quando, agora)) continue;
        // Substitutiva e reposição só faz quem precisa. Planejar para elas
        // de saída tiraria horas das provas que todos fazem.
        const QString desc = QString::fromStdString(p.av.descricao).toLower();
        if (desc.contains(QStringLiteral("substitutiv")) ||
            desc.contains(QStringLiteral("reposi"))) {
            continue;
        }
        planejamento::ProvaAlvo a;
        a.idTurma = p.av.idTurma;
        a.turmaNome = p.av.turmaNome;
        a.descricao = p.av.descricao;
        a.data = p.av.quando;
        a.inferida = p.estado == avaliacao::Estado::Inferida;
        // O tamanho da matéria: os tópicos entre esta prova e a anterior, os
        // mesmos que a "Próxima prova" lista.
        const MateriaDaProva m = materiaDaProva(snapshot_, p, provas_);
        a.topicos = m.coletada ? static_cast<int>(m.topicos.size()) : -1;
        e.provas.push_back(std::move(a));
    }
    for (const auto& at : snapshot_.atividades) {
        if (at.status == StatusAtividade::Concluida || jaPassou(at.prazo, agora)) continue;
        e.entregas.push_back({at.idTurma, at.turmaNome, at.titulo, at.prazo});
    }
    for (const auto& t : snapshot_.turmas) e.turmas.emplace_back(t.idTurma, t.nome);
    // As aulas da grade saem do tempo de estudo de cada dia da semana.
    e.aulas = planejamento::minutosDeAulaPorDia(snapshot_.turmas);
    return e;
}

void JanelaPrincipal::montarEstudo() {
    painelEstudo_ = new PainelEstudo(QDir::current().absoluteFilePath(QStringLiteral("sigaa-viewer.db")),
                                     pastaBaseMateriais(), formulario_->abas);
    // No fim: as outras abas têm índice fixo (setTabText por número) e a aba
    // lembrada é guardada por índice.
    abaEstudo_ = formulario_->abas->addTab(painelEstudo_, QStringLiteral("Estudo"));
    // O painel replanejou ou recebeu um check: a Agenda lê o banco de novo.
    // Sem replanejar aqui — o painel acabou de fazer isso.
    painelEstudo_->aoMudarPlano = [this] {
        store::Database db;
        if (db.aberto() && db.migrar()) estudo_ = db.carregarSessoesEstudo();
        montarAgenda();
        atualizarTituloEstudo();
    };
}

void JanelaPrincipal::montarMobile() {
    painelMobile_ = new PainelMobile(QDir::current().absoluteFilePath(QStringLiteral("sigaa-viewer.db")),
                                     pastaBaseMateriais(), formulario_->abas);
    abaMobile_ = formulario_->abas->addTab(painelMobile_, QStringLiteral("Acesso mobile"));
    // O ponto no nome é o aviso de que há uma porta aberta, visível de
    // qualquer aba — quem esqueceu o servidor ligado precisa poder notar.
    painelMobile_->aoMudar = [this](bool noAr) {
        formulario_->abas->setTabText(abaMobile_, noAr ? QStringLiteral("Acesso mobile ●")
                                                       : QStringLiteral("Acesso mobile"));
    };
    // Depois do ciclo de eventos: a janela aparece primeiro, e uma VPN que
    // demora a responder não atrasa a abertura.
    QTimer::singleShot(0, painelMobile_, [p = painelMobile_] { p->ligarSeAutomatico(); });
}

void JanelaPrincipal::irParaPlanejamento() {
    painelEstudo_->mostrarPlanejamento();
    formulario_->abas->setCurrentIndex(abaEstudo_);
}

void JanelaPrincipal::atualizarTituloEstudo() {
    const QDate hoje = QDate::currentDate();
    int minutos = 0;
    for (const auto& se : estudo_) {
        if (!se.feita && QDate(se.dia.year, se.dia.month, se.dia.day) == hoje) minutos += se.minutos;
    }
    formulario_->abas->setTabText(
        abaEstudo_, minutos > 0 ? QStringLiteral("Estudo (%1 hoje)")
                                      .arg(QString::fromStdString(planejamento::duracao(minutos)))
                                : QStringLiteral("Estudo"));
    if (navegacao_) navegacao_->sincronizar();
}

void JanelaPrincipal::atualizarEstudo() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) return;
    const auto guardadas = db.carregarSessoesEstudo();
    if (guardadas.empty()) {
        estudo_.clear();
        atualizarTituloEstudo();
        return;
    }
    // Replanejar a cada recarga, e não só quando a janela abre: uma prova
    // confirmada ou uma entrega nova mudam o plano, e a Agenda de amanhã tem
    // de refletir isso sem o aluno lembrar de abrir o Planejamento.
    const QDate h = QDate::currentDate();
    DateTime hoje;
    hoje.year = h.year();
    hoje.month = h.month();
    hoje.day = h.day();
    const auto e = entradasDoPlanejamento();
    planejamento::DoAgente agente;
    agente.estudos = db.carregarRegistrosEstudo();
    agente.focos = db.carregarFocos({}, /*soAbertos=*/true);
    auto plano = planejamento::planejar(e.provas, e.entregas, db.carregarPreferenciasEstudo(),
                                        guardadas, hoje, e.aulas, agente);
    db.substituirSessoesEstudo(plano.sessoes);
    estudo_ = std::move(plano.sessoes);
    atualizarTituloEstudo();
}

void JanelaPrincipal::marcarEstudoDaAgenda(QStandardItem* it) {
    const QString chave = it->data(PapelChaveSessao).toString();
    if (chave.isEmpty()) return;
    const bool feita = it->checkState() == Qt::Checked;
    store::Database db;
    if (!db.aberto() ||
        !db.marcarSessaoEstudo(chave.toStdString(), feita, QDateTime::currentSecsSinceEpoch())) {
        status(QStringLiteral("Não consegui guardar a marcação do estudo."));
        return;
    }
    for (auto& se : estudo_) {
        if (QString::fromStdString(se.chave()) == chave) se.feita = feita;
    }
    // A cor muda junto com o check: feito fica apagado, como aula passada.
    // Sem `blockSignals`, mudar a cor dispararia `itemChanged` de novo.
    QSignalBlocker bloqueio(it->model());
    it->setForeground(QBrush(feita ? tema::cor::apagado() : tema::cor::acento()));
}

QString JanelaPrincipal::detalheDaProva(const avaliacao::Efetiva& prova, QString* dica) const {
    const QString corTexto = tema::token("text").name();
    const QString corApoio = tema::token("text-3").name();
    const auto& av = prova.av;

    // Linha 1: dia · hora · local. O local vem da turma — é o que o aluno
    // procura na manhã da prova, e o SIGAA só o mostra dentro da turma.
    QStringList onde{diaCurto(QDate(av.quando.year, av.quando.month, av.quando.day))};
    if (av.quando.hasTime) {
        onde << QTime(av.quando.hour, av.quando.minute).toString(QStringLiteral("HH:mm"));
    }
    for (const auto& t : snapshot_.turmas) {
        if (t.idTurma == av.idTurma && !t.local.empty()) onde << QString::fromStdString(t.local);
    }
    QString h = QStringLiteral("<div style=\"color:%1\">%2</div>")
                    .arg(corTexto, onde.join(QStringLiteral(" · ")).toHtmlEscaped());

    // Linha 2: de onde veio a data. A mesma honestidade da coluna Origem.
    QString origem;
    switch (prova.estado) {
    case avaliacao::Estado::Editada: {
        const QDate ds(prova.quandoSigaa.year, prova.quandoSigaa.month, prova.quandoSigaa.day);
        origem = QStringLiteral("<span style=\"color:%1; font-weight:600\">✓ você corrigiu</span>"
                                "<span style=\"color:%2\">&nbsp;&nbsp;SIGAA diz %3</span>")
                     .arg(tema::cor::sucesso().name(), corApoio,
                          ds.isValid() ? ds.toString(QStringLiteral("dd/MM")) : QStringLiteral("—"));
        break;
    }
    case avaliacao::Estado::Inferida:
        origem = QStringLiteral("<span style=\"color:%1; font-weight:600\">○ inferida — confirme</span>")
                     .arg(cor::inferido().name());
        break;
    case avaliacao::Estado::Confirmada:
        origem = QStringLiteral("<span style=\"color:%1\">✓ você confirmou</span>")
                     .arg(tema::cor::sucesso().name());
        break;
    case avaliacao::Estado::Criada:
        origem = QStringLiteral("<span style=\"color:%1\">✎ você cadastrou</span>")
                     .arg(tema::cor::sucesso().name());
        break;
    default:
        origem = QStringLiteral("<span style=\"color:%1\">painel do professor</span>").arg(corApoio);
    }
    h += QStringLiteral("<div style=\"margin-top:2px\">%1</div>").arg(origem);

    // Matéria: tópicos desde a prova anterior da turma.
    const MateriaDaProva m = materiaDaProva(snapshot_, prova, provas_);
    const QString desde = m.desde.isValid()
        ? QStringLiteral("Matéria desde %1 (%2)")
              .arg(m.provaAnterior.toHtmlEscaped(), m.desde.toString(QStringLiteral("dd/MM")))
        : QStringLiteral("Matéria desde o início do período");
    h += QStringLiteral("<hr style=\"border:none; height:1px; background:%1\">")
             .arg(tema::token("line").name());
    if (dica) dica->clear();
    if (!m.coletada) {
        h += QStringLiteral("<div style=\"color:%1\">Ainda não coletei as aulas desta turma — "
                            "use Atualizar tudo para ver a matéria.</div>").arg(corApoio);
    } else if (m.topicos.empty()) {
        h += QStringLiteral("<div style=\"color:%1\">%2 · nenhum tópico registrado pelo "
                            "professor</div>").arg(corApoio, desde);
    } else {
        h += QStringLiteral("<div style=\"color:%1\">%2 · %3 tópico(s)</div><ol style=\"margin:2px 0 0 0; "
                            "-qt-list-indent:1; color:%4\">")
                 .arg(corApoio, desde).arg(m.topicos.size()).arg(corTexto);
        constexpr size_t kMaxTopicos = 5;
        for (size_t i = 0; i < m.topicos.size() && i < kMaxTopicos; ++i) {
            h += QStringLiteral("<li>%1</li>").arg(cortar(m.topicos[i], 56).toHtmlEscaped());
        }
        h += QStringLiteral("</ol>");
        // A lista inteira na dica do cartão: o cartão mostra as primeiras, e
        // quem está estudando quer ver até onde vai sem abrir a turma.
        if (dica) {
            QString d = QStringLiteral("<p><b>%1 · %2 tópico(s)</b></p><ol>")
                            .arg(desde).arg(m.topicos.size());
            for (const auto& t : m.topicos) d += QStringLiteral("<li>%1</li>").arg(t.toHtmlEscaped());
            *dica = d + QStringLiteral("</ol>");
        }
        if (m.topicos.size() > kMaxTopicos) {
            h += QStringLiteral("<div style=\"color:%1\">+%2 tópicos</div>")
                     .arg(corApoio).arg(m.topicos.size() - kMaxTopicos);
        }
    }
    // Arquivos: conferidos no cache local, o mesmo que a janela da turma usa
    // para o selo "✓ offline".
    if (!m.idsArquivos.empty()) {
        int salvos = 0;
        for (const auto& t : snapshot_.turmas) {
            if (t.idTurma != av.idTurma) continue;
            const sync::CacheLocal cache(
                sync::pastaDaTurma(pastaBaseMateriais().toStdString(), t.nome));
            for (const auto& id : m.idsArquivos) salvos += cache.temNoDisco(id) ? 1 : 0;
        }
        const int total = static_cast<int>(m.idsArquivos.size());
        h += QStringLiteral("<div style=\"margin-top:6px; color:%1\">%2 arquivo(s) · %3</div>")
                 .arg(corApoio)
                 .arg(total)
                 .arg(salvos == total ? QStringLiteral("<span style=\"color:%1\">✓ todos offline</span>")
                                            .arg(tema::cor::sucesso().name())
                                      : QStringLiteral("%1 ainda não salvo(s)").arg(total - salvos));
    }
    return h;
}

void JanelaPrincipal::atualizarResumoProvas(const Snapshot& s) {
    const ResumoProvas r = resumoProvas(provas_);
    const auto porDia = provasPorDia(provas_);
    formulario_->calProvas->definirProvas(porDia);
    if (calSeguinte_) {
        calSeguinte_->definirProvas(porDia);
        // O principal pode ter saltado para o mês da próxima prova agora.
        const QDate seguinte = QDate(formulario_->calProvas->yearShown(),
                                     formulario_->calProvas->monthShown(), 1).addMonths(1);
        calSeguinte_->setCurrentPage(seguinte.year(), seguinte.month());
    }

    auto pintar = [](QLabel* l, const QColor& c) {
        QPalette p = l->palette();
        p.setColor(QPalette::WindowText, c);
        l->setPalette(p);
    };

    const QDate hoje = QDate::currentDate();

    const QString corTexto = tema::token("text").name();
    const QString corApoio = tema::token("text-3").name();

    // --- Próxima prova e Em seguida: duas colunas do mesmo formato ---------
    //
    // "Em seguida" já foi uma lista estreita de blocos, e sobrava coluna: a
    // pergunta que ela responde ("e depois?") merece a mesma resposta que a
    // primeira — onde é, de onde veio a data, o que estudar. Agora são duas
    // colunas iguais, e a segunda mostra a prova seguinte por inteiro.
    const QDateTime agora = QDateTime::currentDateTime();
    std::vector<const avaliacao::Efetiva*> futuras;
    for (const auto& p : provas_) {
        if (p.av.quando.valid() && !jaPassou(p.av.quando, agora)) futuras.push_back(&p);
    }

    auto rotuloQuando = [&](const avaliacao::Efetiva& p) {
        const QDate d(p.av.quando.year, p.av.quando.month, p.av.quando.day);
        const qint64 dias = hoje.daysTo(d);
        const QString q = dias == 0   ? QStringLiteral("é hoje")
                          : dias == 1 ? QStringLiteral("amanhã")
                                      : QStringLiteral("em %1 dias").arg(dias);
        return QStringLiteral("&nbsp;&nbsp;<span style=\"color:%1\">◷ %2</span>")
            .arg(dias <= 7 ? cor::urgente().name() : corTexto, q);
    };
    auto titulo = [](const avaliacao::Efetiva& p) {
        return QStringLiteral("%1 · %2").arg(umaLinhaQt(p.av.descricao), umaLinhaQt(p.av.turmaNome));
    };

    auto* detalhe = formulario_->detalheCartaoProxima;
    auto* tituloSeg = colunaSeguintes_->findChild<QLabel*>(QStringLiteral("tituloSeguintes"));
    auto* valorSeg = colunaSeguintes_->findChild<QLabel*>(QStringLiteral("valorSeguintes"));
    auto* detalheSeg = colunaSeguintes_->findChild<QLabel*>(QStringLiteral("listaSeguintes"));
    idTurmaProxima_.clear();
    idTurmaSeguinte_.clear();
    botaoTurmaProxima_->hide();

    // Agrupa por dia: o primeiro dia com prova e o que vem depois dele.
    auto diaDe = [](const avaliacao::Efetiva& p) {
        return QDate(p.av.quando.year, p.av.quando.month, p.av.quando.day);
    };
    std::vector<const avaliacao::Efetiva*> doPrimeiroDia;
    std::vector<const avaliacao::Efetiva*> doSegundoDia;
    if (!futuras.empty()) {
        const QDate dia1 = diaDe(*futuras[0]);
        QDate dia2;
        for (const auto* p : futuras) {
            const QDate d = diaDe(*p);
            if (d == dia1) {
                doPrimeiroDia.push_back(p);
            } else if (!dia2.isValid() || d == dia2) {
                dia2 = d;
                doSegundoDia.push_back(p);
            } else {
                break;
            }
        }
    }

    // Lista compacta de provas de um dia: uma por linha, hora e prova em cima,
    // turma · local · matéria embaixo. Até kMaxLinhas; o resto é "+N", e a
    // tabela logo abaixo tem todas.
    constexpr size_t kMaxLinhas = 5;
    auto listaDoDia = [&](const std::vector<const avaliacao::Efetiva*>& v, size_t desde) {
        QString t;
        for (size_t i = desde; i < v.size() && i < desde + kMaxLinhas; ++i) {
            const auto& p = *v[i];
            const QString hora = p.av.quando.hasTime
                ? QTime(p.av.quando.hour, p.av.quando.minute).toString(QStringLiteral("HH:mm"))
                : QStringLiteral("sem hora");
            const QString marca = p.estado == avaliacao::Estado::Inferida
                ? QStringLiteral(" <span style=\"color:%1; font-weight:400\">○ inferida</span>")
                      .arg(cor::inferido().name())
                : QString();
            QStringList apoio{cortar(umaLinhaQt(p.av.turmaNome), 34)};
            for (const auto& tu : snapshot_.turmas) {
                if (tu.idTurma == p.av.idTurma && !tu.local.empty()) {
                    apoio << QString::fromStdString(tu.local);
                }
            }
            const MateriaDaProva m = materiaDaProva(snapshot_, p, provas_);
            if (!m.topicos.empty()) {
                apoio << (m.topicos.size() == 1 ? QStringLiteral("1 tópico")
                                                : QStringLiteral("%1 tópicos").arg(m.topicos.size()));
            }
            t += QStringLiteral("<div style=\"margin-top:%1px\"><span style=\"color:%2; font-weight:700\">"
                                "%3</span>&nbsp;&nbsp;<span style=\"color:%4; font-weight:600\">%5</span>%6"
                                "<br><span style=\"color:%7\">%8</span></div>")
                     .arg(i == desde ? 0 : 8)
                     .arg(corTexto, hora, corTexto,
                          cortar(umaLinhaQt(p.av.descricao), 40).toHtmlEscaped(), marca, corApoio,
                          apoio.join(QStringLiteral(" · ")).toHtmlEscaped());
        }
        if (v.size() > desde + kMaxLinhas) {
            t += QStringLiteral("<div style=\"margin-top:8px; color:%1\">+%2 — veja na tabela abaixo</div>")
                     .arg(corApoio).arg(v.size() - desde - kMaxLinhas);
        }
        return t;
    };

    if (doPrimeiroDia.size() == 1) {
        // Uma prova no dia: o destaque completo, com a matéria.
        const auto& p1 = *doPrimeiroDia[0];
        formulario_->tituloCartaoProxima->setText(QStringLiteral("PRÓXIMA PROVA") + rotuloQuando(p1));
        formulario_->valorCartaoProxima->setText(titulo(p1));
        QString dica;
        detalhe->setText(detalheDaProva(p1, &dica));
        detalhe->setToolTip(dica);
        idTurmaProxima_ = p1.av.idTurma;
        botaoTurmaProxima_->show();
    } else if (!doPrimeiroDia.empty()) {
        // Várias no mesmo dia: a lista do dia. Duas colunas "completas" do
        // mesmo dia, mais uma lista de extras, contavam a mesma história em
        // três camadas — o dia vira UMA coluna, e cada prova uma linha dela.
        const auto& p1 = *doPrimeiroDia[0];
        formulario_->tituloCartaoProxima->setText(QStringLiteral("PRÓXIMAS PROVAS") + rotuloQuando(p1));
        formulario_->valorCartaoProxima->setText(
            QStringLiteral("%1 provas · %2").arg(doPrimeiroDia.size()).arg(diaCurto(diaDe(p1))));
        detalhe->setText(listaDoDia(doPrimeiroDia, 0));
        detalhe->setToolTip(QString());
        // Sem "Abrir turma": com várias turmas no dia, qual abriria? A lista
        // e a tabela levam cada uma à sua.
    } else {
        formulario_->tituloCartaoProxima->setText(QStringLiteral("PRÓXIMA PROVA"));
        formulario_->valorCartaoProxima->setText(QStringLiteral("Nenhuma à frente"));
        detalhe->setToolTip(QString());
        // Distinguir "acabou o semestre" de "nunca entrei nas turmas" importa:
        // as duas telas são idênticas e só uma delas é problema do usuário.
        detalhe->setText(
            r.total > 0
                ? QStringLiteral("As %1 provas conhecidas já passaram.").arg(r.total)
                : QStringLiteral("Nenhuma prova coletada — use Atualizar tudo."));
    }

    // Em seguida: o próximo DIA com prova, depois do primeiro. Nunca repete o
    // dia da esquerda. Uma prova nele: completa; várias: a primeira completa
    // e as outras em lista curta embaixo.
    const bool temSeguinte = !doSegundoDia.empty();
    colunaSeguintes_->setVisible(temSeguinte);
    fileteSeguintes_->setVisible(temSeguinte);
    if (temSeguinte) {
        const auto& p2 = *doSegundoDia[0];
        tituloSeg->setText(QStringLiteral("EM SEGUIDA") + rotuloQuando(p2));
        valorSeg->setText(titulo(p2));
        QString dicaSeg;
        QString h = detalheDaProva(p2, &dicaSeg);
        detalheSeg->setToolTip(dicaSeg);
        if (doSegundoDia.size() > 1) {
            h += QStringLiteral("<div style=\"margin-top:8px; color:%1; font-weight:600\">+%2 no mesmo dia</div>")
                     .arg(cor::urgente().name())
                     .arg(doSegundoDia.size() - 1);
            h += listaDoDia(doSegundoDia, 1);
        }
        detalheSeg->setText(h);
        idTurmaSeguinte_ = p2.av.idTurma;
    }

    // --- Carga por semana --------------------------------------------------
    atualizarCarga();
    if (auto* legenda = findChild<QLabel*>(QStringLiteral("legendaCarga"))) {
        legenda->setText(
            QStringLiteral("<span style=\"color:%1\">■</span> prova&nbsp;&nbsp;&nbsp;"
                           "<span style=\"color:%1\">□</span> deduzida&nbsp;&nbsp;&nbsp;"
                           "<span style=\"color:%2\">■</span> entrega pendente&nbsp;&nbsp;&nbsp;"
                           "<span style=\"color:%4\">■</span> já passou"
                           "<span style=\"color:%3\">&nbsp;&nbsp;·&nbsp;&nbsp;clique numa semana para filtrar</span>")
                .arg(cor::urgente().name(), tema::token("surface-3").name(), corApoio,
                     CargaSemanal::corPassado().name()));
    }

    // --- A confirmar: pílula na linha da tabela -------------------------
    if (auto* pilula = findChild<QToolButton*>(QStringLiteral("pilulaConfirmar"))) {
        pilula->setVisible(r.inferidas > 0);
        pilula->setText(r.inferidas == 1 ? QStringLiteral("○ 1 data a confirmar")
                                         : QStringLiteral("○ %1 datas a confirmar")
                                               .arg(r.inferidas));
    }
    // O diálogo aberto acompanha: toda ação dele termina num `mostrar`, que
    // passa por aqui com a lista nova.
    atualizarDialogoConfirmar();
}


void JanelaPrincipal::filtrarProvasPorSemana(QDate inicio) {
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(formulario_->tvProvas->model());
    if (!proxy) return;
    if (!inicio.isValid()) {
        semanaFiltrada_ = QDate();
        filtrarProvasPorDia(QDate());
        return;
    }

    // Semana sem prova: a mesma regra do dia vazio — recusar o filtro e dizer
    // por quê, em vez de deixar uma tabela em branco.
    int provas = 0;
    for (int i = 0; i < 7; ++i) provas += formulario_->calProvas->provasEm(inicio.addDays(i));
    if (provas == 0) {
        semanaFiltrada_ = QDate();
        filtrarProvasPorDia(QDate());
        formulario_->rotuloFiltro->setText(
            QStringLiteral("A semana de %1 não tem prova — mostrando todas")
                .arg(inicio.toString(QStringLiteral("dd/MM"))));
        return;
    }

    semanaFiltrada_ = inicio;
    diaFiltrado_ = QDate();
    carga_->destacar(inicio);
    // Os sete prefixos ISO da semana, alternados: a chave da coluna Data é
    // "2026-09-28" ou "2026-09-28T15:45".
    QStringList dias;
    for (int i = 0; i < 7; ++i) dias << inicio.addDays(i).toString(Qt::ISODate);
    proxy->setFilterRegularExpression(
        QRegularExpression(QStringLiteral("^(%1)").arg(dias.join(QLatin1Char('|')))));
    formulario_->botaoTodasProvas->setEnabled(true);
    formulario_->tvProvas->expandAll();
    for (int c = 0; c < proxy->columnCount(); ++c) formulario_->tvProvas->resizeColumnToContents(c);
    contarProvasFiltradas(QStringLiteral("na semana de %1")
                              .arg(inicio.toString(QStringLiteral("dd/MM"))));
}

void JanelaPrincipal::contarProvasFiltradas(const QString& onde) {
    auto* proxy = formulario_->tvProvas->model();
    int n = 0;
    for (int g = 0; g < proxy->rowCount(); ++g) n += proxy->rowCount(proxy->index(g, 0));
    formulario_->rotuloFiltro->setText(QStringLiteral("%1 prova(s) %2").arg(n).arg(onde));
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
    if (calSeguinte_) {
        const bool doSeguinte = dia.isValid() && dia.year() == calSeguinte_->yearShown() &&
                                dia.month() == calSeguinte_->monthShown();
        if (doSeguinte) calSeguinte_->setSelectedDate(dia);
        calSeguinte_->mostrarSelecao(doSeguinte);
    }
    // Dia e semana são filtros exclusivos, e "mostrar todas" desfaz os dois.
    semanaFiltrada_ = QDate();
    if (carga_) carga_->destacar(QDate());
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
    if (semanaFiltrada_.isValid()) filtrarProvasPorSemana(semanaFiltrada_);
    else filtrarProvasPorDia(diaFiltrado_);
    painelEstudo_->definirEntradas(entradasDoPlanejamento());
    atualizarEstudo();
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
    if (navegacao_) navegacao_->sincronizar();
}

void JanelaPrincipal::montarBarraAgenda() {
    // A faixa de datas é o título da tela agora que ela pagina: sem peso
    // tipográfico ela se perde entre dois botões e a pessoa não repara que
    // mudou de semana.
    formulario_->rotuloDia->setFont(tema::fonte(tema::Papel::Titulo));
    formulario_->rotuloDia->setTextFormat(Qt::RichText);
    rotuloDeSecao(formulario_->rotuloAulas);
    // 32 px entre a linha da semana e a seção, como no protótipo: o título
    // pertence à página, não à lista de aulas.
    formulario_->rotuloAulas->setContentsMargins(0, tema::esp(4), 0, tema::esp(1));
    rotuloDeSecao(formulario_->rotuloPrazos);
    // O resumo divide a linha com o título, à direita, como no protótipo: é o
    // dado que responde "tenho aula hoje?" antes de a pessoa ler a lista. Corpo
    // inteiro, não legenda — os números em negrito precisam ser lidos de longe.
    formulario_->rotuloResumoDia->setTextFormat(Qt::RichText);

    // Setas quadradas de 32 e "Hoje" na mesma altura, medidas da referência.
    // Em código porque o QSS não fixa tamanho de QToolButton (min-height
    // conta só o conteúdo, e o glifo ◀ tem altura diferente da palavra).
    for (QToolButton* b : {formulario_->botaoSemanaAnterior, formulario_->botaoSemanaSeguinte}) {
        b->setFixedSize(32, 32);
        QFont f = b->font();
        f.setPointSizeF(f.pointSizeF() * 0.8);   // 11/14: a seta é sinal, não texto
        b->setFont(f);
    }
    formulario_->botaoHoje->setFixedHeight(32);

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
    if (alvo == formulario_->painelCalendario && ev->type() == QEvent::Resize && calSeguinte_) {
        // Cabe o segundo mês? Altura natural dos dois, mais título e legenda.
        const int precisa = 2 * formulario_->calProvas->minimumSizeHint().height() +
                            tituloCalSeguinte_->sizeHint().height() +
                            formulario_->legendaCalendario->sizeHint().height() + tema::esp(4);
        const bool cabe = formulario_->painelCalendario->height() >= precisa;
        calSeguinte_->setVisible(cabe);
        tituloCalSeguinte_->setVisible(cabe);
        return QMainWindow::eventFilter(alvo, ev);
    }
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
    auto* modelo = modeloAgenda(s, inicio, fim, hoje, arvore, estudo_);
    connect(modelo, &QStandardItemModel::itemChanged, this,
            &JanelaPrincipal::marcarEstudoDaAgenda);
    arvore->setModel(modelo);
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
    QString relativo;
    if (delta == 0) relativo = QStringLiteral("esta semana");
    else if (delta == 1) relativo = QStringLiteral("semana que vem");
    else if (delta == -1) relativo = QStringLiteral("semana passada");
    // A faixa em negrito e o "· esta semana" em peso normal e cinza, como no
    // protótipo: são duas informações, e a de peso é a data.
    QString titulo = faixaDaSemana(inicio, fim).toHtmlEscaped();
    if (!relativo.isEmpty()) {
        titulo += QStringLiteral(" <span style=\"font-weight:400; color:%1\">· %2</span>")
                      .arg(tema::token("text-3").name(), relativo);
    }
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
        // Número em negrito na cor do texto, rótulo em text-2, separador em
        // text-3: o olho pega "4 · 4 · 18" primeiro e só lê as palavras se
        // precisar.
        const QString num = QStringLiteral("<b style=\"color:%1\">%2</b>")
                                .arg(tema::token("text").name(), QStringLiteral("%1"));
        const QString ponto = QStringLiteral("&nbsp;&nbsp;<span style=\"color:%1\">·</span>&nbsp;&nbsp;")
                                  .arg(tema::token("text-3").name());
        formulario_->rotuloResumoDia->setText(
            num.arg(r.aulasHoje) +
            (r.aulasHoje == 1 ? QStringLiteral(" aula hoje") : QStringLiteral(" aulas hoje")) +
            ponto + num.arg(r.aulasAmanha) + QStringLiteral(" amanhã") + ponto +
            num.arg(naSemana) + QStringLiteral(" nesta semana"));
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
    op.incluirNoticias = escolha.noticias;
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

void JanelaPrincipal::entrarNoSigaa() {
    // Cada clique é um login na conta do aluno. Clicar de novo enquanto o
    // navegador ainda abre dobraria isso sem ganho nenhum.
    // Mensagens TEMPORÁRIAS da barra (`showMessage`), e não `status()`: o
    // rótulo de `status()` é o estado do app ("Último estado conhecido…"), e
    // escrever ali um aviso de ação o deixava preso para sempre — "Abrindo o
    // SIGAA no navegador…" continuava na tela com o portal já aberto. A
    // mensagem temporária cobre o rótulo e, ao sumir, ele volta sozinho.
    auto* barra = formulario_->statusbar;
    if (EntrarNoSigaa::emAndamento()) {
        barra->showMessage(QStringLiteral("O navegador já está abrindo o SIGAA."), 4000);
        return;
    }

    std::string login, senha;
    if (!obterCredenciais(login, senha)) return;

    // A sessão do app NÃO é encerrada nem reaproveitada: o navegador entra
    // numa sessão só dele (ver core/http/EntradaNoNavegador.h). Dividir uma
    // sessão entre os dois faria um derrubar o ViewState do outro.
    QString erro;
    auto* entrada =
        EntrarNoSigaa::abrir(config::selecionada().baseUrl, login, senha, this, &erro);
    if (!entrada) {
        barra->showMessage(erro, 8000);
        return;
    }
    // Sem prazo: fica até a entrada terminar, que é quando se sabe o que dizer.
    barra->showMessage(QStringLiteral("Abrindo o SIGAA no navegador…"));
    connect(entrada, &EntrarNoSigaa::terminou, this, [barra](bool entregue) {
        barra->showMessage(
            entregue ? QStringLiteral("SIGAA aberto no navegador.")
                     : QStringLiteral("O navegador não abriu a página de entrada. "
                                      "Tente de novo."),
            entregue ? 4000 : 8000);
    });
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
    connect(&dlg, &DialogoOpcoes::pediuAgentes, this, [this, &dlg] {
        // Os agentes moram na aba Estudo. Aceitar (e não rejeitar) Opções:
        // o aluno pode ter mudado algo antes de clicar, e perder em silêncio
        // seria pior que salvar.
        dlg.accept();
        painelEstudo_->mostrarAgentes();
        formulario_->abas->setCurrentIndex(abaEstudo_);
    });
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
    c.noticias = cfg.value(QStringLiteral("sync/noticias"), true).toBool();
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
    cfg.setValue(QStringLiteral("sync/noticias"), c.noticias);
    cfg.setValue(QStringLiteral("sync/baixar"), c.baixarMateriais);
    cfg.setValue(QStringLiteral("app/verificarAtualizacao"), c.verificarAtualizacao);

    // A escolha da rotina é também a escolha padrão do diálogo de Atualizar:
    // duas telas que configuram a mesma coisa e discordam seriam pior que uma.
    ultimaEscolha_.arquivos = c.arquivos;
    ultimaEscolha_.frequencia = c.frequencia;
    ultimaEscolha_.noticias = c.noticias;
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
        e.noticias = c.noticias;
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
        atualizarAvatar(d.login());
        agendarProxima();   // já dispara a primeira coleta, completa
        return;
    }

    atualizarAvatar(QString::fromStdString(res.cred.login));

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
    atualizarAvatar(d.login());
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
