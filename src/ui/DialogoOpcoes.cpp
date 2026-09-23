#include "ui/DialogoOpcoes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/atualizacao/Atualizador.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

QString rotuloIntervalo(int minutos) {
    constexpr int kDia = 24 * 60;
    if (minutos == kDia) return QObject::tr("uma vez por dia");
    if (minutos % kDia == 0) {
        const int dias = minutos / kDia;
        return dias == 7 ? QObject::tr("uma vez por semana")
                         : QObject::tr("a cada %1 dias").arg(dias);
    }
    if (minutos == 60) return QObject::tr("a cada hora");
    if (minutos % 60 == 0) return QObject::tr("a cada %1 horas").arg(minutos / 60);
    return QObject::tr("a cada %1 minutos").arg(minutos);
}

void encher(QComboBox* c, const QList<int>& valores, int atual) {
    for (int v : valores) c->addItem(rotuloIntervalo(v), v);

    // Altura mínima EM C++, não no QSS.
    //
    // O layout aperta primeiro quem tem o menor mínimo, e um QComboBox sem
    // mínimo explícito aceita ser espremido até virar uma fatia de poucos
    // pixels — foi o que aconteceu: o diálogo abria mais curto que o conteúdo
    // e as duas linhas do formulário desapareciam, sobrepostas aos rótulos.
    //
    // `min-height` na folha de estilo NÃO resolve: ela muda o desenho e não o
    // `sizeHint`, então o layout continua alocando a altura antiga (a mesma
    // armadilha de tema::kRespiroCelula). Aqui o número entra na conta.
    c->setMinimumHeight(c->fontMetrics().height() + tema::esp(4));

    if (const int i = c->findData(atual); i >= 0) {
        c->setCurrentIndex(i);
        return;
    }

    // Valor fora da lista: de uma versão anterior com outros limites, ou
    // editado à mão no arquivo de configuração. Sobe para o PRIMEIRO permitido
    // que não seja mais frequente que o pedido.
    //
    // Não para o extremo da lista: quem tinha 20 minutos configurado quer o
    // app atento, e acordar com "a cada 2 dias" seria uma mudança silenciosa
    // na direção oposta à intenção dele. Nem para o valor mais próximo em
    // qualquer direção: arredondar 20 min para baixo furaria o piso que a
    // lista existe para garantir.
    for (int idx = 0; idx < c->count(); ++idx) {
        if (c->itemData(idx).toInt() >= atual) {
            c->setCurrentIndex(idx);
            return;
        }
    }
    // Pedido MAIOR que o maior permitido: aí sim o extremo é o certo.
    c->setCurrentIndex(c->count() - 1);
}

} // namespace

const QList<int>& DialogoOpcoes::intervalosPortal() {
    // DUAS HORAS é o piso, e não quinze minutos.
    //
    // Nada do que o portal traz muda em quinze minutos: prazo de entrega é
    // publicado com dias de antecedência, notícia de turma não é jornal. O
    // ganho de olhar de quinze em quinze é zero; o custo é uma conta que
    // consulta o SIGAA 96 vezes por dia e não tem como saber que isso chama
    // atenção.
    //
    // O teto de dois dias existe porque além disso o recurso deixa de ser
    // "automático" — quem quer menos que isso desliga a rotina e clica em
    // Atualizar, que é honesto e está a um botão de distância.
    static const QList<int> v{120, 240, 480, 720, 1440, 2880};
    return v;
}

const QList<int>& DialogoOpcoes::intervalosCompleto() {
    // UMA VEZ POR DIA é o máximo. O ciclo completo entra em cada turma — 4
    // requisições por turma, 28 numa grade de sete — e é a diferença entre "o
    // app consulta o SIGAA" e "o app varre o SIGAA".
    //
    // O que ele traz também não pede mais que isso: material publicado, mapa
    // de frequência e tópicos de aula mudam em escala de dias.
    static const QList<int> v{1440, 2880, 4320, 10080};
    return v;
}

