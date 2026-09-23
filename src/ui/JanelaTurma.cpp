#include "ui/JanelaTurma.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QAction>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QToolButton>
#include <QTimer>
#include <QTreeView>
#include <QUrl>

#include <algorithm>

#include "core/config/Instituicao.h"
#include "core/http/SigaaSession.h"
#include "core/store/Database.h"
#include "core/sync/Baixador.h"
#include "core/sync/Materiais.h"
#include "core/frequencia/Presenca.h"
#include "core/report/TurmaMd.h"
#include "core/store/Database.h"
#include "ui/Copiar.h"
#include "ui/DialogosAvaliacao.h"
#include "ui/Distintivos.h"
#include "ui/Modelos.h"
#include "ui/Tema.h"
#include "ui_JanelaTurma.h"

namespace sigaa::ui {
namespace {

constexpr int kAbaAulas = 0;
constexpr int kAbaArquivos = 1;
constexpr int kAbaPresenca = 2;
constexpr int kAbaParticipantes = 3;

void escalarFonte(QWidget* w, qreal fator, bool negrito = false) {
    QFont f = w->font();
    f.setPointSizeF(f.pointSizeF() * fator);
    f.setBold(negrito);
    w->setFont(f);
}

void esmaecer(QWidget* w) {
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
    w->setPalette(p);
}

} // namespace

JanelaTurma::JanelaTurma(Turma turma, std::vector<TopicoAula> topicos,
                         std::vector<ArquivoTurma> arquivos,
                         std::vector<Participante> participantes, std::string login,
                         std::string senha, QWidget* pai)
    : QDialog(pai),
      formulario_(std::make_unique<Ui::JanelaTurma>()),
      turma_(std::move(turma)),
      login_(std::move(login)),
      senha_(std::move(senha)),
      arquivos_(std::move(arquivos)),
      topicos_(std::move(topicos)),
      participantes_(std::move(participantes)) {
    formulario_->setupUi(this);

    setWindowTitle(QString::fromStdString(turma_.nome));
    formulario_->rotuloTurma->setText(QString::fromStdString(turma_.nome));
    escalarFonte(formulario_->rotuloTurma, 1.3, /*negrito=*/true);
    escalarFonte(formulario_->rotuloSecao, 0.82, /*negrito=*/true);
    esmaecer(formulario_->rotuloSecao);
    for (QLabel* l : {formulario_->rotuloDetalhe, formulario_->rotuloStatus,
                      formulario_->rotuloOutrasAbas}) {
        escalarFonte(l, 0.9);
        esmaecer(l);
    }

    QStringList partes;
    if (!turma_.codigo.empty()) partes << QString::fromStdString(turma_.codigo);
    if (!turma_.horario.empty()) partes << QString::fromStdString(turma_.horario);
    if (!turma_.local.empty()) partes << QString::fromStdString(turma_.local);
    if (!turma_.periodo.empty()) partes << QString::fromStdString(turma_.periodo);
    formulario_->rotuloDetalhe->setText(partes.join(QStringLiteral(" · ")));

    for (QAbstractItemView* v :
         {static_cast<QAbstractItemView*>(formulario_->arvoreAulas),
          static_cast<QAbstractItemView*>(formulario_->tvArquivos),
          static_cast<QAbstractItemView*>(formulario_->tvParticipantes)}) {
        tema::ajustarLista(v);
        habilitarCopia(v);
    }
    // Coluna "Offline" da árvore de aulas: é a única desta janela que carrega
    // um estado em vez de um dado.
    // Na view inteira: o delegate tambem garante a altura minima das linhas,
    // que a QTreeView nao tem como .
    auto* dist = new DelegadoDistintivo(this);
    formulario_->arvoreAulas->setItemDelegate(dist);
    formulario_->tvArquivos->setItemDelegate(dist);

    // --- responsividade -----------------------------------------------------
    //
    // O diálogo nasce com 760x520 no formulário, mas a janela do aluno pode
    // ser menor — e era: com a barra de seis botões no rodapé, abaixo de
    // ~700 px os últimos saíam pela borda sem indicação nenhuma.
    //
    // `QLayout::SetMinimumSize` faz o diálogo aceitar encolher até o mínimo
    // REAL do conteúdo em vez de travar no tamanho do Designer, e o rodapé
    // quebra em duas linhas quando não couber numa.
    setMinimumSize(520, 380);
    if (auto* raiz = layout()) raiz->setSizeConstraint(QLayout::SetDefaultConstraint);

    // Abre grande. O formulário nasce com 760x520 — um tamanho escolhido no
    // Designer, não pela tela de ninguém — e numa tela de 1080p isso é menos
    // de um terço do espaço disponível, com a lista de aulas rolando para
    // mostrar seis linhas enquanto sobrava metade do monitor.
    //
    // 80% da área ÚTIL (que já desconta barra de tarefas e painéis), com teto
    // para não virar uma janela de 3000 px num monitor ultrawide, onde linhas
    // dessa largura ficam impossíveis de acompanhar com o olho.
    if (const QScreen* tela = screen() ? screen() : QGuiApplication::primaryScreen()) {
        const QRect util = tela->availableGeometry();
        resize(qMin(static_cast<int>(util.width() * 0.8), 1500),
               qMin(static_cast<int>(util.height() * 0.8), 1000));
    }

    // O botão que gera o resumo para estudar. Criado em código porque nasceu
    // depois do formulário e porque só faz sentido ao lado de "Abrir pasta" —
    // os dois levam ao mesmo lugar no disco.
    botaoResumo_ = new QPushButton(QStringLiteral("Resumo .md"), this);
    botaoResumo_->setToolTip(QStringLiteral(
        "Grava turma.md na pasta da turma: aulas, datas, materiais e provas em "
        "um arquivo só. Feito para colar num assistente de IA — ele recebe o "
        "fio da disciplina junto com os PDFs."));
    connect(botaoResumo_, &QPushButton::clicked, this, &JanelaTurma::gerarResumoMd);
    if (auto* rodape = formulario_->layoutRodape) {
        rodape->insertWidget(rodape->indexOf(formulario_->botaoPasta), botaoResumo_);
    }

    montarPresenca();

    connect(formulario_->botaoFechar, &QPushButton::clicked, this, &QDialog::accept);
    connect(formulario_->botaoAtualizar, &QPushButton::clicked, this,
            &JanelaTurma::atualizarDoSigaa);
    connect(formulario_->botaoAbrir, &QPushButton::clicked, this,
            &JanelaTurma::abrirSelecionados);
    connect(formulario_->botaoRebaixar, &QPushButton::clicked, this,
            &JanelaTurma::rebaixarSelecionados);
    connect(formulario_->botaoBaixarTudo, &QPushButton::clicked, this,
            &JanelaTurma::baixarTudo);
    connect(formulario_->botaoPasta, &QPushButton::clicked, this, [this] {
        const QString p = pastaDestino();
        QDir().mkpath(p);
        QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    // Duplo clique baixa: é o gesto que a pessoa já usa no navegador.
    connect(formulario_->tvArquivos, &QTableView::doubleClicked, this,
            [this] { abrirSelecionados(); });
    connect(formulario_->arvoreAulas, &QTreeView::doubleClicked, this,
            [this] { abrirSelecionados(); });

    // Pinta agora, com o que o banco já sabe. Sem rede, sem espera: os ciclos
    // de sincronização já visitaram esta turma, e ficar buscando de novo o que
    // está guardado era o que fazia a janela abrir num "Entrando na turma…".
    relerCacheOffline();
    mostrarConteudo();
    recarregarPresenca();

    if (topicos_.empty() && arquivos_.empty()) {
        // Nunca rodou um ciclo com turmas. Dizer isso é melhor do que ir buscar
        // por conta própria: buscar aqui esconderia do usuário que o
        // "Atualizar tudo" é o que mantém a turma disponível offline.
        status(QStringLiteral("Nada guardado desta turma ainda. Use Atualizar."));
    }
}

JanelaTurma::~JanelaTurma() {
    // Última linha de defesa. `done()` já recusa fechar com trabalho em
    // andamento, mas a janela também pode morrer junto com o pai — e a thread
    // escreve em membros nossos, então liberar antes dela terminar é
    // use-after-free.
    if (trabalho_) trabalho_->wait();

    // Encerra a sessão no SIGAA. Esta janela abre uma própria (para baixar sem
    // atrapalhar a sincronização), e abandoná-la deixaria o aluno com uma
    // sessão viva no servidor por 30 minutos a cada turma que ele abrisse —
    // que é o acúmulo que faz o login seguinte ficar sem resposta.
    //
    // Custa uma requisição na thread da interface ao fechar a janela. É o
    // único lugar em que aceitamos isso: sem sessão viva não há o que
    // encerrar, e quando há, a alternativa é o travamento que este logout
    // existe para evitar.
    //
    // `turmaRemota_` referencia `sessao_`, então morre primeiro.
    turmaRemota_.reset();
    if (sessao_) sessao_->logout();

    // A senha é cópia nossa; some junto com a janela.
    std::fill(senha_.begin(), senha_.end(), '\0');
}

void JanelaTurma::done(int r) {
    if (ocupado_) {
        // Não dá para simplesmente matar a thread: ela está no meio de um POST
        // e abortar deixaria a sessão do SIGAA num estado que o próximo pedido
        // herdaria. Melhor recusar e dizer por quê.
        status(QStringLiteral("Aguarde o download terminar…"));
        return;
    }
    QDialog::done(r);
}

QString pastaBaseMateriais() {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(QStringLiteral("SIGAA"));
}

QString JanelaTurma::pastaDestino() const {
    // O saneamento do nome e a montagem do caminho vêm de core/sync: é o mesmo
    // código que o "Atualizar tudo" usa para escolher onde gravar.
    return QString::fromStdString(
        sync::pastaDaTurma(pastaBaseMateriais().toStdString(), turma_.nome));
}

bool JanelaTurma::garantirSessao(std::string* erro, bool* criouAgora) {
    if (criouAgora) *criouAgora = false;
    // Chamada de DENTRO da thread de trabalho, nunca da GUI: ela faz login e
    // duas navegações. Só uma thread por vez toca estes membros — `ocupado_`
    // desliga os botões enquanto há trabalho, e é isso que torna seguro.
    if (turmaRemota_) return true;

    auto sessao = std::make_unique<http::SigaaSession>();
    if (!sessao->login(login_, senha_, erro)) return false;

    auto remota = std::make_unique<sync::SessaoTurma>(*sessao);
    if (!remota->entrar(turma_, erro)) return false;
    // A aba Arquivos pode faltar (turma sem material publicado tem menu menor).
    // Isso não invalida as aulas, que já vieram na resposta do `entrar` —
    // falhar aqui esconderia a linha do tempo inteira por causa de uma aba que
    // a turma nem tem.
    if (!remota->abrirArquivos(erro) && erro) erro->clear();

    // Ordem importa: `turmaRemota_` referencia `sessao_`, então a sessão tem de
    // estar no lugar antes.
    sessao_ = std::move(sessao);
    turmaRemota_ = std::move(remota);
    if (criouAgora) *criouAgora = true;
    return true;
}

void JanelaTurma::atualizarDoSigaa() {
    if (ocupado_) return;

    ocupado(true);
    status(turmaRemota_ ? QStringLiteral("Relendo a turma no SIGAA…")
                        : QStringLiteral("Entrando na turma…"));

    // O resultado é compartilhado com a thread por shared_ptr, e não por
    // ponteiro cru com `delete` no fim: se a janela morrer antes, o lambda de
    // `finished` nunca roda (o contexto `this` corta a conexão) e o `delete`
    // não aconteceria.
    struct Saida {
        std::string erro;
        bool ok{false};
        // Dias em que o professor lançou diferente do que o aluno registrou.
        // Sai da thread para virar aviso na interface — é a única coisa que a
        // atualização faz que contradiz o que o usuário escreveu.
        std::vector<frequencia::Conflito> conflitosPresenca;
    };
    auto saida = std::make_shared<Saida>();

    auto* th = QThread::create([this, saida] {
        bool sessaoNova = false;
        if (!garantirSessao(&saida->erro, &sessaoNova)) return;
        // Só relê a aba quando a sessão JÁ EXISTIA — aí sim vale conferir o
        // que o professor publicou desde que a janela abriu. Numa sessão
        // recém-criada, `garantirSessao` acabou de abrir Arquivos, e chamar de
        // novo era uma requisição gêmea da anterior, byte por byte (era o par
        // #5/#6 no diagnóstico de tráfego).
        if (!sessaoNova) turmaRemota_->abrirArquivos(nullptr);
        // Participantes custa uma requisição a mais. Falhar aqui não invalida
        // o resto: a turma pode nem ter a aba no menu, e derrubar a atualização
        // inteira por isso apagaria da tela as aulas e os arquivos que vieram
        // bem.
        //
        // O ciclo de sync também busca esta lista, mas só quando o banco está
        // sem ninguém (core/sync/Crawler.h). Daí em diante é AQUI que ela se
        // mantém em dia — na turma que o aluno abriu, e não nas sete.
        // Frequência: uma requisição, e o dado que mais muda de semana para
        // semana. Já estamos DENTRO da turma — sair e voltar para buscá-la
        // custaria as duas requisições da navegação de novo.
        if (turmaRemota_->abrirFrequencia(nullptr)) {
            Snapshot parcial;
            parcial.frequencias.push_back(turmaRemota_->frequencia());
            store::Database db;
            if (db.aberto() && db.migrar()) {
                db.gravar(parcial, static_cast<std::int64_t>(
                                       QDateTime::currentSecsSinceEpoch()));

                // Confronta com o que o aluno registrou. O professor pode ter
                // lançado — concordando, a marcação se aposenta em silêncio;
                // discordando, vira alarme, e é para esse caso que o registro
                // existia.
                auto rec = frequencia::reconciliar(turmaRemota_->frequencia(),
                                                   db.carregarMarcacoes(turma_.idTurma));
                if (rec.mudou) {
                    db.gravarMarcacoes(rec.marcacoes);
                    const auto agora =
                        static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());
                    for (const auto& c : rec.conflitos) {
                        frequencia::Mudanca mu;
                        mu.idTurma = c.idTurma;
                        mu.turmaNome = c.turmaNome;
                        mu.data = c.data;
                        mu.tipo = frequencia::TipoMudanca::SigaaAtropelou;
                        mu.de = frequencia::descrever(c.doAluno, c.faltasDoAluno);
                        mu.para = frequencia::descrever(c.doSigaa, c.faltasDoSigaa);
                        mu.quando = agora;
                        db.registrarMudancaPresenca(mu);
                    }
                    saida->conflitosPresenca = rec.conflitos;
                }
            }
        }

        if (turmaRemota_->abrirParticipantes(nullptr)) {
            // Grava já, sem esperar o próximo ciclo completo: sem isto a aba
            // voltaria vazia toda vez que a turma fosse reaberta, e custaria
            // uma requisição de novo para mostrar o que já tinha sido lido.
            //
            // Um snapshot só com participantes é seguro porque `gravar` faz
            // upsert e nunca apaga o que não veio (ver tests/database_test.cpp).
            Snapshot parcial;
            parcial.participantes = turmaRemota_->participantes();
            store::Database db;
            if (db.aberto() && db.migrar()) {
                db.gravar(parcial, static_cast<std::int64_t>(
                                       QDateTime::currentSecsSinceEpoch()));
            }
        }
        saida->ok = true;
    });
    trabalho_ = th;

    // Contexto `th`, não `this`: assim a QThread é destruída mesmo que a
    // janela feche antes — do contrário vazaria um QThread por abertura.
    connect(th, &QThread::finished, th, &QObject::deleteLater);

    connect(th, &QThread::finished, this, [this, saida] {
        trabalho_ = nullptr;
        ocupado(false);
        if (!saida->ok) {
            // O que está na tela veio do banco e continua válido. Dizer só o
            // que falhou é melhor que limpar a janela: sem internet, a turma
            // guardada é exatamente o que o aluno tem.
            status(QStringLiteral("Não consegui falar com o SIGAA (%1). "
                                  "Mostrando o que está guardado.")
                       .arg(QString::fromStdString(saida->erro)));
            return;
        }
        // Só substitui o que veio do banco quando a leitura ao vivo tem o que
        // dizer. Uma turma sem a aba Arquivos, ou um parser que tropeçou, volta
        // com listas vazias — e aceitá-las apagaria da tela material que o
        // último sync guardou. É a mesma regra do banco (coleta parcial não
        // apaga) aplicada à janela. Lista vazia CONFIRMADA pelo SIGAA vale:
        // aí o professor removeu tudo mesmo.
        if (!turmaRemota_->arquivos().empty() || turmaRemota_->semArquivos()) {
            arquivos_ = turmaRemota_->arquivos();
        }
        const auto& conteudo = turmaRemota_->conteudo();
        if (!conteudo.topicos.empty() || conteudo.semTopicos) {
            topicos_ = conteudo.topicos;
        }
        // Mesma regra: lista vazia aqui é falha, não resposta. Uma turma sem
        // participante nenhum não existe — sempre há ao menos o professor —
        // então aceitar o vazio só apagaria a lista que o banco guardou.
        if (!turmaRemota_->participantes().empty()) {
            participantes_ = turmaRemota_->participantes();
        }
        relerCacheOffline();
        mostrarConteudo();
        recarregarPresenca();
        avisarConflitosPresenca(saida->conflitosPresenca);
        status(QStringLiteral("Atualizado: %1 aula(s), %2 arquivo(s), %3 participante(s).")
                   .arg(topicos_.size())
                   .arg(arquivos_.size())
                   .arg(participantes_.size()));

    });
    th->start();
}

void JanelaTurma::gerarResumoMd() {
    report::DadosTurmaMd d;
    d.turma = turma_;
    d.topicos = topicos_;
    d.arquivos = arquivos_;

    // As provas passam pela MESMA resolução da tela principal: se o aluno
    // corrigiu uma data, é a corrigida que vai para o resumo. Um arquivo de
    // estudo que anuncia a data errada é pior que não ter arquivo.
    {
        store::Database db;
        if (db.aberto() && db.migrar()) {
            Snapshot s = db.carregarUltimo();
            std::vector<Avaliacao> daTurma;
            for (const auto& a : s.avaliacoes) {
                if (a.idTurma == turma_.idTurma) daTurma.push_back(a);
            }
            d.provas = avaliacao::efetivas(daTurma, db.carregarAjustes());
            for (const auto& f : s.frequencias) {
                if (f.idTurma == turma_.idTurma) {
                    frequenciaDoResumo_ = f;
                    d.frequencia = &frequenciaDoResumo_;
                    break;
                }
            }
        }
    }

    const QString pasta = pastaDestino();
    QDir().mkpath(pasta);
    const QString caminho = QDir(pasta).filePath(
        QString::fromLatin1(report::kNomeTurmaMd));

    QFile f(caminho);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        status(QStringLiteral("Não consegui escrever %1.").arg(caminho));
        return;
    }
    const std::string md = report::gerarTurmaMd(d);
    f.write(md.data(), static_cast<qint64>(md.size()));
    f.close();

    status(QStringLiteral("Resumo salvo em %1 — %2 aula(s), %3 prova(s).")
               .arg(QString::fromLatin1(report::kNomeTurmaMd))
               .arg(topicos_.size())
               .arg(d.provas.size()));
}


// ---------------------------------------------------------------------------
// Aba Presença
// ---------------------------------------------------------------------------

void JanelaTurma::montarPresenca() {
    auto* tv = formulario_->tvPresenca;
    tema::ajustarLista(tv);
    habilitarCopia(tv);
    tv->setItemDelegate(new DelegadoDistintivo(this));

    // QAction, e não botões soltos: as mesmas ações aparecem na barra da aba e
    // no menu do botão direito sobre o dia — que é onde se procura o que fazer
    // com uma linha.
    auto nova = [this](const QString& texto, const QString& dica,
                       void (JanelaTurma::*slot)()) {
        auto* a = new QAction(texto, this);
        a->setToolTip(dica);
        connect(a, &QAction::triggered, this, slot);
        auto* b = new QToolButton(this);
        b->setDefaultAction(a);
        auto* l = formulario_->layoutAcoesPresenca;
        l->insertWidget(l->count(), b);
        formulario_->tvPresenca->addAction(a);
        return a;
    };

    acMarcarPresente_ = nova(
        QStringLiteral("Estive presente"),
        QStringLiteral("Registra que você esteve nesta aula. Vale como seu "
                       "registro pessoal — não altera a contagem do SIGAA."),
        &JanelaTurma::marcarPresencaComoPresente);
    acMarcarFalta_ = nova(
        QStringLiteral("Faltei"),
        QStringLiteral("Registra que você faltou nesta aula, para o seu próprio "
                       "controle antes de o professor lançar."),
        &JanelaTurma::marcarPresencaComoFalta);
    acDesfazerMarcacao_ = nova(
        QStringLiteral("Desfazer"),
        QStringLiteral("Remove o seu registro deste dia. O histórico guarda que "
                       "ele existiu."),
        &JanelaTurma::desfazerMarcacao);
    acHistoricoPresenca_ = nova(
        QStringLiteral("Histórico"),
        QStringLiteral("Todos os seus registros e o que o professor lançou "
                       "depois, em ordem."),
        &JanelaTurma::verHistoricoPresenca);

    // A conexão com o modelo de seleção NÃO fica aqui.
    //
    // Neste ponto a view ainda não tem modelo, então `selectionModel()` é nulo
    // e o `connect` não faz nada — silenciosamente, que é o pior jeito de não
    // fazer. Foi por isso que "Estive presente" e "Faltei" nunca habilitavam:
    // selecionar um dia não avisava ninguém.
    //
    // Pior, `setModel` CRIA um modelo de seleção novo a cada recarga, então
    // conectar uma vez não bastaria nem depois do primeiro modelo. A conexão
    // vive em `recarregarPresenca`, ao lado do `setModel` que a invalida.
}

void JanelaTurma::recarregarPresenca() {
    store::Database db;
    if (db.aberto() && db.migrar()) {
        for (const auto& f : db.carregarUltimo().frequencias) {
            if (f.idTurma == turma_.idTurma) {
                frequencia_ = f;
                break;
            }
        }
        marcacoes_ = db.carregarMarcacoes(turma_.idTurma);
    }
    diasPresenca_ = frequencia::efetivos(frequencia_, marcacoes_);

    auto* tv = formulario_->tvPresenca;
    auto* antigo = tv->model();
    tv->setModel(modeloPresenca(diasPresenca_, tv));
    delete antigo;
    tv->resizeColumnsToContents();
    tema::esticarColuna(tv, 2);

    // Aqui, e não no montar: `setModel` acabou de trocar o modelo de seleção,
    // e qualquer conexão anterior morreu com o antigo.
    connect(tv->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { atualizarAcoesPresenca(); });

    int semRegistro = 0;
    for (const auto& d : diasPresenca_) {
        if (d.estado == frequencia::EstadoDia::NaoRegistrada) ++semRegistro;
    }

    // O texto diz o que a aba É, e o que ela NÃO é. Sem a segunda metade, um
    // aluno pode marcar-se presente em cinco dias e fechar o app achando que
    // melhorou a própria situação no SIGAA — e descobrir o contrário na
    // secretaria, que é o pior lugar possível para descobrir.
    QString t;
    if (!frequencia_.temDados) {
        t = QStringLiteral("O professor ainda não lançou frequência nesta turma. "
                           "Use “Atualizar” para buscar no SIGAA.");
    } else {
        t = QStringLiteral("%1 falta(s) de %2 permitidas, segundo o diário do "
                           "professor.")
                .arg(frequencia_.faltas())
                .arg(frequencia_.limiteFaltas());
        if (semRegistro > 0) {
            t += QStringLiteral(" %1 dia(s) sem lançamento — você pode registrar o "
                                "que aconteceu neles.")
                     .arg(semRegistro);
        }
        t += QStringLiteral("\nO que você registrar aqui é seu, e não muda a conta "
                            "do SIGAA.");
    }
    formulario_->rotuloPresenca->setText(t);

    formulario_->abas->setTabText(kAbaPresenca,
                                  QStringLiteral("Presença (%1)").arg(diasPresenca_.size()));
    atualizarAcoesPresenca();
}

std::string JanelaTurma::diaSelecionado() const {
    auto* sel = formulario_->tvPresenca->selectionModel();
    if (!sel || !sel->hasSelection()) return {};
    const QModelIndex i = sel->selectedRows(0).value(0);
    return i.isValid() ? i.data(PapelDataDia).toString().toStdString() : std::string{};
}

void JanelaTurma::atualizarAcoesPresenca() {
    const std::string iso = diaSelecionado();
    const frequencia::DiaEfetivo* dia = nullptr;
    for (const auto& d : diasPresenca_) {
        if (d.data.toIso() == iso) { dia = &d; break; }
    }

    // Marcar só onde o professor NÃO lançou. Onde ele lançou, o dado é dele —
    // deixar o botão ativo sugeriria que o aluno pode sobrescrever o diário.
    const bool podeMarcar =
        dia && (dia->estado == frequencia::EstadoDia::NaoRegistrada ||
                dia->estado == frequencia::EstadoDia::MarcadaPeloAluno);
    acMarcarPresente_->setEnabled(podeMarcar);
    acMarcarFalta_->setEnabled(podeMarcar);
    acDesfazerMarcacao_->setEnabled(
        dia && dia->estado == frequencia::EstadoDia::MarcadaPeloAluno);
}

void JanelaTurma::marcarPresencaComoPresente() { marcarPresenca(SituacaoDia::Presente); }
void JanelaTurma::marcarPresencaComoFalta() { marcarPresenca(SituacaoDia::Falta); }

void JanelaTurma::marcarPresenca(SituacaoDia situacao) {
    const std::string iso = diaSelecionado();
    if (iso.empty()) {
        status(QStringLiteral("Selecione o dia que você quer registrar."));
        return;
    }

    const frequencia::DiaEfetivo* dia = nullptr;
    for (const auto& d : diasPresenca_) {
        if (d.data.toIso() == iso) { dia = &d; break; }
    }
    if (!dia) return;

    frequencia::Marcacao m;
    m.idTurma = turma_.idTurma;
    m.data = dia->data;
    m.situacao = situacao;
    // Duas faltas por encontro é o que o SIGAA lança para as aulas geminadas
    // desta grade — e é o que o mapa mostra em todos os dias com falta da
    // captura de rede. Chutar 1 daria ao aluno um registro que discorda do
    // diário por um motivo que não é o dele.
    m.faltas = situacao == SituacaoDia::Falta ? 2 : 0;
    m.situacaoSigaaNaEpoca = dia->situacaoSigaa;
    m.editadoEm = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());

