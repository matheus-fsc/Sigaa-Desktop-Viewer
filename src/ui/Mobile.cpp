#include "ui/Mobile.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#ifdef SIGAA_QRCODE
#include <qrcodegen.hpp>
#endif

#include "core/store/Database.h"
#include "ui/Tema.h"
#include "web/Pareamento.h"
#include "web/Servidor.h"

namespace sigaa::ui {
namespace {

constexpr int kPortaPadrao = 8765;
constexpr int kMaxAcessos = 50;

const QString kAutomatico = QStringLiteral("web/automatico");
const QString kInterface = QStringLiteral("web/interface");
const QString kEndereco = QStringLiteral("web/endereco");
const QString kPorta = QStringLiteral("web/porta");

QLabel* nota(const QString& texto, QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    l->setWordWrap(true);
    l->setProperty("classe", QStringLiteral("nota"));
    return l;
}

QString q(const std::string& s) { return QString::fromStdString(s); }

QString haQuanto(std::int64_t segundos) {
    if (segundos < 60) return QStringLiteral("agora");
    const auto min = segundos / 60;
    if (min < 60) return QStringLiteral("há %1 min").arg(min);
    const auto h = min / 60;
    if (h < 24) return QStringLiteral("há %1 h").arg(h);
    const auto d = h / 24;
    return d == 1 ? QStringLiteral("ontem") : QStringLiteral("há %1 dias").arg(d);
}

// Que tipo de rede é esta interface. Decide o rótulo na lista e o aviso
// embaixo dela — é a informação que o aluno precisa para escolher certo.
enum class Tipo { Vpn, Local, SoEste };

bool pareceVpn(const QNetworkInterface& i, const QHostAddress& ip) {
    // 100.64.0.0/10 é o espaço de CGNAT que o Tailscale usa para a tailnet.
    if (ip.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10)) return true;
    static const QRegularExpression nomes(
        QStringLiteral("^(tailscale|ts\\d|wg|tun|tap|utun|zt|ppp|nordlynx|proton|mullvad|"
                       "openvpn|wireguard|zerotier|hamachi|netbird|nebula)"),
        QRegularExpression::CaseInsensitiveOption);
    return nomes.match(i.name()).hasMatch() || nomes.match(i.humanReadableName()).hasMatch() ||
           i.type() == QNetworkInterface::Virtual;
}

struct Opcao {
    QString interface;   // nome estável ("tailscale0"); vazio = loopback
    QString ip;
    Tipo tipo;
};

std::vector<Opcao> opcoesDeEndereco() {
    std::vector<Opcao> v;
    for (const auto& i : QNetworkInterface::allInterfaces()) {
        const auto f = i.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning)) continue;
        if (f & QNetworkInterface::IsLoopBack) continue;
        for (const auto& e : i.addressEntries()) {
            const QHostAddress ip = e.ip();
            // Só IPv4: é o que toda VPN entrega e o que se digita num celular.
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) continue;
            v.push_back({i.name(), ip.toString(), pareceVpn(i, ip) ? Tipo::Vpn : Tipo::Local});
        }
    }
    // VPN primeiro: é a escolha certa quase sempre, e a lista abre nela.
    std::stable_sort(v.begin(), v.end(), [](const Opcao& a, const Opcao& b) {
        return a.tipo == Tipo::Vpn && b.tipo != Tipo::Vpn;
    });
    v.push_back({QString(), QStringLiteral("127.0.0.1"), Tipo::SoEste});
    return v;
}

QString rotulo(const Opcao& o) {
    switch (o.tipo) {
        case Tipo::Vpn:    return QStringLiteral("%1 · %2 — VPN").arg(o.interface, o.ip);
        case Tipo::Local:  return QStringLiteral("%1 · %2 — rede local").arg(o.interface, o.ip);
        case Tipo::SoEste: return QStringLiteral("127.0.0.1 — só este computador");
    }
    return o.ip;
}

QString explicacao(Tipo t) {
    switch (t) {
        case Tipo::Vpn:
            return QStringLiteral("Só quem está na sua VPN alcança este endereço, e ainda "
                                  "precisa do código de pareamento.");
        case Tipo::Local:
            return QStringLiteral("Atenção: qualquer pessoa conectada a esta rede (o Wi-Fi da "
                                  "faculdade, por exemplo) alcança a porta. O código de "
                                  "pareamento continua sendo exigido, mas prefira a VPN.");
        case Tipo::SoEste:
            return QStringLiteral("Só este computador. Use com `tailscale serve`, Caddy ou outro "
                                  "proxy que leve o acesso até o celular com HTTPS.");
    }
    return {};
}

