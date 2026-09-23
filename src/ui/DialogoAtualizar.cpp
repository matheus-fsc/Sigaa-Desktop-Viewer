#include "ui/DialogoAtualizar.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

// Estimativa de tempo, em segundos, para o que foi escolhido.
//
// Números grosseiros de propósito — o ponto não é acertar o relógio, é deixar
// a conta VISÍVEL antes de a pessoa esperar por ela. "≈ 40 s" ao lado de seis
// turmas marcadas é o que faz alguém desmarcar cinco.
//
// A base é a captura de rede: ~1,5 s por requisição, 1 requisição no portal,
// 2 para entrar na turma e abrir a linha do tempo, 1 por aba extra.
int segundosEstimados(int turmas, bool arquivos, bool frequencia) {
    constexpr int kPorRequisicao = 2;   // arredondado para cima: inclui o parse
    int reqs = 1;                       // o portal, sempre
    const int porTurma = 2 + (arquivos ? 1 : 0) + (frequencia ? 1 : 0);
    reqs += turmas * porTurma;
    return reqs * kPorRequisicao;
}

} // namespace

DialogoAtualizar::DialogoAtualizar(const std::vector<Turma>& turmas,
                                   const Escolha& ultima, QWidget* pai)
    : QDialog(pai), turmas_(turmas) {
    setWindowTitle(QStringLiteral("Atualizar"));
    setModal(true);
    resize(680, 460);

    auto* raiz = new QVBoxLayout(this);
    raiz->setSpacing(tema::esp(3));
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));

    auto* titulo = new QLabel(QStringLiteral("O que você quer buscar agora?"), this);
    titulo->setFont(tema::fonte(tema::Papel::Subtitulo));
    raiz->addWidget(titulo);

    auto* nota = new QLabel(
        QStringLiteral("Os prazos e as notícias do portal vêm sempre — é uma "
                       "requisição só. O que custa tempo é entrar em cada turma."),
        this);
    nota->setWordWrap(true);
    nota->setFont(tema::fonte(tema::Papel::Legenda));
    raiz->addWidget(nota);

    // --- corpo: turmas à esquerda, opções à direita -------------------------
    auto* corpo = new QHBoxLayout;
    corpo->setSpacing(tema::esp(4));

    auto* caixaTurmas = new QGroupBox(QStringLiteral("Turmas"), this);
    auto* lt = new QVBoxLayout(caixaTurmas);
    lt->setSpacing(tema::esp(2));

    lista_ = new QListWidget(caixaTurmas);
    lista_->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto& t : turmas_) {
        if (t.frontEndId.empty()) continue;   // sem como entrar (RECON §1.2)
        auto* it = new QListWidgetItem(QString::fromStdString(t.nome), lista_);
        it->setData(Qt::UserRole, QString::fromStdString(t.idTurma));
        const bool marcada = ultima.turmas.empty()
                                 ? false
                                 : ultima.turmas.count(t.idTurma) > 0;
        it->setCheckState(marcada ? Qt::Checked : Qt::Unchecked);
        if (!t.horario.empty()) {
            it->setToolTip(QString::fromStdString(t.horario + " · " + t.periodo));
        }
    }
    lt->addWidget(lista_, 1);

    auto* atalhos = new QHBoxLayout;
    auto* todas = new QPushButton(QStringLiteral("Todas"), caixaTurmas);
    auto* nenhuma = new QPushButton(QStringLiteral("Nenhuma"), caixaTurmas);
    atalhos->addWidget(todas);
    atalhos->addWidget(nenhuma);
    atalhos->addStretch();
    lt->addLayout(atalhos);
    corpo->addWidget(caixaTurmas, 3);

    auto* caixaOpcoes = new QGroupBox(QStringLiteral("Em cada turma marcada"), this);
    auto* lo = new QVBoxLayout(caixaOpcoes);
    lo->setSpacing(tema::esp(3));

    frequencia_ = new QCheckBox(QStringLiteral("Frequência (faltas)"), caixaOpcoes);
    frequencia_->setToolTip(
        QStringLiteral("Abre o mapa de frequência. É o dado que mais muda de "
                       "semana para semana."));
    frequencia_->setChecked(ultima.frequencia);

    arquivos_ = new QCheckBox(QStringLiteral("Arquivos publicados"), caixaOpcoes);
    arquivos_->setToolTip(
        QStringLiteral("Abre a aba Arquivos para descobrir material novo."));
    arquivos_->setChecked(ultima.arquivos);

    baixar_ = new QCheckBox(QStringLiteral("Baixar o material novo"), caixaOpcoes);
    baixar_->setToolTip(
        QStringLiteral("Salva em Documentos/SIGAA o que ainda não está no disco. "
                       "Depende de “Arquivos publicados”."));
    baixar_->setChecked(ultima.baixarMateriais);

    lo->addWidget(frequencia_);
    lo->addWidget(arquivos_);
    lo->addWidget(baixar_);
    lo->addSpacing(tema::esp(2));

    soPortal_ = new QCheckBox(QStringLiteral("Só o portal (não entrar nas turmas)"),
                              caixaOpcoes);
    soPortal_->setToolTip(
        QStringLiteral("O ciclo rápido: prazos, provas e notícias, sem visitar "
                       "turma nenhuma. Uma requisição."));
    soPortal_->setChecked(!ultima.entrarNasTurmas);
    lo->addWidget(soPortal_);

    lo->addStretch();
    corpo->addWidget(caixaOpcoes, 2);
    raiz->addLayout(corpo, 1);

    resumo_ = new QLabel(this);
    resumo_->setWordWrap(true);
    resumo_->setProperty("classe", QStringLiteral("recado"));
    raiz->addWidget(resumo_);

    botoes_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    botoes_->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Atualizar"));
    botoes_->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    botoes_->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(botoes_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(botoes_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes_);

    // --- ligações -----------------------------------------------------------
    auto marcarTodas = [this](Qt::CheckState e) {
        for (int i = 0; i < lista_->count(); ++i) lista_->item(i)->setCheckState(e);
        atualizarResumo();
    };
    connect(todas, &QPushButton::clicked, this, [marcarTodas] { marcarTodas(Qt::Checked); });
    connect(nenhuma, &QPushButton::clicked, this,
            [marcarTodas] { marcarTodas(Qt::Unchecked); });

    connect(lista_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        // Marcar uma turma implica querer entrar nela. Sem isto, o aluno marca
        // a turma, clica em Atualizar e recebe só o portal — e conclui que o
        // recurso não funciona.
        if (soPortal_->isChecked()) {
            for (int i = 0; i < lista_->count(); ++i) {
                if (lista_->item(i)->checkState() == Qt::Checked) {
                    soPortal_->setChecked(false);
                    break;
                }
            }
        }
        atualizarResumo();
    });

    // O CAMINHO RÁPIDO: duplo clique escolhe a turma e confirma. É o gesto para
    // "quero saber da aula de hoje" sem passar por caixa nenhuma.
    connect(lista_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        for (int i = 0; i < lista_->count(); ++i) {
            lista_->item(i)->setCheckState(lista_->item(i) == it ? Qt::Checked
                                                                 : Qt::Unchecked);
        }
        soPortal_->setChecked(false);
        accept();
    });

    for (QCheckBox* c : {frequencia_, arquivos_, baixar_}) {
        connect(c, &QCheckBox::toggled, this, &DialogoAtualizar::atualizarResumo);
    }
    connect(soPortal_, &QCheckBox::toggled, this, [this](bool sim) {
        // Desabilitar em vez de esconder: a pessoa vê o que deixou de fora e
        // por quê, em vez de a caixa sumir e ela procurar onde foi parar.
        lista_->setEnabled(!sim);
        frequencia_->setEnabled(!sim);
        arquivos_->setEnabled(!sim);
        baixar_->setEnabled(!sim);
        atualizarResumo();
    });
    connect(arquivos_, &QCheckBox::toggled, baixar_, &QWidget::setEnabled);

    lista_->setEnabled(!soPortal_->isChecked());
    frequencia_->setEnabled(!soPortal_->isChecked());
    arquivos_->setEnabled(!soPortal_->isChecked());
    baixar_->setEnabled(!soPortal_->isChecked() && arquivos_->isChecked());
    atualizarResumo();
}

