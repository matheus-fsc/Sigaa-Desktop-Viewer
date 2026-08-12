#include "ui/JanelaDiagnostico.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>

#include "ui/Modelos.h"
#include "ui_JanelaDiagnostico.h"

namespace sigaa::ui {
namespace {

// A partir daqui a repetição deixa de ser coincidência. Duas requisições
// iguais seguidas acontecem no fluxo normal (voltar ao portal para renovar o
// ViewState); três é sinal de que alguém está preso.
constexpr int kRepeticoesSuspeitas = 3;

// O app se propõe a uma requisição a cada 1,5 s, mas só quando há trabalho.
// Acima de 30/min alguma coisa está disparando ciclos em cima de ciclos.
constexpr int kRitmoSuspeito = 30;

// Janela de tempo do cálculo de ritmo. Um minuto é o que se lê como "por
// minuto" sem precisar de explicação.
constexpr int kJanelaRitmoSegundos = 60;

QString caminhoDe(const std::string& url) {
    const QString u = QString::fromStdString(url);
    const int barra = u.indexOf(QLatin1Char('/'), u.indexOf(QLatin1String("//")) + 2);
    return barra > 0 ? u.mid(barra) : u;
}

QStandardItem* celula(const QString& texto, const QVariant& ordem) {
    auto* it = new QStandardItem(texto);
    it->setEditable(false);
    it->setData(ordem, PapelOrdenacao);
    return it;
}

} // namespace

JanelaDiagnostico::JanelaDiagnostico(QWidget* pai)
    : QDialog(pai), formulario_(std::make_unique<Ui::JanelaDiagnostico>()) {
    formulario_->setupUi(this);

    modelo_ = new QStandardItemModel(0, 8, this);
    modelo_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Hora"), QStringLiteral("Método"),
         QStringLiteral("Caminho"), QStringLiteral("HTTP"), QStringLiteral("Bytes"),
         QStringLiteral("Tempo"), QStringLiteral("Resposta")});
    formulario_->tvRequisicoes->setModel(modelo_);

    // O CAMINHO é a coluna que se lê: é ela que mostra a repetição. Ela fica
    // com a sobra da largura; as numéricas se ajustam ao conteúdo, e a de
    // resposta ganha largura fixa porque uma mensagem de erro do libcurl tem
    // 120 caracteres e comeria a tela inteira no modo automático (o texto
    // completo fica no tooltip).
    auto* cabecalho = formulario_->tvRequisicoes->horizontalHeader();
    cabecalho->setSectionResizeMode(QHeaderView::ResizeToContents);
    cabecalho->setSectionResizeMode(3, QHeaderView::Stretch);
    cabecalho->setSectionResizeMode(7, QHeaderView::Interactive);
    cabecalho->resizeSection(7, 260);
    cabecalho->setStretchLastSection(false);

    connect(formulario_->botaoCopiar, &QPushButton::clicked, this, &JanelaDiagnostico::copiar);
    connect(formulario_->botaoLimpar, &QPushButton::clicked, this, &JanelaDiagnostico::limpar);

    // QueuedConnection explícita: o sinal é emitido da thread do sync, e o slot
    // toca o modelo. Deixar no Auto daria o mesmo resultado hoje, mas a
    // garantia aqui é do tipo que não pode depender de heurística.
    connect(this, &JanelaDiagnostico::chegou, this, &JanelaDiagnostico::acrescentar,
            Qt::QueuedConnection);

    // O que já passou antes de a janela abrir. Sem isto, quem abre a janela
    // DEPOIS de desconfiar de um laço não vê justamente o laço.
    for (const auto& ev : http::historico()) acrescentar(ev);

    observador_ = http::registrarObservador([this](const http::EventoRequisicao& ev) {
        emit chegou(ev);
    });
}

JanelaDiagnostico::~JanelaDiagnostico() {
    // Antes de qualquer destruição: o observador captura `this` e é chamado de
    // outra thread. Remover no destrutor do Qt (deleteLater) seria tarde.
    http::removerObservador(observador_);
}