#ifdef SIGAA_QRCODE
QPixmap desenharQr(const QString& texto, int lado) {
    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(texto.toUtf8().constData(), QrCode::Ecc::MEDIUM);
    const int borda = 4;
    const int n = qr.getSize() + 2 * borda;
    // Preto no branco SEMPRE, também no tema escuro: leitor de QR de câmera
    // barata não entende o código invertido.
    QImage img(n, n, QImage::Format_RGB32);
    img.fill(Qt::white);
    for (int y = 0; y < qr.getSize(); ++y) {
        for (int x = 0; x < qr.getSize(); ++x) {
            if (qr.getModule(x, y)) img.setPixel(x + borda, y + borda, qRgb(0, 0, 0));
        }
    }
    return QPixmap::fromImage(img.scaled(lado, lado, Qt::KeepAspectRatio, Qt::FastTransformation));
}
#endif

} // namespace

PainelMobile::PainelMobile(QString banco, QString materiais, QWidget* pai)
    : QWidget(pai), banco_(std::move(banco)), materiais_(std::move(materiais)) {
    auto* externo = new QVBoxLayout(this);
    externo->setContentsMargins(0, 0, 0, 0);

    auto* conteudo = new QWidget;
    auto* raiz = new QVBoxLayout(conteudo);
    raiz->setContentsMargins(tema::esp(6), tema::esp(5), tema::esp(6), tema::esp(5));
    raiz->setSpacing(tema::esp(4));

    auto* rolagem = new QScrollArea(this);
    rolagem->setWidget(conteudo);
    rolagem->setWidgetResizable(true);
    rolagem->setFrameShape(QFrame::NoFrame);
    externo->addWidget(rolagem);

    auto* titulo = new QLabel(QStringLiteral("Acesso mobile"), conteudo);
    QFont ft = tema::fonte(tema::Papel::Titulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);
    raiz->addWidget(nota(
        QStringLiteral("Veja agenda, provas, turmas e materiais no celular, pelo navegador. Este "
                       "computador serve as telas pela sua VPN: nada vai para a internet aberta, "
                       "a senha do SIGAA não sai daqui e o celular só lê — atualizar do SIGAA "
                       "continua sendo pelo app."),
        conteudo));

    // --- servidor ----------------------------------------------------------
    auto* caixa = new QGroupBox(QStringLiteral("Servidor"), conteudo);
    auto* lv = new QVBoxLayout(caixa);
    lv->setSpacing(tema::esp(3));

    auto* linha = new QHBoxLayout;
    estado_ = new QLabel(caixa);
    estado_->setWordWrap(true);
    estado_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    botaoLigar_ = new QPushButton(caixa);
    botaoLigar_->setMinimumWidth(tema::esp(30));
    linha->addWidget(estado_, 1);
    linha->addWidget(botaoLigar_);
    lv->addLayout(linha);

    automatico_ = new QCheckBox(QStringLiteral("Ligar sozinho sempre que o app abrir"), caixa);
    automatico_->setToolTip(
        QStringLiteral("Se a VPN ainda não estiver conectada quando o app abrir, ele tenta de "
                       "novo a cada 30 segundos."));
    lv->addWidget(automatico_);

    auto* form = new QFormLayout;
    form->setSpacing(tema::esp(2));
    auto* linhaEnd = new QHBoxLayout;
    endereco_ = new QComboBox(caixa);
    endereco_->setMinimumHeight(endereco_->fontMetrics().height() + tema::esp(4));
    auto* botaoListar = new QPushButton(QStringLiteral("Procurar de novo"), caixa);
    botaoListar->setProperty("papel", QStringLiteral("discreto"));
    botaoListar->setToolTip(QStringLiteral("Acabou de conectar a VPN? Clique para ela aparecer."));
    linhaEnd->addWidget(endereco_, 1);
    linhaEnd->addWidget(botaoListar);
    form->addRow(QStringLiteral("Endereço"), linhaEnd);
    porta_ = new QSpinBox(caixa);
    porta_->setRange(1024, 65535);
    porta_->setMinimumHeight(porta_->fontMetrics().height() + tema::esp(4));
    porta_->setMaximumWidth(porta_->fontMetrics().horizontalAdvance(QStringLiteral("000000")) + tema::esp(12));
    form->addRow(QStringLiteral("Porta"), porta_);
    lv->addLayout(form);
    sobreEndereco_ = nota(QString(), caixa);
    lv->addWidget(sobreEndereco_);
    raiz->addWidget(caixa);

    // --- pareamento ----------------------------------------------------------
    auto* par = new QGroupBox(QStringLiteral("Parear o celular"), conteudo);
    auto* lp = new QHBoxLayout(par);
    lp->setSpacing(tema::esp(5));
    qr_ = new QLabel(par);
    qr_->setFixedSize(220, 220);
    qr_->setAlignment(Qt::AlignCenter);
    qr_->setWordWrap(true);
    qr_->setProperty("classe", QStringLiteral("recado"));
    lp->addWidget(qr_, 0, Qt::AlignTop);

    auto* lado = new QVBoxLayout;
    lado->setSpacing(tema::esp(2));
    lado->addWidget(nota(
        QStringLiteral("Com o celular na mesma VPN, aponte a câmera para o QR code. Ou abra o "
                       "endereço abaixo no navegador e digite o seu PIN."),
        par));

    // O caminho curto: o endereço que se digita, e o PIN que o aluno escolheu.
    enderecoCurto_ = new QLabel(par);
    enderecoCurto_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont fe = tema::fonte(tema::Papel::Subtitulo);
    fe.setWeight(QFont::Bold);
    enderecoCurto_->setFont(fe);
    lado->addWidget(enderecoCurto_);

    auto* linhaPin = new QHBoxLayout;
    pin_ = new QLineEdit(par);
    pin_->setEchoMode(QLineEdit::Password);
    pin_->setMaxLength(web::kPinMaximo);
    pin_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,%1}").arg(web::kPinMaximo)), pin_));
    pin_->setPlaceholderText(QStringLiteral("Novo PIN (%1 a %2 números)")
                                 .arg(web::kPinMinimo)
                                 .arg(web::kPinMaximo));
    pin_->setMaximumWidth(tema::esp(60));
    botaoPin_ = new QPushButton(QStringLiteral("Salvar PIN"), par);
    botaoRemoverPin_ = new QPushButton(QStringLiteral("Remover PIN"), par);
    botaoRemoverPin_->setProperty("papel", QStringLiteral("discreto"));
    linhaPin->addWidget(pin_);
    linhaPin->addWidget(botaoPin_);
    linhaPin->addWidget(botaoRemoverPin_);
    linhaPin->addStretch();
    lado->addLayout(linhaPin);
    estadoPin_ = nota(QString(), par);
    lado->addWidget(estadoPin_);

    link_ = new QLineEdit(par);
    link_->setReadOnly(true);
    link_->setPlaceholderText(QStringLiteral("Ligue o servidor para gerar o link"));
    lado->addWidget(link_);
    auto* botoes = new QHBoxLayout;
    botaoCopiar_ = new QPushButton(QStringLiteral("Copiar link"), par);
    botaoNovo_ = new QPushButton(QStringLiteral("Trocar QR code"), par);
    botaoNovo_->setProperty("papel", QStringLiteral("discreto"));
    botaoNovo_->setToolTip(QStringLiteral(
        "Mostrou o QR para alguém? O código atual deixa de parear na hora. Os aparelhos já "
        "pareados continuam; para tirar um deles, use Desconectar na lista abaixo."));
    botoes->addWidget(botaoCopiar_);
    botoes->addWidget(botaoNovo_);
    botoes->addStretch();
    lado->addLayout(botoes);
    lado->addWidget(nota(
        QStringLiteral("O link e o PIN só servem para parear: cada celular recebe um acesso "
                       "próprio, que aparece em Aparelhos e pode ser desconectado sozinho. No "
                       "navegador, “Adicionar à tela inicial” abre como app."),
        par));
    lado->addStretch();
    lp->addLayout(lado, 1);
    raiz->addWidget(par);

    // --- aparelhos ---------------------------------------------------------------
    auto* aps = new QGroupBox(QStringLiteral("Aparelhos"), conteudo);
    auto* la = new QVBoxLayout(aps);
    la->setSpacing(tema::esp(2));
    resumoAparelhos_ = nota(QString(), aps);
    la->addWidget(resumoAparelhos_);
    aparelhos_ = new QTableWidget(0, 5, aps);
    aparelhos_->setHorizontalHeaderLabels({QStringLiteral("Aparelho"), QStringLiteral("Pareado"),
                                           QStringLiteral("Último acesso"), QStringLiteral("De"),
                                           QString()});
    aparelhos_->verticalHeader()->hide();
    aparelhos_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aparelhos_->setSelectionMode(QAbstractItemView::NoSelection);
    aparelhos_->setShowGrid(false);
    aparelhos_->horizontalHeader()->setStretchLastSection(false);
    aparelhos_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) {
        aparelhos_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    aparelhos_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    aparelhos_->setMouseTracking(true);
    connect(aparelhos_, &QTableWidget::cellClicked, this, [this](int linha, int coluna) {
        if (coluna != 4) return;
        if (auto* it = aparelhos_->item(linha, 4)) desconectar(it->data(Qt::UserRole).toLongLong());
    });
    connect(aparelhos_, &QTableWidget::cellEntered, this, [this](int, int coluna) {
        aparelhos_->viewport()->setCursor(coluna == 4 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    aparelhos_->setMinimumHeight(tema::esp(36));
    tema::ajustarLista(aparelhos_);
    la->addWidget(aparelhos_);
    auto* linhaAps = new QHBoxLayout;
    botaoTodos_ = new QPushButton(QStringLiteral("Desconectar todos"), aps);
    botaoTodos_->setProperty("papel", QStringLiteral("perigo"));
    linhaAps->addStretch();
    linhaAps->addWidget(botaoTodos_);
    la->addLayout(linhaAps);
    raiz->addWidget(aps);

    // --- instruções ------------------------------------------------------------
    auto* como = new QGroupBox(QStringLiteral("Como chegar até o celular"), conteudo);
    auto* lc = new QVBoxLayout(como);
    auto* texto = new QLabel(como);
    texto->setWordWrap(true);
    texto->setTextFormat(Qt::RichText);
    texto->setOpenExternalLinks(true);
    texto->setText(QStringLiteral(
        "<p><b>Tailscale</b> (o mais simples): instale no computador e no celular, entre com a "
        "mesma conta e escolha <i>tailscale0</i> (ou <i>Tailscale</i>) em Endereço. Funciona "
        "fora de casa, sem abrir porta no roteador.</p>"
        "<p><b>Quer HTTPS e instalar como app?</b> Escolha <i>127.0.0.1</i> e rode "
        "<code>tailscale serve --bg %1</code>. Abra pelo endereço <i>https://…ts.net</i> que ele "
        "mostrar, com o mesmo <code>#t=…</code> do link.</p>"
        "<p><b>WireGuard, OpenVPN, ZeroTier…</b>: escolha a interface da VPN (<i>wg0</i>, "
        "<i>tun0</i>, <i>zt…</i>). O celular precisa estar conectado à mesma VPN.</p>"
        "<p><b>Só em casa, sem VPN</b>: dá para usar a rede local, mas aí qualquer um no mesmo "
        "Wi-Fi alcança a porta. Não use em rede pública.</p>"
        "<p>O computador precisa estar ligado e com o app aberto (pode ficar na bandeja). Sem "
        "janela, use <code>sigaa-cli web --escutar &lt;ip-da-vpn&gt;</code>.</p>")
                       .arg(kPortaPadrao));
    lc->addWidget(texto);
    raiz->addWidget(como);

    // --- acessos -----------------------------------------------------------------
    auto* reg = new QGroupBox(QStringLiteral("Últimos acessos"), conteudo);
    auto* lr = new QVBoxLayout(reg);
    lr->addWidget(nota(QStringLiteral("Desde que o servidor ligou. “Recusado” é um pedido de "
                                      "quem não está pareado, ou um PIN errado. Se aparecer e não "
                                      "foi você, troque o PIN e o QR code."),
                       reg));
    acessos_ = new QTableWidget(0, 4, reg);
    acessos_->setHorizontalHeaderLabels(
        {QStringLiteral("Hora"), QStringLiteral("De"), QStringLiteral("Pedido"), QStringLiteral("Resultado")});
    acessos_->verticalHeader()->hide();
    acessos_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    acessos_->setSelectionMode(QAbstractItemView::NoSelection);
    acessos_->setShowGrid(false);
    acessos_->horizontalHeader()->setStretchLastSection(true);
    acessos_->setMinimumHeight(tema::esp(50));
    tema::ajustarLista(acessos_);
    lr->addWidget(acessos_);
    raiz->addWidget(reg);
    raiz->addStretch();

    // --- estado inicial e ligações ---------------------------------------------
    QSettings cfg;
    automatico_->setChecked(cfg.value(kAutomatico, false).toBool());
    porta_->setValue(cfg.value(kPorta, kPortaPadrao).toInt());
    listarEnderecos();

    reTentar_ = new QTimer(this);
    reTentar_->setInterval(30 * 1000);
    connect(reTentar_, &QTimer::timeout, this, [this] {
        listarEnderecos();
        ligar(/*silencioso=*/true);
    });

    connect(botaoLigar_, &QPushButton::clicked, this, [this] {
        if (noAr()) desligar();
        else ligar();
    });
    connect(automatico_, &QCheckBox::toggled, this, [this] { salvar(); });
    connect(botaoListar, &QPushButton::clicked, this, [this] { listarEnderecos(); });
    connect(endereco_, &QComboBox::currentIndexChanged, this, [this] {
        sobreEndereco_->setText(explicacao(static_cast<Tipo>(endereco_->currentData(Qt::UserRole + 2).toInt())));
    });
    connect(botaoCopiar_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(link_->text());
        botaoCopiar_->setText(QStringLiteral("Copiado"));
        QTimer::singleShot(2000, this, [this] { botaoCopiar_->setText(QStringLiteral("Copiar link")); });
    });
    connect(botaoNovo_, &QPushButton::clicked, this, [this] { gerarNovoCodigo(); });
    connect(botaoPin_, &QPushButton::clicked, this, [this] { salvarPin(); });
    connect(pin_, &QLineEdit::returnPressed, this, [this] { salvarPin(); });
    connect(botaoRemoverPin_, &QPushButton::clicked, this, [this] {
        store::Database db(banco_.toStdString());
        if (db.aberto() && db.migrar()) web::removerPin(db);
        mostrarPin(QStringLiteral("PIN removido. Só o QR code pareia agora."));
    });
    connect(botaoTodos_, &QPushButton::clicked, this, [this] { desconectar(0); });

    // "Último acesso" anda sozinho: o servidor grava, a lista relê.
    relogioAparelhos_ = new QTimer(this);
    relogioAparelhos_->setInterval(15 * 1000);
    connect(relogioAparelhos_, &QTimer::timeout, this, [this] { listarAparelhos(); });

    sobreEndereco_->setText(explicacao(static_cast<Tipo>(endereco_->currentData(Qt::UserRole + 2).toInt())));
    atualizarEstado();
    mostrarPin();
    listarAparelhos();
}

void PainelMobile::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    listarAparelhos();
    relogioAparelhos_->start();
}

void PainelMobile::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    relogioAparelhos_->stop();
}