    store::Database db;
    if (!db.aberto() || !db.migrar() || !db.gravarMarcacao(m)) {
        status(QStringLiteral("Não consegui salvar o registro."));
        return;
    }

    frequencia::Mudanca mu;
    mu.idTurma = turma_.idTurma;
    mu.turmaNome = turma_.nome;
    mu.data = dia->data;
    mu.tipo = frequencia::TipoMudanca::AlunoMarcou;
    mu.de = frequencia::descrever(dia->situacaoSigaa, 0);
    mu.para = frequencia::descrever(m.situacao, m.faltas);
    mu.quando = m.editadoEm;
    db.registrarMudancaPresenca(mu);

    recarregarPresenca();
    status(situacao == SituacaoDia::Presente
               ? QStringLiteral("Registrado: você esteve nesta aula.")
               : QStringLiteral("Registrado: você faltou nesta aula."));
}

void JanelaTurma::desfazerMarcacao() {
    const std::string iso = diaSelecionado();
    if (iso.empty()) return;

    const frequencia::DiaEfetivo* dia = nullptr;
    for (const auto& d : diasPresenca_) {
        if (d.data.toIso() == iso) { dia = &d; break; }
    }
    if (!dia) return;

    store::Database db;
    if (!db.aberto() || !db.migrar()) return;

    // O histórico é gravado ANTES do DELETE: depois não há de onde tirar o que
    // estava registrado.
    frequencia::Mudanca mu;
    mu.idTurma = turma_.idTurma;
    mu.turmaNome = turma_.nome;
    mu.data = dia->data;
    mu.tipo = frequencia::TipoMudanca::AlunoDesfez;
    mu.de = frequencia::descrever(dia->situacao, dia->faltas);
    mu.para = frequencia::descrever(dia->situacaoSigaa, 0);
    mu.quando = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());
    db.registrarMudancaPresenca(mu);
    db.removerMarcacao(turma_.idTurma, iso);

    recarregarPresenca();
    status(QStringLiteral("Registro removido."));
}

