#include "ui/JanelaTurma.h"

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
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QUrl>

#include <algorithm>

#include "core/config/Instituicao.h"
#include "core/http/SigaaSession.h"
#include "core/sync/Baixador.h"
#include "core/sync/Materiais.h"
#include "ui/Modelos.h"
#include "ui_JanelaTurma.h"

namespace sigaa::ui {
namespace {

constexpr int kAbaAulas = 0;
constexpr int kAbaArquivos = 1;

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
                         std::vector<ArquivoTurma> arquivos, std::string login,
                         std::string senha, QWidget* pai)
    : QDialog(pai),
      formulario_(std::make_unique<Ui::JanelaTurma>()),
      turma_(std::move(turma)),
      login_(std::move(login)),
      senha_(std::move(senha)),
      arquivos_(std::move(arquivos)),
      topicos_(std::move(topicos)) {
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

bool JanelaTurma::garantirSessao(std::string* erro) {
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
    };
    auto saida = std::make_shared<Saida>();

    auto* th = QThread::create([this, saida] {
        if (!garantirSessao(&saida->erro)) return;
        // Sessão que já existia: relê a aba para pegar o que o professor
        // publicou desde que a janela abriu.
        turmaRemota_->abrirArquivos(nullptr);
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
        relerCacheOffline();
        mostrarConteudo();
        status(QStringLiteral("Atualizado: %1 aula(s), %2 arquivo(s).")
                   .arg(topicos_.size())
                   .arg(arquivos_.size()));
    });
    th->start();
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
    tv->horizontalHeader()->setStretchLastSection(true);

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
            if (q != QStringLiteral("Arquivos")) outras << q;
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
                    r.caminho = *caminho;
                    cache.registrar(p.idArquivo, *caminho);
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