void PainelMobile::salvarPin() {
    const std::string pin = pin_->text().toStdString();
    if (!web::pinValido(pin)) {
        mostrarPin(QStringLiteral("O PIN precisa ter de %1 a %2 números.")
                       .arg(web::kPinMinimo)
                       .arg(web::kPinMaximo),
                   /*erro=*/true);
        return;
    }
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() || !web::definirPin(db, pin)) {
        mostrarPin(QStringLiteral("Não consegui gravar o PIN no banco."), true);
        return;
    }
    pin_->clear();
    mostrarPin(QStringLiteral("PIN salvo. Os aparelhos já pareados continuam conectados."));
}

void PainelMobile::mostrarPin(const QString& recado, bool erro) {
    bool tem = false;
    {
        store::Database db(banco_.toStdString());
        tem = db.aberto() && web::temPin(db);
    }
    botaoRemoverPin_->setVisible(tem);
    botaoPin_->setText(tem ? QStringLiteral("Trocar PIN") : QStringLiteral("Salvar PIN"));
    QString t = recado;
    if (t.isEmpty()) {
        t = tem ? QStringLiteral("PIN definido. Ele não aparece aqui de novo; se esquecer, troque.")
                : QStringLiteral("Sem PIN: só o QR code pareia. Crie um para digitar no celular.");
    }
    estadoPin_->setText(t);
    estadoPin_->setStyleSheet(erro ? QStringLiteral("color: %1").arg(tema::cor::atrasado().name())
                                   : QString());
}