DialogoOpcoes::DialogoOpcoes(const Config& atual, QWidget* pai) : QDialog(pai) {
    setWindowTitle(QStringLiteral("Opções"));
    setModal(true);

    auto* raiz = new QVBoxLayout(this);
    raiz->setSpacing(tema::esp(3));
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));

    // --- rotina automática --------------------------------------------------
    auto* caixa = new QGroupBox(QStringLiteral("Atualização automática"), this);
    auto* lv = new QVBoxLayout(caixa);
    lv->setSpacing(tema::esp(3));

    automatico_ = new QCheckBox(
        QStringLiteral("Buscar novidades sozinho enquanto o app estiver aberto"), caixa);
    automatico_->setChecked(atual.automatico);
    lv->addWidget(automatico_);

    auto* form = new QFormLayout;
    form->setSpacing(tema::esp(3));

    intervaloPortal_ = new QComboBox(caixa);
    encher(intervaloPortal_, intervalosPortal(), atual.minutosPortal);
    intervaloPortal_->setToolTip(
        QStringLiteral("Uma requisição: prazos, provas e notícias do portal."));
    form->addRow(QStringLiteral("Portal"), intervaloPortal_);

    intervaloCompleto_ = new QComboBox(caixa);
    encher(intervaloCompleto_, intervalosCompleto(), atual.minutosCompleto);
    intervaloCompleto_->setToolTip(
        QStringLiteral("Entra em cada turma. Custa cerca de 4 requisições por "
                       "turma — é o ciclo caro."));
    form->addRow(QStringLiteral("Dentro das turmas"), intervaloCompleto_);
    lv->addLayout(form);

    auto* sub = new QLabel(QStringLiteral("Em cada turma, buscar:"), caixa);
    sub->setFont(tema::fonte(tema::Papel::Legenda));
    lv->addWidget(sub);

    frequencia_ = new QCheckBox(QStringLiteral("Frequência (faltas)"), caixa);
    frequencia_->setChecked(atual.frequencia);
    arquivos_ = new QCheckBox(QStringLiteral("Arquivos publicados"), caixa);
    arquivos_->setChecked(atual.arquivos);
    baixar_ = new QCheckBox(QStringLiteral("Baixar o material novo"), caixa);
    baixar_->setChecked(atual.baixarMateriais);
    baixar_->setToolTip(QStringLiteral("Depende de “Arquivos publicados”."));
    for (QCheckBox* c : {frequencia_, arquivos_, baixar_}) lv->addWidget(c);

    raiz->addWidget(caixa);

    resumo_ = new QLabel(this);
    resumo_->setWordWrap(true);
    resumo_->setProperty("classe", QStringLiteral("recado"));
    raiz->addWidget(resumo_);

    // --- atualização do app -------------------------------------------------
    auto* atu = new QGroupBox(QStringLiteral("Atualização do aplicativo"), this);
    auto* la = new QVBoxLayout(atu);
    la->setSpacing(tema::esp(2));

    auto* versao = new QLabel(
        QStringLiteral("Você está na versão %1.")
            .arg(QString::fromStdString(atualizacao::versaoAtual())),
        atu);
    versao->setFont(tema::fonte(tema::Papel::Legenda));
    la->addWidget(versao);

    verificarAtualizacao_ = new QCheckBox(
        QStringLiteral("Procurar versão nova ao abrir o app"), atu);
    verificarAtualizacao_->setChecked(atual.verificarAtualizacao);
    verificarAtualizacao_->setToolTip(
        QStringLiteral("Uma consulta ao GitHub na abertura. Instalar continua "
                       "sendo um clique seu — o app nunca troca o próprio "
                       "executável sozinho."));
    la->addWidget(verificarAtualizacao_);

    estadoAtualizacao_ = new QLabel(atu);
    estadoAtualizacao_->setWordWrap(true);
    estadoAtualizacao_->setFont(tema::fonte(tema::Papel::Legenda));
    la->addWidget(estadoAtualizacao_);

    auto* linhaAtu = new QHBoxLayout;
    botaoProcurar_ = new QPushButton(QStringLiteral("Procurar agora"), atu);
    botaoInstalar_ = new QPushButton(QStringLiteral("Baixar e instalar"), atu);
    botaoInstalar_->setVisible(false);
    connect(botaoProcurar_, &QPushButton::clicked, this,
            &DialogoOpcoes::pediuProcurarAtualizacao);
    connect(botaoInstalar_, &QPushButton::clicked, this,
            &DialogoOpcoes::pediuInstalarAtualizacao);
    linhaAtu->addWidget(botaoProcurar_);
    linhaAtu->addWidget(botaoInstalar_);
    linhaAtu->addStretch();
    la->addLayout(linhaAtu);
    raiz->addWidget(atu);

    // --- desenvolvedor ------------------------------------------------------
    // Separado e no fim, porque não é para o aluno. O diagnóstico de tráfego e
    // o relatório HTML existem para depurar o app, não para usá-lo — e ocupar
    // a barra principal com eles gastava o lugar mais valioso da janela com
    // duas telas que ninguém abre duas vezes.
    auto* dev = new QGroupBox(QStringLiteral("Desenvolvedor"), this);
    auto* ld = new QVBoxLayout(dev);
    ld->setSpacing(tema::esp(2));

    auto* nota = new QLabel(
        QStringLiteral("Ferramentas para investigar o app quando algo dá errado. "
                       "Não são necessárias para o uso normal."),
        dev);
    nota->setWordWrap(true);
    nota->setFont(tema::fonte(tema::Papel::Legenda));
    ld->addWidget(nota);

    auto* linha = new QHBoxLayout;
    auto* bDiag = new QPushButton(QStringLiteral("Diagnóstico de rede"), dev);
    auto* bRel = new QPushButton(QStringLiteral("Abrir relatório HTML"), dev);
    connect(bDiag, &QPushButton::clicked, this, &DialogoOpcoes::pediuDiagnostico);
    connect(bRel, &QPushButton::clicked, this, &DialogoOpcoes::pediuRelatorio);
    linha->addWidget(bDiag);
    linha->addWidget(bRel);
    linha->addStretch();
    ld->addLayout(linha);
    raiz->addWidget(dev);

    raiz->addStretch();

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                        this);
    botoes->button(QDialogButtonBox::Save)->setText(QStringLiteral("Salvar"));
    botoes->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    connect(botoes, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes);

    // --- ligações -----------------------------------------------------------
    auto ligar = [this](bool sim) {
        for (QWidget* w : {static_cast<QWidget*>(intervaloPortal_),
                           static_cast<QWidget*>(intervaloCompleto_),
                           static_cast<QWidget*>(frequencia_),
                           static_cast<QWidget*>(arquivos_),
                           static_cast<QWidget*>(baixar_)}) {
            w->setEnabled(sim);
        }
        if (sim) baixar_->setEnabled(arquivos_->isChecked());
        atualizarResumo();
    };
    connect(automatico_, &QCheckBox::toggled, this, ligar);
    connect(arquivos_, &QCheckBox::toggled, this, [this](bool sim) {
        baixar_->setEnabled(automatico_->isChecked() && sim);
        atualizarResumo();
    });
    for (QComboBox* c : {intervaloPortal_, intervaloCompleto_}) {
        connect(c, &QComboBox::currentIndexChanged, this,
                [this] { atualizarResumo(); });
    }
    connect(frequencia_, &QCheckBox::toggled, this, [this] { atualizarResumo(); });

    ligar(automatico_->isChecked());

    // O diálogo nasce do TAMANHO DO CONTEÚDO, e não de um número escolhido
    // aqui. Um `resize(560, 520)` cravado abria mais curto que os campos
    // precisavam, e o layout resolvia o conflito esmagando o que tinha menos
    // mínimo — os dois combos. `SetMinimumSize` impede que ele encolha abaixo
    // do que cabe, inclusive quando a fonte do sistema for maior que a minha.
    raiz->setSizeConstraint(QLayout::SetMinimumSize);
    adjustSize();
}