void JanelaTurma::verHistoricoPresenca() {
    store::Database db;
    if (!db.aberto() || !db.migrar()) return;

    const auto mudancas = db.historicoPresenca(turma_.idTurma);
    DialogoHistorico(paraHistorico(mudancas),
                     QStringLiteral("Histórico de presença — %1")
                         .arg(QString::fromStdString(turma_.nome)),
                     QStringLiteral(
                         "Você ainda não registrou presença nesta turma. O "
                         "histórico guarda o que você marcar e o que o professor "
                         "lançar depois — inclusive quando os dois discordam."),
                     this)
        .exec();
}

void JanelaTurma::avisarConflitosPresenca(
    const std::vector<frequencia::Conflito>& cs) {
    if (cs.empty()) return;

    // Modal: a atualização acabou de aposentar um registro que o aluno fez à
    // mão, e este é o caso em que ele mais precisa saber — o professor lançou
    // falta num dia em que o aluno tinha anotado presença. Uma linha na barra
    // de status passaria batido.
    QString corpo;
    for (const auto& c : cs) {
        corpo += QStringLiteral("• %1 — você: %2 · professor: %3\n")
                     .arg(QString::fromStdString(c.data.toIso()),
                          QString::fromStdString(
                              frequencia::descrever(c.doAluno, c.faltasDoAluno)),
                          QString::fromStdString(
                              frequencia::descrever(c.doSigaa, c.faltasDoSigaa)));
    }

    QMessageBox cx(this);
    cx.setIcon(QMessageBox::Warning);
    cx.setWindowTitle(QStringLiteral("O professor lançou diferente do seu registro"));
    cx.setText(cs.size() == 1
                   ? QStringLiteral("O professor lançou um dia diferente do que você "
                                    "tinha registrado.")
                   : QStringLiteral("O professor lançou %1 dias diferentes do que você "
                                    "tinha registrado.")
                         .arg(cs.size()));
    cx.setInformativeText(
        corpo + QStringLiteral("\nO lançamento dele é o que vale no sistema. O seu "
                               "registro ficou no histórico, com a data em que você o "
                               "fez — é o que você leva para uma conversa com ele ou "
                               "com a secretaria."));
    cx.addButton(QStringLiteral("Ver histórico"), QMessageBox::AcceptRole);
    auto* ok = cx.addButton(QStringLiteral("Entendi"), QMessageBox::RejectRole);
    cx.setDefaultButton(ok);
    cx.exec();
    if (cx.clickedButton() != ok) verHistoricoPresenca();
}