void JanelaDiagnostico::acrescentar(const http::EventoRequisicao& ev) {
    const QString caminho = caminhoDe(ev.url);
    const bool falhou = !ev.erro.empty();

    if (caminho == ultimaUrl_) ++repeticoes_;
    else repeticoes_ = 1;
    ultimaUrl_ = caminho;
    if (falhou) ++erros_;

    const QDateTime quando = QDateTime::fromSecsSinceEpoch(
        std::chrono::duration_cast<std::chrono::seconds>(ev.quando.time_since_epoch()).count());

    auto* seq = celula(QString::number(ev.sequencia), qlonglong(ev.sequencia));
    auto* caminhoItem = celula(caminho, caminho);
    if (!ev.urlFinal.empty()) {
        // Redirecionamento importa: é assim que o SIGAA manda para a tela de
        // login sem devolver erro.
        caminhoItem->setToolTip(QStringLiteral("redirecionou para %1")
                                    .arg(QString::fromStdString(ev.urlFinal)));
    }

    auto* http_ = celula(falhou ? QStringLiteral("—") : QString::number(ev.status),
                         qlonglong(ev.status));
    auto* resposta = celula(falhou ? QString::fromStdString(ev.erro)
                                   : QString::fromStdString(ev.pagina),
                            falhou ? 1 : 0);
    // A mensagem do libcurl e longa e a coluna e estreita: o texto inteiro fica
    // no tooltip, senao a unica pista do que aconteceu some no "...".
    if (falhou) resposta->setToolTip(QString::fromStdString(ev.erro));

    QString tempo = QStringLiteral("%1 ms").arg(ev.duracaoMs);
    if (ev.esperaMs > 0) tempo += QStringLiteral(" (+%1 esperando)").arg(ev.esperaMs);
    if (ev.tentativas > 1) {
        resposta->setText(resposta->text() +
                          QStringLiteral(" · %1 tentativas").arg(ev.tentativas));
    }

    if (falhou) {
        for (auto* it : {seq, http_, resposta}) it->setForeground(QBrush(cor::atrasado()));
    } else if (ev.pagina == "Login" || ev.pagina == "SessaoExpirada") {
        // Não é erro de rede, e é pior: o crawl continua, lendo tela de login
        // como se fosse conteúdo. Marcar aqui é o que explica uma coleta vazia.
        resposta->setForeground(QBrush(cor::urgente()));
        resposta->setToolTip(QStringLiteral(
            "O SIGAA devolveu tela de login. Daqui para a frente a coleta lê "
            "página de login em vez de conteúdo — é o que produz um sync vazio "
            "sem mensagem de erro."));
    }
    if (repeticoes_ >= kRepeticoesSuspeitas) {
        caminhoItem->setForeground(QBrush(cor::atrasado()));
        caminhoItem->setToolTip(
            QStringLiteral("%1ª vez seguida nesta mesma URL").arg(repeticoes_));
    }

    modelo_->appendRow({seq, celula(quando.toString(QStringLiteral("HH:mm:ss")),
                                    quando.toMSecsSinceEpoch()),
                        celula(QString::fromStdString(ev.metodo), QString::fromStdString(ev.metodo)),
                        caminhoItem, http_,
                        celula(QString::number(ev.bytes), qlonglong(ev.bytes)),
                        celula(tempo, ev.duracaoMs), resposta});

    if (formulario_->caixaSeguir->isChecked()) formulario_->tvRequisicoes->scrollToBottom();
    atualizarContadores();
}

void JanelaDiagnostico::atualizarContadores() {
    const auto hist = http::historico();

    // Ritmo do último minuto, e não a média desde que o app abriu: a média
    // dilui exatamente o pico que interessa.
    const auto agora = std::chrono::system_clock::now();
    int noMinuto = 0;
    for (const auto& e : hist) {
        if (agora - e.quando <= std::chrono::seconds(kJanelaRitmoSegundos)) ++noMinuto;
    }

    formulario_->valorTotal->setText(
        QStringLiteral("%1 requisições nesta execução").arg(http::totalRequisicoes()));
    formulario_->valorRitmo->setText(QStringLiteral("· %1/min").arg(noMinuto));
    formulario_->valorErros->setText(erros_ > 0 ? QStringLiteral("· %1 com erro").arg(erros_)
                                                : QString());

    QString alerta;
    if (repeticoes_ >= kRepeticoesSuspeitas) {
        alerta = QStringLiteral(
                     "⚠ %1 requisições seguidas para o mesmo endereço (%2). Isto tem "
                     "cara de laço: o SIGAA trata repetição assim como robô, e a conta "
                     "que ele bloqueia é a sua.")
                     .arg(repeticoes_)
                     .arg(ultimaUrl_);
    } else if (noMinuto > kRitmoSuspeito) {
        alerta = QStringLiteral(
                     "⚠ %1 requisições no último minuto. O app se propõe a uma a cada "
                     "1,5 s e só quando há trabalho — este ritmo indica ciclos "
                     "disparando em cima de ciclos.")
                     .arg(noMinuto);
    }
    formulario_->rotuloAlerta->setText(alerta);
    formulario_->rotuloAlerta->setVisible(!alerta.isEmpty());
}

void JanelaDiagnostico::copiar() {
    QString texto;
    for (const auto& e : http::historico()) {
        texto += QString::fromStdString(e.linha()) + QLatin1Char('\n');
    }
    QApplication::clipboard()->setText(texto);
}

void JanelaDiagnostico::limpar() {
    http::limparHistorico();
    modelo_->removeRows(0, modelo_->rowCount());
    erros_ = 0;
    repeticoes_ = 0;
    ultimaUrl_.clear();
    atualizarContadores();
}

} // namespace sigaa::ui