void DialogoOpcoes::atualizarResumo() {
    if (!automatico_->isChecked()) {
        resumo_->setText(QStringLiteral(
            "Desligado: o app só fala com o SIGAA quando você clicar em "
            "Atualizar. Nada é buscado sozinho."));
        return;
    }

    const int portal = intervaloPortal_->currentData().toInt();
    const int completo = intervaloCompleto_->currentData().toInt();

    // A CONTA APARECE, e é o ponto deste rótulo: "a cada 15 minutos" não diz
    // nada sobre o peso; "cerca de 100 requisições por dia" diz. É o número
    // que deixa alguém escolher um intervalo maior por vontade própria, em vez
    // de descobrir o problema quando a conta parar de logar.
    const int porDia = (24 * 60) / portal;
    const int completosPorDia = (24 * 60) / completo;
    const int porTurma = 2 + (arquivos_->isChecked() ? 1 : 0) +
                         (frequencia_->isChecked() ? 1 : 0);

    resumo_->setText(
        QStringLiteral(
            "Cerca de %1 consultas ao portal por dia, mais %2 ciclo(s) completo(s) "
            "— cada um custa ~%3 requisições por turma.\n"
            "Os intervalos têm um mínimo definido pelo app: o SIGAA é o servidor "
            "da universidade, e uma conta que o consulta rápido demais pode ser "
            "bloqueada sem aviso.")
            .arg(porDia)
            .arg(completosPorDia)
            .arg(porTurma));
}

void DialogoOpcoes::procurandoAtualizacao() {
    botaoProcurar_->setEnabled(false);
    botaoInstalar_->setVisible(false);
    estadoAtualizacao_->setText(QStringLiteral("Consultando o GitHub…"));
}

void DialogoOpcoes::mostrarResultadoAtualizacao(const QString& texto, bool haNova) {
    botaoProcurar_->setEnabled(true);
    estadoAtualizacao_->setText(texto);
    // O botão de instalar só APARECE quando há o que instalar. Deixá-lo
    // sempre visível e apagado convida ao clique e não explica nada.
    botaoInstalar_->setVisible(haNova);
    adjustSize();
}

DialogoOpcoes::Config DialogoOpcoes::config() const {
    Config c;
    c.automatico = automatico_->isChecked();
    c.minutosPortal = intervaloPortal_->currentData().toInt();
    c.minutosCompleto = intervaloCompleto_->currentData().toInt();
    c.arquivos = arquivos_->isChecked();
    c.frequencia = frequencia_->isChecked();
    c.baixarMateriais = arquivos_->isChecked() && baixar_->isChecked();
    c.verificarAtualizacao = verificarAtualizacao_->isChecked();
    return c;
}

} // namespace sigaa::ui