void JanelaTurma::relerCacheOffline() {
    offline_.clear();
    const sync::CacheLocal cache(pastaDestino().toStdString());
    for (const auto& a : arquivos_) {
        const std::string p = cache.caminho(a.idArquivo);
        if (!p.empty()) {
            offline_.insert(QString::fromStdString(a.idArquivo),
                            QString::fromStdString(p));
        }
    }
}

bool JanelaTurma::abrirLocal(const QString& id) {
    const auto it = offline_.constFind(id);
    if (it == offline_.constEnd()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(*it));
}

void JanelaTurma::mostrarConteudo() {
    // --- aulas -------------------------------------------------------------
    auto* arvore = formulario_->arvoreAulas;
    auto* modeloAntigo = arvore->model();
    arvore->setModel(modeloAulas(topicos_, arquivos_, offline_, arvore));
    delete modeloAntigo;
    arvore->expandAll();
    arvore->resizeColumnToContents(0);
    tema::esticarColuna(arvore, 0);   // "Aula / material": o texto livre
    arvore->resizeColumnToContents(1);
    arvore->resizeColumnToContents(2);

    const int nMateriais = static_cast<int>(arquivos_.size());
    if (topicos_.empty()) {
        formulario_->rotuloSecao->setText(
            QStringLiteral("O professor não registrou tópicos de aula nesta turma. "
                           "Os arquivos publicados estão na aba Arquivos."));
    } else {
        formulario_->rotuloSecao->setText(
            QStringLiteral("%1 aula(s) registradas pelo professor. %2")
                .arg(topicos_.size())
                .arg(offline_.size() == nMateriais && nMateriais > 0
                         ? QStringLiteral("Todo o material já está salvo no seu computador.")
                         : QStringLiteral("O ✓ marca o que já está salvo para uso offline.")));
    }
    formulario_->abas->setTabText(kAbaAulas,
                                  QStringLiteral("Aulas (%1)").arg(topicos_.size()));
    formulario_->abas->setTabText(kAbaArquivos,
                                  QStringLiteral("Arquivos (%1)").arg(nMateriais));

    // --- arquivos ----------------------------------------------------------
    auto* tv = formulario_->tvArquivos;
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tv->model());
    if (!proxy) {
        proxy = new QSortFilterProxyModel(tv);
        proxy->setSortRole(PapelOrdenacao);
        tv->setModel(proxy);
    }
    auto* antigo = proxy->sourceModel();
    auto* novo = modeloArquivos(arquivos_, proxy);
    proxy->setSourceModel(novo);
    delete antigo;
    tv->resizeColumnsToContents();
    // O nome do arquivo é o que se procura na lista; o tópico é contexto.
    tema::esticarColuna(tv, 0);

    // --- participantes -----------------------------------------------------
    auto* tvp = formulario_->tvParticipantes;
    auto* proxyP = qobject_cast<QSortFilterProxyModel*>(tvp->model());
    if (!proxyP) {
        proxyP = new QSortFilterProxyModel(tvp);
        proxyP->setSortRole(PapelOrdenacao);
        tvp->setModel(proxyP);
    }
    auto* antigoP = proxyP->sourceModel();
    proxyP->setSourceModel(modeloParticipantes(participantes_, proxyP));
    delete antigoP;
    // O ícone tem tamanho fixo (36x45) e a linha precisa caber nele: sem isto o
    // Qt encolhe o avatar para a altura de uma linha de texto e o retrato vira
    // uma tarja de 16 px em que não se reconhece ninguém.
    tvp->setIconSize(QSize(36, 45));
    tvp->verticalHeader()->setDefaultSectionSize(51);
    tvp->resizeColumnsToContents();
    tema::esticarColuna(tvp, 2);   // Curso / Departamento: a coluna mais longa

    formulario_->abas->setTabText(
        kAbaParticipantes,
        QStringLiteral("Participantes (%1)").arg(participantes_.size()));

    // A contagem separada é o que a pessoa procura ("quantos somos?"), e sai
    // daqui em vez de uma coluna somada na tela.
    const auto nDocentes = std::count_if(
        participantes_.begin(), participantes_.end(),
        [](const Participante& p) { return p.papel == PapelParticipante::Docente; });
    formulario_->rotuloParticipantes->setText(
        participantes_.empty()
            ? QStringLiteral("A lista de participantes ainda não foi buscada. "
                             "Use Atualizar.")
            : QStringLiteral("%1 professor(es) e %2 aluno(s) matriculados.")
                  .arg(nDocentes)
                  .arg(participantes_.size() - static_cast<size_t>(nDocentes)));

    // O menu do SIGAA só existe quando houve sessão. Abrindo do banco, esta
    // janela nunca viu o menu — e o rótulo fica de fora em vez de mentir uma
    // lista vazia. Os campos abaixo dependem todos disso.
    const bool comSessao = turmaRemota_ != nullptr;

    // Lista das outras abas do menu, apenas informativa. O aluno merece saber
    // que elas existem e que este app ainda não as abre — melhor do que deixá-lo
    // concluir que a turma só tem arquivos.
    QStringList outras;
    if (comSessao) {
        for (const auto& m : turmaRemota_->menu()) {
            const QString q = QString::fromStdString(m);
            if (q != QStringLiteral("Arquivos") && q != QStringLiteral("Participantes")) {
                outras << q;
            }
        }
    }
    formulario_->rotuloOutrasAbas->setText(
        outras.isEmpty()
            ? QString()
            : QStringLiteral("Outras abas desta turma no SIGAA, ainda não abertas "
                             "por este app: %1.")
                  .arg(outras.join(QStringLiteral(", "))));

    if (arquivos_.empty()) {
        // Quatro estados diferentes, e confundi-los tem custo. "Não coletei
        // ainda", "o professor não publicou nada" e "esta turma nem tem a aba"
        // são informação; "não consegui ler" é bug nosso. Só o último pede que
        // o aluno vá conferir no site — dizer isso nos outros o mandaria
        // procurar o que não existe, e dizer o contrário o faria desistir de
        // material que existe.
        const bool temAba =
            comSessao && std::find(turmaRemota_->menu().begin(),
                                   turmaRemota_->menu().end(),
                                   std::string("Arquivos")) != turmaRemota_->menu().end();
        status(!comSessao
                   ? QStringLiteral("Nenhum arquivo guardado desta turma. "
                                    "Use Atualizar para buscar no SIGAA.")
               : turmaRemota_->semArquivos()
                   ? QStringLiteral("O professor não publicou nenhum arquivo nesta turma.")
               : !temAba
                   ? QStringLiteral("Esta turma não tem a aba Arquivos no SIGAA — o "
                                    "material pode estar dentro das aulas.")
                   : QStringLiteral("Não li nenhum arquivo — o SIGAA pode ter mudado a "
                                    "página. Confira no site."));
        formulario_->botaoAbrir->setEnabled(false);
        formulario_->botaoBaixarTudo->setEnabled(false);
        formulario_->botaoRebaixar->setEnabled(false);
    } else if (offline_.size() == nMateriais) {
        status(QStringLiteral("%1 arquivo(s) — todos já salvos em Documentos/SIGAA.")
                   .arg(nMateriais));
        formulario_->botaoAbrir->setEnabled(true);
        formulario_->botaoBaixarTudo->setEnabled(false);
        formulario_->botaoRebaixar->setEnabled(true);
    } else {
        status(QStringLiteral("%1 arquivo(s), %2 ainda não salvos.")
                   .arg(nMateriais)
                   .arg(nMateriais - offline_.size()));
        formulario_->botaoAbrir->setEnabled(true);
        formulario_->botaoBaixarTudo->setEnabled(true);
        formulario_->botaoRebaixar->setEnabled(!offline_.isEmpty());
    }
    formulario_->botaoPasta->setEnabled(true);
}