void PainelMobile::listarAparelhos() {
    std::vector<store::Database::DispositivoWeb> lista;
    {
        store::Database db(banco_.toStdString());
        if (db.aberto() && db.migrar()) lista = db.dispositivosWeb();
    }
    const auto agora = QDateTime::currentSecsSinceEpoch();
    int ativos = 0;
    aparelhos_->setRowCount(static_cast<int>(lista.size()));
    for (int i = 0; i < static_cast<int>(lista.size()); ++i) {
        const auto& d = lista[static_cast<size_t>(i)];
        // "Conectado" num servidor sem conexão permanente é "usou há pouco":
        // a página relê os dados ao voltar para a tela, então dois minutos
        // sem pedido já querem dizer que o celular está em outra coisa.
        const bool ativo = noAr() && agora - d.ultimoAcesso < 120;
        if (ativo) ++ativos;
        auto* nome = new QTableWidgetItem((ativo ? QStringLiteral("● ") : QString()) + q(d.nome));
        if (ativo) nome->setForeground(tema::cor::sucesso());
        nome->setToolTip(ativo ? QStringLiteral("Usou nos últimos 2 minutos") : QString());
        aparelhos_->setItem(i, 0, nome);
        aparelhos_->setItem(
            i, 1,
            new QTableWidgetItem(QStringLiteral("%1 · %2")
                                     .arg(QDateTime::fromSecsSinceEpoch(d.criadoEm)
                                              .toString(QStringLiteral("dd/MM HH:mm")),
                                          d.via == "pin" ? QStringLiteral("PIN")
                                                         : QStringLiteral("QR code"))));
        aparelhos_->setItem(i, 2, new QTableWidgetItem(haQuanto(agora - d.ultimoAcesso)));
        aparelhos_->setItem(i, 3, new QTableWidgetItem(q(d.ultimoIp)));
        // Texto clicável, e não um QPushButton na célula: o padding da folha
        // de estilo não cabe na altura de uma linha de tabela e cortava o rótulo.
        auto* sair = new QTableWidgetItem(QStringLiteral("Desconectar"));
        sair->setForeground(tema::cor::atrasado());
        sair->setData(Qt::UserRole, static_cast<qlonglong>(d.id));
        sair->setToolTip(QStringLiteral("Tira o acesso deste aparelho agora"));
        aparelhos_->setItem(i, 4, sair);
    }
    botaoTodos_->setVisible(lista.size() > 1);
    if (lista.empty()) {
        resumoAparelhos_->setText(QStringLiteral("Nenhum aparelho pareado ainda."));
    } else {
        resumoAparelhos_->setText(
            QStringLiteral("%1 aparelho(s) pareado(s)%2. Desconectar tira o acesso na hora; o "
                           "celular volta para a tela de pareamento.")
                .arg(lista.size())
                .arg(ativos ? QStringLiteral(", %1 usando agora").arg(ativos) : QString()));
    }
}

