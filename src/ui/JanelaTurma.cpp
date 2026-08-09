#include "ui/JanelaTurma.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "core/http/SigaaSession.h"
#include "core/sync/Materiais.h"
#include "ui/Modelos.h"
#include "ui_JanelaTurma.h"

namespace sigaa::ui {
namespace {

// Nome de pasta a partir do nome da turma. O SIGAA usa "/" em nome de turma
// ("TÓPICOS I/II") e isso viraria subpasta sem querer.
QString pastaSegura(const QString& s) {
    QString o;
    for (const QChar c : s) {
        o += (QString("/\\:*?\"<>|").contains(c)) ? QChar('-') : c;
    }
    return o.simplified();
}

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

JanelaTurma::JanelaTurma(Turma turma, std::string login, std::string senha, QWidget* pai)
    : QDialog(pai),
      formulario_(std::make_unique<Ui::JanelaTurma>()),
      turma_(std::move(turma)),
      login_(std::move(login)),
      senha_(std::move(senha)) {
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
    connect(formulario_->botaoBaixar, &QPushButton::clicked, this,
            &JanelaTurma::baixarSelecionados);
    connect(formulario_->botaoPasta, &QPushButton::clicked, this, [this] {
        const QString p = pastaDestino();
        QDir().mkpath(p);
        QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    // Duplo clique baixa: é o gesto que a pessoa já usa no navegador.
    connect(formulario_->tvArquivos, &QTableView::doubleClicked, this,
            [this] { baixarSelecionados(); });

    // Depois que o laço de eventos começa: a janela precisa existir antes de a
    // primeira requisição pintar "conectando..." nela.
    QTimer::singleShot(0, this, &JanelaTurma::abrir);
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

QString JanelaTurma::pastaDestino() const {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(base).filePath(QStringLiteral("SIGAA/") +
                               pastaSegura(QString::fromStdString(turma_.nome)));
}

void JanelaTurma::abrir() {
    ocupado(true);
    status(QStringLiteral("Entrando na turma…"));

    // Cópias por valor para a thread: ela não pode tocar widget nenhum.
    const std::string login = login_;
    const std::string senha = senha_;
    const Turma turma = turma_;

    // O resultado é compartilhado com a thread por shared_ptr, e não por
    // ponteiro cru com `delete` no fim: se a janela morrer antes, o lambda de
    // `finished` nunca roda (o contexto `this` corta a conexão) e o `delete`
    // não aconteceria.
    struct Saida {
        std::string erro;
        bool ok{false};
    };
    auto saida = std::make_shared<Saida>();

    auto* th = QThread::create([this, login, senha, turma, saida] {
        sessao_ = std::make_unique<http::SigaaSession>();
        if (!sessao_->login(login, senha, &saida->erro)) return;
        turmaRemota_ = std::make_unique<sync::SessaoTurma>(*sessao_);
        if (!turmaRemota_->entrar(turma, &saida->erro)) return;
        if (!turmaRemota_->abrirArquivos(&saida->erro)) return;
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
            status(QStringLiteral("Não consegui abrir: %1")
                       .arg(QString::fromStdString(saida->erro)));
            return;
        }
        arquivos_ = turmaRemota_->arquivos();
        mostrarArquivos();
    });
    th->start();
}

void JanelaTurma::mostrarArquivos() {
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

    // Lista das outras abas do menu, apenas informativa. O aluno merece saber
    // que elas existem e que este app ainda não as abre — melhor do que deixá-lo
    // concluir que a turma só tem arquivos.
    QStringList outras;
    for (const auto& m : turmaRemota_->menu()) {
        const QString q = QString::fromStdString(m);
        if (q != QStringLiteral("Arquivos")) outras << q;
    }
    formulario_->rotuloOutrasAbas->setText(
        outras.isEmpty()
            ? QString()
            : QStringLiteral("Outras abas desta turma no SIGAA, ainda não abertas "
                             "por este app: %1.")
                  .arg(outras.join(QStringLiteral(", "))));

    if (arquivos_.empty()) {
        // A distinção importa: "o professor não publicou nada" é informação;
        // "não consegui ler" é bug nosso, e confundir os dois faria o aluno
        // deixar de procurar o material no site.
        status(turmaRemota_->semArquivos()
                   ? QStringLiteral("O professor não publicou nenhum arquivo nesta turma.")
                   : QStringLiteral("Não li nenhum arquivo — o SIGAA pode ter mudado a "
                                    "página. Confira no site."));
        formulario_->botaoBaixar->setEnabled(false);
    } else {
        status(QStringLiteral("%1 arquivo(s). Selecione e clique em Baixar.")
                   .arg(arquivos_.size()));
        formulario_->botaoBaixar->setEnabled(true);
        formulario_->tvArquivos->selectRow(0);
    }
    formulario_->botaoPasta->setEnabled(true);
}

void JanelaTurma::baixarSelecionados() {
    if (ocupado_ || !turmaRemota_) return;

    QStringList ids;
    const auto sel = formulario_->tvArquivos->selectionModel();
    if (sel) {
        for (const auto& idx : sel->selectedRows(0)) {
            const QString id = idx.data(PapelIdArquivo).toString();
            if (!id.isEmpty()) ids << id;
        }
    }
    if (ids.isEmpty()) {
        status(QStringLiteral("Selecione ao menos um arquivo."));
        return;
    }

    const QString destino = pastaDestino();
    QDir().mkpath(destino);

    ocupado(true);
    status(ids.size() == 1 ? QStringLiteral("Baixando…")
                           : QStringLiteral("Baixando %1 arquivos…").arg(ids.size()));

    const std::string dir = destino.toStdString();
    std::vector<std::string> pedidos;
    for (const QString& i : ids) pedidos.push_back(i.toStdString());

    struct Saida {
        QStringList salvos;
        QStringList falhas;
    };
    auto saida = std::make_shared<Saida>();

    auto* th = QThread::create([this, pedidos, dir, saida] {
        for (const auto& id : pedidos) {
            std::string erro;
            // Um por vez: o SIGAA invalida a view se vir navegação concorrente
            // (RECON §2.2), e paralelizar aqui quebraria todos os downloads.
            if (auto p = turmaRemota_->baixar(id, dir, &erro)) {
                saida->salvos << QString::fromStdString(*p);
            } else {
                saida->falhas << QString::fromStdString(erro);
            }
        }
    });
    trabalho_ = th;
    connect(th, &QThread::finished, th, &QObject::deleteLater);

    connect(th, &QThread::finished, this, [this, saida] {
        trabalho_ = nullptr;
        ocupado(false);

        if (!saida->salvos.isEmpty()) {
            // Abre o primeiro: "baixar e abrir" é o que a pessoa queria fazer.
            // Os demais ficam na pasta, e o botão "Abrir pasta" leva até lá.
            QDesktopServices::openUrl(QUrl::fromLocalFile(saida->salvos.first()));
        }

        if (saida->falhas.isEmpty()) {
            status(saida->salvos.size() == 1
                       ? QStringLiteral("Salvo em %1")
                             .arg(QFileInfo(saida->salvos.first()).fileName())
                       : QStringLiteral("%1 arquivos salvos em Documentos/SIGAA.")
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
    formulario_->botaoBaixar->setEnabled(!sim && !arquivos_.empty());
    formulario_->tvArquivos->setEnabled(!sim);
}

void JanelaTurma::status(const QString& msg) {
    formulario_->rotuloStatus->setText(msg);
}

} // namespace sigaa::ui