QStringList JanelaTurma::idsSelecionados() const {
    QStringList ids;
    auto acrescentar = [&ids](const QModelIndex& idx) {
        const QString id = idx.data(PapelIdArquivo).toString();
        if (!id.isEmpty() && idx.data(PapelBaixavel).toBool() && !ids.contains(id)) {
            ids << id;
        }
    };

    if (formulario_->abas->currentIndex() == kAbaAulas) {
        const auto* sel = formulario_->arvoreAulas->selectionModel();
        if (!sel) return ids;
        for (const auto& idx : sel->selectedRows(0)) {
            acrescentar(idx);
            // Uma AULA selecionada significa "quero o material desta aula".
            for (int i = 0; i < idx.model()->rowCount(idx); ++i) {
                acrescentar(idx.model()->index(i, 0, idx));
            }
        }
        return ids;
    }

    const auto* sel = formulario_->tvArquivos->selectionModel();
    if (!sel) return ids;
    for (const auto& idx : sel->selectedRows(0)) {
        // A tabela de arquivos não usa PapelBaixavel: tudo que está nela veio
        // da aba Arquivos e, por definição, tem download.
        const QString id = idx.data(PapelIdArquivo).toString();
        if (!id.isEmpty() && !ids.contains(id)) ids << id;
    }
    return ids;
}