void PainelMobile::desconectar(std::int64_t id) {
    if (id == 0) {
        const auto r = QMessageBox::question(
            this, QStringLiteral("Desconectar todos"),
            QStringLiteral("Todos os aparelhos perdem o acesso agora e precisam parear de novo. "
                           "Continuar?"));
        if (r != QMessageBox::Yes) return;
    }
    store::Database db(banco_.toStdString());
    if (db.aberto() && db.migrar()) db.removerDispositivosWeb(id);
    listarAparelhos();
}

PainelMobile::~PainelMobile() {
    // Antes dos widgets: o registro de acessos chama de volta este painel.
    if (servidor_) servidor_->parar();
}

bool PainelMobile::noAr() const { return servidor_ && servidor_->rodando(); }

void PainelMobile::listarEnderecos() {
    const QString antes = endereco_->count() ? interfaceEscolhida() : QSettings().value(kInterface).toString();
    const QString ipAntes = endereco_->count() ? enderecoEscolhido() : QSettings().value(kEndereco).toString();
    endereco_->blockSignals(true);
    endereco_->clear();
    int escolher = -1;
    for (const Opcao& o : opcoesDeEndereco()) {
        endereco_->addItem(rotulo(o), o.ip);
        const int i = endereco_->count() - 1;
        endereco_->setItemData(i, o.interface, Qt::UserRole + 1);
        endereco_->setItemData(i, static_cast<int>(o.tipo), Qt::UserRole + 2);
        // A escolha é pela INTERFACE, não pelo IP: o IP do Wi-Fi muda de rede
        // para rede, e "tailscale0" continua sendo a VPN.
        if (escolher < 0 && !antes.isEmpty() && o.interface == antes) escolher = i;
        if (escolher < 0 && antes.isEmpty() && !ipAntes.isEmpty() && o.ip == ipAntes) escolher = i;
    }
    endereco_->setCurrentIndex(escolher >= 0 ? escolher : 0);
    endereco_->blockSignals(false);
    sobreEndereco_->setText(explicacao(static_cast<Tipo>(endereco_->currentData(Qt::UserRole + 2).toInt())));
}