void DialogoAtualizar::atualizarResumo() {
    int marcadas = 0;
    for (int i = 0; i < lista_->count(); ++i) {
        if (lista_->item(i)->checkState() == Qt::Checked) ++marcadas;
    }

    const bool soPortal = soPortal_->isChecked();
    // Nenhuma turma marcada não é erro: é o ciclo só-portal, escrito de outro
    // jeito. Bloquear o botão aqui seria um beco sem saída para quem só quer
    // conferir os prazos.
    const int turmasReais = soPortal ? 0 : marcadas;
    const int seg = segundosEstimados(turmasReais, arquivos_->isChecked(),
                                      frequencia_->isChecked());

    QString t;
    if (turmasReais == 0) {
        t = QStringLiteral("Só o portal: prazos, provas e notícias. ");
    } else if (turmasReais == 1) {
        t = QStringLiteral("1 turma, mais o portal. ");
    } else {
        t = QStringLiteral("%1 turmas, mais o portal. ").arg(turmasReais);
    }
    t += QStringLiteral("Estimativa: cerca de %1 s esperando o SIGAA.").arg(seg);

    if (turmasReais > 0 && turmasReais < lista_->count()) {
        t += QStringLiteral("\nAs turmas não marcadas ficam como estão — nada é "
                            "apagado, o banco só não recebe notícia delas agora.");
    }
    resumo_->setText(t);
}

DialogoAtualizar::Escolha DialogoAtualizar::escolha() const {
    Escolha e;
    e.entrarNasTurmas = !soPortal_->isChecked();
    e.arquivos = arquivos_->isChecked();
    e.frequencia = frequencia_->isChecked();
    e.baixarMateriais = arquivos_->isChecked() && baixar_->isChecked();

    if (!e.entrarNasTurmas) return e;

    for (int i = 0; i < lista_->count(); ++i) {
        const auto* it = lista_->item(i);
        if (it->checkState() == Qt::Checked) {
            e.turmas.insert(it->data(Qt::UserRole).toString().toStdString());
        }
    }
    // Nenhuma marcada com "entrar nas turmas" ligado seria uma coleta que visita
    // zero turmas e mesmo assim se anuncia como completa. Cai no só-portal, que
    // é o que ela de fato é.
    if (e.turmas.empty()) e.entrarNasTurmas = false;
    return e;
}

} // namespace sigaa::ui