void JanelaTurma::abrirSelecionados() {
    if (ocupado_) return;

    const QStringList ids = idsSelecionados();
    if (ids.isEmpty()) {
        status(formulario_->abas->currentIndex() == kAbaAulas
                   ? QStringLiteral("Selecione uma aula ou um material com arquivo.")
                   : QStringLiteral("Selecione ao menos um arquivo."));
        return;
    }

    QStringList faltando;
    for (const QString& id : ids) {
        if (!offline_.contains(id)) faltando << id;
    }

    if (faltando.isEmpty()) {
        // Nada para a rede. Baixar de novo o que a pessoa já tem produziria um
        // arquivo idêntico e faria ela esperar por isso.
        if (abrirLocal(ids.first())) {
            status(ids.size() == 1
                       ? QStringLiteral("Aberto do seu computador — já estava baixado.")
                       : QStringLiteral("Aberto do seu computador. Os %1 selecionados "
                                        "já estavam baixados.")
                             .arg(ids.size()));
        } else {
            // O manifesto dizia que existia e o sistema não abriu. Reler é o
            // que corrige o ✓ em vez de deixá-lo mentindo na tela.
            relerCacheOffline();
            mostrarConteudo();
            status(QStringLiteral("Não consegui abrir o arquivo local. "
                                  "Use “Baixar de novo”."));
        }
        return;
    }
    baixarIds(faltando, /*abrirOPrimeiro=*/true);
}