QString PainelMobile::enderecoEscolhido() const { return endereco_->currentData().toString(); }

QString PainelMobile::interfaceEscolhida() const {
    return endereco_->currentData(Qt::UserRole + 1).toString();
}

void PainelMobile::salvar() {
    QSettings cfg;
    cfg.setValue(kAutomatico, automatico_->isChecked());
    cfg.setValue(kInterface, interfaceEscolhida());
    cfg.setValue(kEndereco, enderecoEscolhido());
    cfg.setValue(kPorta, porta_->value());
}

void PainelMobile::ligarSeAutomatico() {
    if (automatico_->isChecked()) ligar(/*silencioso=*/true);
}

void PainelMobile::ligar(bool silencioso) {
    if (noAr()) return;

    // A interface lembrada sumiu (VPN desconectada): no automático, esperar
    // ela voltar em vez de cair em outra rede sem o aluno ter escolhido.
    const QString lembrada = QSettings().value(kInterface).toString();
    if (silencioso && !lembrada.isEmpty() && interfaceEscolhida() != lembrada) {
        reTentar_->start();
        atualizarEstado(QStringLiteral("Esperando a interface %1 (a VPN está conectada?). "
                                       "Tento de novo a cada 30 segundos.")
                            .arg(lembrada));
        return;
    }

    std::string token;
    {
        store::Database db(banco_.toStdString());
        if (!db.aberto() || !db.migrar()) {
            atualizarEstado(QStringLiteral("Não consegui abrir o banco do app."));
            return;
        }
        token = web::tokenOuNovo(db);
    }
    if (token.empty()) {
        atualizarEstado(QStringLiteral("Não consegui gravar o código de pareamento."));
        return;
    }

    web::Config c;
    c.host = enderecoEscolhido().toStdString();
    c.porta = porta_->value();
    c.fonte.banco = banco_.toStdString();
    c.fonte.materiais = materiais_.toStdString();
    auto s = std::make_unique<web::Servidor>(c);

    // Da thread do servidor para a da janela. O QPointer descarta o pedido
    // que chegar depois de o painel ter sido destruído.
    QPointer<PainelMobile> eu(this);
    s->aoAcessar = [eu](const web::Acesso& a) {
        QMetaObject::invokeMethod(
            qApp, [eu, a] { if (eu) eu->registrarAcesso(a); }, Qt::QueuedConnection);
    };

    std::string erro;
    if (!s->iniciar(&erro)) {
        if (silencioso) reTentar_->start();
        atualizarEstado(q(erro) + (silencioso ? QStringLiteral(" Tento de novo a cada 30 segundos.")
                                              : QString()));
        return;
    }
    reTentar_->stop();
    servidor_ = std::move(s);
    salvar();
    acessos_->setRowCount(0);
    atualizarEstado();
}

