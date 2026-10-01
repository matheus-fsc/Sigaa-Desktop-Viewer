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
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

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
        QStringLiteral("1. Conecte o celular à mesma VPN deste computador.\n"
                       "2. Aponte a câmera para o QR code — ou copie o link e abra no celular.\n"
                       "3. No navegador, use “Adicionar à tela inicial” para abrir como app."),
        par));
    link_ = new QLineEdit(par);
    link_->setReadOnly(true);
    link_->setPlaceholderText(QStringLiteral("Ligue o servidor para gerar o link"));
    lado->addWidget(link_);
    auto* botoes = new QHBoxLayout;
    botaoCopiar_ = new QPushButton(QStringLiteral("Copiar link"), par);
    botaoNovo_ = new QPushButton(QStringLiteral("Gerar novo código"), par);
    botaoNovo_->setProperty("papel", QStringLiteral("perigo"));
    botaoNovo_->setToolTip(QStringLiteral(
        "Perdeu o celular ou mostrou o QR para alguém? O código atual para de valer na hora, "
        "e cada celular precisa parear de novo."));
    botoes->addWidget(botaoCopiar_);
    botoes->addWidget(botaoNovo_);
    botoes->addStretch();
    lado->addLayout(botoes);
    lado->addWidget(nota(
        QStringLiteral("O link carrega o código de acesso: trate como uma senha. Ele vai depois "
                       "do “#”, parte que o navegador nunca envia a servidor nenhum."),
        par));
    lado->addStretch();
    lp->addLayout(lado, 1);
    raiz->addWidget(par);

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
    lr->addWidget(nota(QStringLiteral("Desde que o servidor ligou. “Recusado” é um pedido sem "
                                      "o código certo — se aparecer e não foi você, gere um "
                                      "código novo."),
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

    sobreEndereco_->setText(explicacao(static_cast<Tipo>(endereco_->currentData(Qt::UserRole + 2).toInt())));
    atualizarEstado();
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
    if (aoMudar) aoMudar(ar);
}

void PainelMobile::mostrarPareamento() {
    const bool ar = noAr();
    botaoCopiar_->setEnabled(ar);
    if (!ar) {
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
    const auto r = QMessageBox::question(
        this, QStringLiteral("Gerar novo código"),
        QStringLiteral("O código atual para de valer agora, e todo celular pareado precisa ler o "
                       "QR code de novo. Continuar?"));
    if (r != QMessageBox::Yes) return;
    store::Database db(banco_.toStdString());
    if (!db.aberto() || !db.migrar() || web::novoToken(db).empty()) {
        QMessageBox::warning(this, QStringLiteral("Gerar novo código"),
                             QStringLiteral("Não consegui gravar o código novo no banco."));
        return;
    }
    mostrarPareamento();
}

void PainelMobile::registrarAcesso(const web::Acesso& a) {
    // Página estática não interessa a ninguém: só os pedidos de dados.
    if (a.caminho.rfind("/api/", 0) != 0) return;
    acessos_->insertRow(0);
    QString resultado;
    if (a.status == 401) resultado = QStringLiteral("Recusado (sem código válido)");
    else if (a.status == 429) resultado = QStringLiteral("Bloqueado (tentativas demais)");
    else if (a.status >= 400) resultado = QStringLiteral("Erro %1").arg(a.status);
    else resultado = QStringLiteral("OK");
    const QStringList cel{QDateTime::fromSecsSinceEpoch(a.quando).toString(QStringLiteral("HH:mm:ss")),
                          q(a.ip), q(a.caminho), resultado};
    for (int i = 0; i < cel.size(); ++i) {
        auto* it = new QTableWidgetItem(cel[i]);
        if (a.status == 401 || a.status == 429) it->setForeground(tema::cor::atrasado());
        acessos_->setItem(0, i, it);
    }
    if (acessos_->rowCount() > kMaxAcessos) acessos_->setRowCount(kMaxAcessos);
}

} // namespace sigaa::ui