void JanelaTurma::rebaixarSelecionados() {
    if (ocupado_) return;

    const QStringList ids = idsSelecionados();
    if (ids.isEmpty()) {
        status(QStringLiteral("Selecione o que você quer baixar de novo."));
        return;
    }
    baixarIds(ids, /*abrirOPrimeiro=*/true, /*forcar=*/true);
}

void JanelaTurma::baixarTudo() {
    if (ocupado_) return;

    QStringList ids;
    for (const auto& a : arquivos_) {
        const QString id = QString::fromStdString(a.idArquivo);
        // O que já está no disco nem entra na lista: o Baixador também
        // reaproveitaria, mas assim o total mostrado no progresso é o número
        // de downloads de verdade, e não uma barra que salta de 0 a 6.
        if (!id.isEmpty() && !offline_.contains(id)) ids << id;
    }
    if (ids.isEmpty()) {
        status(QStringLiteral("Tudo já está salvo em Documentos/SIGAA."));
        return;
    }
    // Sem abrir nada: "baixar tudo" é para deixar a turma disponível offline,
    // não para encarar dez PDFs abrindo em sequência.
    baixarIds(ids, /*abrirOPrimeiro=*/false);
}

void JanelaTurma::baixarIds(const QStringList& ids, bool abrirOPrimeiro, bool forcar) {
    const QString destino = pastaDestino();
    QDir().mkpath(destino);

    ocupado(true);
    const int total = static_cast<int>(ids.size());
    status(total == 1 ? QStringLiteral("Baixando…")
                      : QStringLiteral("Baixando %1 arquivos…").arg(total));

    formulario_->barra->setRange(0, total > 1 ? total : 0);
    formulario_->barra->setValue(0);

    const std::string dir = destino.toStdString();
    std::vector<sync::PedidoDownload> pedidos;
    for (const QString& i : ids) pedidos.push_back({i.toStdString(), {}});

    // Um canal significa um login a mais. Para um ou dois arquivos isso custa
    // mais que o próprio download, e a sessão desta janela já está aberta e
    // parada — então o caminho curto é usá-la.
    const int canais = sync::canaisRecomendados(total);

    const std::string login = login_;
    const std::string senha = senha_;
    const Turma turma = turma_;
    const std::string baseUrl = config::selecionada().baseUrl;

    struct Saida {
        QStringList salvos;
        QStringList falhas;
        std::vector<std::pair<std::string, std::string>> idsSalvos;   // id -> caminho
    };
    auto saida = std::make_shared<Saida>();

    auto* th = QThread::create([this, pedidos, dir, saida, canais, login, senha, turma,
                                baseUrl, forcar] {
        auto anotar = [&saida](const std::string& id, const sync::ItemBaixado& r) {
            if (r.ok()) {
                saida->salvos << QString::fromStdString(r.caminho);
                saida->idsSalvos.emplace_back(id, r.caminho);
            } else {
                saida->falhas << QString::fromStdString(r.erro);
            }
        };

        auto progresso = [this](int prontos, int) {
            // Chamada de threads que não são a da GUI: a barra só pode ser
            // tocada por evento em fila.
            QMetaObject::invokeMethod(
                this, [this, prontos] { formulario_->barra->setValue(prontos); },
                Qt::QueuedConnection);
        };

        if (canais <= 1) {
            // Caminho curto: uma sessão só, desta janela — criada agora se for
            // o primeiro download desde que a janela abriu. Um arquivo por vez:
            // o SIGAA invalida a view se vir navegação concorrente na MESMA
            // sessão (RECON §2.2).
            std::string erroSessao;
            if (!garantirSessao(&erroSessao)) {
                for (const auto& p : pedidos) {
                    sync::ItemBaixado r;
                    r.idArquivo = p.idArquivo;
                    r.erro = erroSessao;
                    anotar(p.idArquivo, r);
                }
                return;
            }
            sync::CacheLocal cache(dir);
            int feitos = 0;
            for (const auto& p : pedidos) {
                sync::ItemBaixado r;
                r.idArquivo = p.idArquivo;
                std::string erro;
                if (auto caminho = turmaRemota_->baixar(p.idArquivo, dir, &erro)) {
                    // O cache pode redirecionar para uma cópia que já estava na
                    // pasta (mesmo conteúdo republicado com id novo). Abrir
                    // `*caminho` nesse caso abriria um arquivo já apagado.
                    r.caminho = cache.registrar(p.idArquivo, *caminho);
                } else {
                    r.erro = erro;
                }
                anotar(p.idArquivo, r);
                progresso(++feitos, static_cast<int>(pedidos.size()));
            }
            return;
        }

        // Caminho paralelo: cada canal com a PRÓPRIA sessão. Ver o cabeçalho
        // de core/sync/Baixador.h para por que não dá para paralelizar dentro
        // de uma sessão só.
        sync::Baixador b(sync::canalSigaa(baseUrl, login, senha, turma), dir, canais);
        b.setForcar(forcar);
        const auto res = b.baixar(pedidos, progresso);
        for (const auto& r : res) anotar(r.idArquivo, r);
    });
    trabalho_ = th;
    connect(th, &QThread::finished, th, &QObject::deleteLater);

    connect(th, &QThread::finished, this, [this, saida, abrirOPrimeiro] {
        trabalho_ = nullptr;

        for (const auto& [id, caminho] : saida->idsSalvos) {
            offline_.insert(QString::fromStdString(id), QString::fromStdString(caminho));
        }
        mostrarConteudo();
        ocupado(false);
        formulario_->barra->setRange(0, 0);

        if (abrirOPrimeiro && !saida->salvos.isEmpty()) {
            // "Baixar e abrir" é o que a pessoa queria fazer. Os demais ficam
            // na pasta, e o botão "Abrir pasta" leva até lá.
            QDesktopServices::openUrl(QUrl::fromLocalFile(saida->salvos.first()));
        }

        if (saida->falhas.isEmpty()) {
            status(saida->salvos.size() == 1
                       ? QStringLiteral("Salvo em %1")
                             .arg(QFileInfo(saida->salvos.first()).fileName())
                       : QStringLiteral("%1 arquivos salvos em Documentos/SIGAA — "
                                        "disponíveis offline.")
                             .arg(saida->salvos.size()));
        } else {
            status(QStringLiteral("%1 salvo(s), %2 falhou(ram): %3")
                       .arg(saida->salvos.size())
                       .arg(saida->falhas.size())
                       .arg(saida->falhas.first()));
        }
    });
    th->start();
}

void JanelaTurma::ocupado(bool sim) {
    ocupado_ = sim;
    formulario_->barra->setVisible(sim);
    formulario_->botaoAbrir->setEnabled(!sim && !arquivos_.empty());
    formulario_->botaoBaixarTudo->setEnabled(
        !sim && offline_.size() < static_cast<int>(arquivos_.size()));
    // "Baixar de novo" só faz sentido quando há algo baixado para refazer.
    // Desligado, ele também comunica o estado: nada aqui está no disco ainda.
    formulario_->botaoRebaixar->setEnabled(!sim && !offline_.isEmpty());
    formulario_->botaoAtualizar->setEnabled(!sim);
    formulario_->tvArquivos->setEnabled(!sim);
    formulario_->arvoreAulas->setEnabled(!sim);
}

void JanelaTurma::status(const QString& msg) {
    formulario_->rotuloStatus->setText(msg);
}

} // namespace sigaa::ui