void PainelMobile::desligar() {
    reTentar_->stop();
    if (servidor_) servidor_->parar();
    servidor_.reset();
    atualizarEstado();
}

void PainelMobile::atualizarEstado(const QString& recado) {
    const bool ar = noAr();
    botaoLigar_->setText(ar ? QStringLiteral("Desligar") : QStringLiteral("Ligar servidor"));
    endereco_->setEnabled(!ar);
    porta_->setEnabled(!ar);
    if (ar) {
        const auto& c = servidor_->config();
        estado_->setText(QStringLiteral("<b style='color:%1'>● No ar</b> em %2:%3")
                             .arg(tema::cor::sucesso().name(), q(c.host))
                             .arg(c.porta));
    } else if (!recado.isEmpty()) {
        estado_->setText(QStringLiteral("<b style='color:%1'>● Desligado.</b> %2")
                             .arg(tema::cor::urgente().name(), recado.toHtmlEscaped()));
    } else {
        estado_->setText(QStringLiteral("<b>● Desligado.</b> Nenhuma porta aberta."));
    }
    mostrarPareamento();
    if (aparelhos_) listarAparelhos();   // "usando agora" depende de estar no ar
    if (aoMudar) aoMudar(ar);
}

void PainelMobile::mostrarPareamento() {
    const bool ar = noAr();
    botaoCopiar_->setEnabled(ar);
    if (!ar) {
        enderecoCurto_->setText(QStringLiteral("Ligue o servidor para ver o endereço."));
        link_->clear();
        qr_->setPixmap(QPixmap());
        qr_->setText(QStringLiteral("Ligue o servidor para ver o QR code."));
        return;
    }
    std::string token;
    {
        store::Database db(banco_.toStdString());
        token = web::tokenAtual(db);
    }
    const auto& c = servidor_->config();
    const QString url = q(web::urlDePareamento(c.host, c.porta, token));
    // Sem o "http://": é o que se digita, e todo navegador de celular completa.
    enderecoCurto_->setText(QStringLiteral("%1:%2").arg(q(c.host)).arg(c.porta));
    link_->setText(url);
    link_->setCursorPosition(0);
#ifdef SIGAA_QRCODE
    qr_->setText(QString());
    qr_->setPixmap(desenharQr(url, qr_->width() - 8));
#else
    qr_->setText(QStringLiteral("Esta versão foi compilada sem QR code. Copie o link ao lado e "
                                "abra no celular."));
#endif
}

