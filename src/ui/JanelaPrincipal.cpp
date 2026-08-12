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
#include <QToolBar>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>

#include "core/store/Database.h"
#include "platform/Credenciais.h"
#include "ui/CalendarioProvas.h"
#include "ui/DialogoLogin.h"
#include "ui/Icones.h"
#include "ui/JanelaDiagnostico.h"
#include "ui/JanelaTurma.h"
#include "ui/Modelos.h"
#include "ui/Trabalhador.h"
#include "ui_JanelaPrincipal.h"

namespace sigaa::ui {
namespace {

// Etiqueta com o servidor da universidade (docs/PLANO.md §2.4): 15–30 min,
// nunca abaixo de 5. O ciclo caro é raro de propósito — prova quase nunca muda,
// e entrar nas 7 turmas custa ~15 requisições contra 1 do portal.
constexpr int kMinutosPortal = 20;
constexpr int kMinutosTurmas = 6 * 60;

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
void trocarModelo(QTableView* tv, QStandardItemModel* novo, int colunaPadrao) {
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

    tv->sortByColumn(colunaPadrao, Qt::AscendingOrder);
    tv->resizeColumnsToContents();
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

    montarAcoes();
    montarStatus();
    montarProvas();
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

JanelaPrincipal::~JanelaPrincipal() = default;

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
    formulario_->acRelatorio->setIcon(icone(QStringLiteral("relatorio"), this));
    formulario_->acAuto->setIcon(icone(QStringLiteral("automatico"), this));
    formulario_->acConta->setIcon(icone(QStringLiteral("conta"), this));
}

void JanelaPrincipal::montarAcoes() {
    aplicarIcones();

    // O atalho fica em código porque QKeySequence::Refresh é F5 no Windows e no
    // Linux mas Cmd+R no macOS; o .ui só aceita a tecla literal e cravaria F5
    // nos três.
    formulario_->acAtualizar->setShortcut(QKeySequence::Refresh);

    connect(formulario_->acAtualizar, &QAction::triggered, this,
            [this] { sincronizar(false); });
    connect(formulario_->acAtualizarTudo, &QAction::triggered, this,
            [this] { sincronizar(true); });
    connect(formulario_->acRelatorio, &QAction::triggered, this, [this] {
        if (!relatorio_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(relatorio_));
    });

    // Ctrl+D, e não F12: F12 é "ferramentas do desenvolvedor" em navegador, e
    // esta janela não é para desenvolvedor — é para o aluno responder "por que
    // o SIGAA me bloqueou?" com evidência na mão.
    formulario_->acDiagnostico->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(formulario_->acDiagnostico, &QAction::triggered, this,
            &JanelaPrincipal::abrirDiagnostico);

    // Tooltip com os períodos reais: sai das constantes acima, então não pode
    // estar cravado no .ui — mudar a constante e o texto mentir é pior do que
    // não ter texto.
    formulario_->acAuto->setToolTip(
        QStringLiteral(
            "Verifica o portal a cada %1 min e entra nas turmas a cada %2 h — "
            "mas só enquanto esta janela estiver aberta. Fechou o app, nada roda.")
            .arg(kMinutosPortal)
            .arg(kMinutosTurmas / 60));
    connect(formulario_->acAuto, &QAction::toggled, this,
            &JanelaPrincipal::ligarAutomatico);

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
    const ResumoProvas r = resumoProvas(s);
    formulario_->calProvas->definirProvas(provasPorDia(s));

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
    formulario_->tvProvas->resizeColumnsToContents();

    const int n = proxy->rowCount();
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

    // As credenciais vão junto mesmo sem uso imediato: a janela só fala com o
    // SIGAA se a pessoa pedir um arquivo que não está no disco, e pedi-las
    // naquele momento interromperia o clique com um diálogo de login.
    std::string login, senha;
    if (!obterCredenciais(login, senha)) return;

    // Modal: a janela da turma pode abrir sessão própria com o SIGAA, e deixar
    // o usuário disparar um "Atualizar" por trás dela criaria duas navegações
    // concorrentes na mesma conta — que é justamente o que o SIGAA pune
    // invalidando a view (RECON §2.2).
    JanelaTurma d(*alvo, std::move(topicos), std::move(arquivos), std::move(login),
                  std::move(senha), this);
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
    mostrar(snapshot_);
    return true;
}

void JanelaPrincipal::mostrar(const Snapshot& s) {
    auto* abas = formulario_->abas;
    trocarModelo(formulario_->tvPrazos, modeloPrazos(s, formulario_->tvPrazos), 0);
    trocarModelo(formulario_->tvProvas, modeloProvas(s, formulario_->tvProvas), 0);
    trocarModelo(formulario_->tvTurmas, modeloTurmas(s, formulario_->tvTurmas), 0);
    trocarModelo(formulario_->tvAtualizacoes,
                 modeloAtualizacoes(s, formulario_->tvAtualizacoes), 0);

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
    abas->setTabText(1, QStringLiteral("Provas (%1)").arg(resumoProvas(s).total));
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

void JanelaPrincipal::sincronizar(bool comTurmas) {
    if (trabalho_) return;   // IgnoreNew: duas coletas em paralelo invalidam o
                             // ViewState do SIGAA (docs/RECON.md §2.2)

    servico::Opcoes op;
    op.incluirTurmas = comTurmas;
    // O ciclo completo já está dentro de cada turma e já sabe o que falta no
    // disco: baixar aqui é uma requisição por arquivo novo, e é o que faz a
    // janela da turma abrir sem rede depois. O ciclo só-portal não baixa —
    // ele nem sabe que existe arquivo.
    if (comTurmas) op.pastaMateriais = pastaBaseMateriais().toStdString();
    if (!obterCredenciais(op.login, op.senha)) return;

    ocupado(true);
    status(comTurmas
               ? QStringLiteral("Entrando nas turmas e baixando o material novo…")
               : QStringLiteral("Consultando o portal…"));

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
    formulario_->acRelatorio->setEnabled(!relatorio_.isEmpty());

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
    if (d.exec() != QDialog::Accepted) return false;

    sessao_.login = d.login().toStdString();
    sessao_.senha = d.senha().toStdString();
    login = sessao_.login;
    senha = sessao_.senha;
    return true;
}

void JanelaPrincipal::ligarAutomatico(bool sim) {
    QSettings cfg;
    cfg.setValue(QStringLiteral("sync/automatico"), sim);
    if (formulario_->acAuto->isChecked() != sim) formulario_->acAuto->setChecked(sim);

    if (!sim) {
        relogioPortal_->stop();
        relogioTurmas_->stop();
        return;
    }
    relogioPortal_->start(kMinutosPortal * 60 * 1000);
    relogioTurmas_->start(kMinutosTurmas * 60 * 1000);
}

void JanelaPrincipal::agendarProxima() {
    relogioPortal_ = new QTimer(this);
    relogioTurmas_ = new QTimer(this);

    // Um sync em cima do outro invalidaria o ViewState do SIGAA (RECON §2.2);
    // sincronizar() já ignora o pedido quando há coleta em andamento, então
    // aqui basta não insistir.
    connect(relogioPortal_, &QTimer::timeout, this, [this] { sincronizar(false); });
    connect(relogioTurmas_, &QTimer::timeout, this, [this] { sincronizar(true); });

    QSettings cfg;
    const bool auto_ = cfg.value(QStringLiteral("sync/automatico"), true).toBool();
    formulario_->acAuto->setChecked(auto_);
    ligarAutomatico(auto_);

    // Ao abrir, buscar já: é para isso que o usuário abriu a janela. Coleta
    // completa se ainda não há nada no banco (é a única forma de trazer provas);
    // senão o portal, que é uma requisição só.
    if (auto_) sincronizar(/*comTurmas=*/snapshot_.turmas.empty());
}

void JanelaPrincipal::aoAbrir() {
    const auto res = plat::resolverCredenciais();

    if (!res.ok()) {
        // Primeira execução: pedir a conta e já mostrar resultado. Um app que
        // abre vazio e só diz "configure em algum lugar" é abandonado ali.
        DialogoLogin d(this);
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