void PainelMobile::gerarNovoCodigo() {
    // Sem confirmação: trocar o QR não tira ninguém. Quem está pareado tem
    // acesso próprio; o código só deixa de servir para parear um aparelho novo.
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() || web::novoToken(db).empty()) {
        QMessageBox::warning(this, QStringLiteral("Trocar QR code"),
                             QStringLiteral("Não consegui gravar o código novo no banco."));
        return;
    }
    mostrarPareamento();
}

void PainelMobile::registrarAcesso(const web::Acesso& a) {
    // Página estática não interessa a ninguém: só os pedidos de dados.
    if (a.caminho.rfind("/api/", 0) != 0) return;
    // Um aparelho novo, ou um que reapareceu: a lista muda.
    if (a.caminho == "/api/parear" || a.status == 200) listarAparelhos();
    acessos_->insertRow(0);
    QString resultado;
    if (a.caminho == "/api/parear") {
        resultado = a.status == 200   ? QStringLiteral("Pareou: %1").arg(q(a.aparelho))
                    : a.status == 429 ? QStringLiteral("PIN bloqueado (tentativas demais)")
                                      : QStringLiteral("Pareamento recusado (PIN ou código errado)");
    } else if (a.status == 401) resultado = QStringLiteral("Recusado (aparelho não pareado)");
    else if (a.status == 429) resultado = QStringLiteral("Bloqueado (tentativas demais)");
    else if (a.status >= 400) resultado = QStringLiteral("Erro %1").arg(a.status);
    else resultado = QStringLiteral("OK");
    const QStringList cel{QDateTime::fromSecsSinceEpoch(a.quando).toString(QStringLiteral("HH:mm:ss")),
                          a.aparelho.empty() ? q(a.ip) : QStringLiteral("%1 (%2)").arg(q(a.aparelho), q(a.ip)),
                          q(a.caminho), resultado};
    for (int i = 0; i < cel.size(); ++i) {
        auto* it = new QTableWidgetItem(cel[i]);
        if (a.status == 401 || a.status == 429 || a.status == 400) {
            it->setForeground(tema::cor::atrasado());
        }
        acessos_->setItem(0, i, it);
    }
    if (acessos_->rowCount() > kMaxAcessos) acessos_->setRowCount(kMaxAcessos);
}

} // namespace sigaa::ui
