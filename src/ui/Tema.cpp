#include "ui/Tema.h"

#include <algorithm>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QFile>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTableView>
#include <QTreeView>

namespace sigaa::ui::tema {
namespace {

Modo emVigor = Modo::Claro;

// --------------------------------------------------------------------------
// Tokens
// --------------------------------------------------------------------------
//
// Os mesmos nomes do design system (theme.css da referência de telas), para
// que uma conversa sobre "--line-strong" valha igual no protótipo web e aqui.
// A folha de estilo usa esses nomes como `@line-strong`; `resolverTokens`
// troca cada um pela cor do tema em vigor antes de entregar a folha ao Qt.
//
// Por que tokens e não só `palette(...)`: a QPalette tem uma dúzia de papéis
// fixos, e o desenho pede mais do que isso — três níveis de superfície, dois
// de borda, três de texto, e o "soft" de cada cor de estado. Espremer isso nos
// papéis do Qt obrigava a escolher qual papel mentiria (o `Mid` virava borda,
// o `AlternateBase` virava hover), e o QSS não enxerga `placeholder-text` na
// propriedade `color`. Com tokens, cada cor tem o nome do que ela é.
//
// A regra "nenhuma cor literal no .qss" continua: o que mudou é que agora há
// vocabulário suficiente para cumpri-la.

struct Token {
    const char* nome;
    QColor escuro;
    QColor claro;
};

QColor rgba(int r, int g, int b, double a) {
    QColor c(r, g, b);
    c.setAlphaF(a);
    return c;
}

// Ordem: os nomes que são prefixo de outro (surface / surface-2) não importam
// aqui — a troca casa o nome inteiro, não um prefixo.
const std::vector<Token>& tokens() {
    static const std::vector<Token> t{
        // Escuro: nada de preto puro — #000 com texto branco produz o halo que
        // cansa a vista em leitura longa. Claro: fundo recuado um tom, para
        // que a superfície branca das listas se separe da moldura.
        {"bg", QColor(0x1b, 0x1f, 0x27), QColor(0xf4, 0xf5, 0xf8)},
        {"surface", QColor(0x22, 0x27, 0x33), QColor(0xff, 0xff, 0xff)},
        {"surface-2", QColor(0x2a, 0x30, 0x3d), QColor(0xf0, 0xf2, 0xf6)},
        {"surface-3", QColor(0x34, 0x3b, 0x4a), QColor(0xe4, 0xe8, 0xee)},
        {"line", QColor(0x33, 0x3a, 0x48), QColor(0xe0, 0xe4, 0xea)},
        {"line-strong", QColor(0x46, 0x50, 0x64), QColor(0xc8, 0xce, 0xd8)},
        {"text", QColor(0xe7, 0xea, 0xf0), QColor(0x1a, 0x1f, 0x29)},
        {"text-2", QColor(0xb3, 0xbb, 0xc9), QColor(0x45, 0x4d, 0x5c)},
        {"text-3", QColor(0x8e, 0x97, 0xa8), QColor(0x5d, 0x66, 0x77)},
        {"text-off", QColor(0x5f, 0x68, 0x7a), QColor(0xa3, 0xaa, 0xb6)},
        {"accent", QColor(0x8a, 0xad, 0xf5), QColor(0x2a, 0x5c, 0xc4)},
        {"accent-fill", QColor(0x3d, 0x6f, 0xd6), QColor(0x2f, 0x63, 0xcc)},
        {"accent-fill-hover", QColor(0x4a, 0x7b, 0xe0), QColor(0x28, 0x57, 0xb8)},
        {"on-accent", QColor(0xff, 0xff, 0xff), QColor(0xff, 0xff, 0xff)},
        {"accent-soft", rgba(91, 141, 239, .17), rgba(47, 99, 204, .10)},
        {"warn", QColor(0xec, 0xa8, 0x5f), QColor(0x95, 0x50, 0x06)},
        {"warn-soft", rgba(224, 145, 58, .16), rgba(224, 145, 58, .17)},
        {"danger", QColor(0xf2, 0x8f, 0x8f), QColor(0xb0, 0x30, 0x2d)},
        {"danger-soft", rgba(229, 100, 100, .16), rgba(214, 69, 65, .11)},
        {"ok", QColor(0x72, 0xd0, 0x9a), QColor(0x1b, 0x71, 0x41)},
        {"ok-soft", rgba(95, 197, 138, .15), rgba(40, 160, 90, .13)},
        {"focus", QColor(0x9a, 0xb8, 0xff), QColor(0x2f, 0x63, 0xcc)},
        {"tooltip", QColor(0x34, 0x3b, 0x4a), QColor(0x1a, 0x1f, 0x29)},
        {"on-tooltip", QColor(0xe7, 0xea, 0xf0), QColor(0xff, 0xff, 0xff)},
    };
    return t;
}


// Como o QSS quer a cor. `rgba()` só quando há transparência: é o que deixa os
// "soft" funcionarem sobre qualquer fundo — linha, cartão ou seleção.
QString emQss(const QColor& c) {
    if (c.alpha() == 255) return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

QPalette montarPaleta() {
    QPalette p;

    p.setColor(QPalette::Window, token("bg"));
    p.setColor(QPalette::WindowText, token("text"));
    p.setColor(QPalette::Base, token("surface"));
    p.setColor(QPalette::AlternateBase, token("surface-2"));
    p.setColor(QPalette::Text, token("text"));
    // O cinza de texto secundário do app (esmaecer() e companhia).
    p.setColor(QPalette::PlaceholderText, token("text-3"));
    p.setColor(QPalette::Button, token("surface"));
    p.setColor(QPalette::ButtonText, token("text"));
    p.setColor(QPalette::Mid, token("line"));
    p.setColor(QPalette::Midlight, token("surface-2"));
    p.setColor(QPalette::Dark, token("line-strong"));
    p.setColor(QPalette::Light, token("surface"));
    p.setColor(QPalette::Shadow, token("line"));
    // Highlight é a AÇÃO (botão primário, aba ligada, caixa marcada). A
    // seleção de linha usa o "soft", e sai do QSS — um bloco azul cheio por
    // cima de uma linha de prazos apagava as etiquetas de estado dela.
    p.setColor(QPalette::Highlight, token("accent-fill"));
    p.setColor(QPalette::HighlightedText, token("on-accent"));
    p.setColor(QPalette::Link, token("accent"));
    p.setColor(QPalette::LinkVisited, token("accent"));
    p.setColor(QPalette::ToolTipBase, token("tooltip"));
    p.setColor(QPalette::ToolTipText, token("on-tooltip"));

    // O grupo Disabled é obrigatório, não enfeite: sem ele o Qt deriva o
    // cinza de desabilitado da paleta PADRÃO, não desta — e no tema escuro o
    // resultado é um botão desabilitado com texto quase invisível. Os botões
    // desta janela ficam desabilitados o tempo todo durante o sync.
    for (const auto papel : {QPalette::WindowText, QPalette::Text,
                             QPalette::ButtonText, QPalette::HighlightedText}) {
        p.setColor(QPalette::Disabled, papel, token("text-off"));
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, token("surface-2"));

    return p;
}

// Pequenos desenhos que a folha de estilo referencia por caminho: as setas de
// ordenação e de grupo, e o visto da caixa marcada.
//
// POR QUE EXISTEM: estilizar `QHeaderView::section` passa o cabeçalho inteiro
// para o QStyleSheetStyle, e a partir daí a seta de ordenação só aparece se a
// folha disser qual imagem usar — sem isso ela some, e as tabelas são todas
// ordenáveis. O mesmo vale para `::branch` e `QCheckBox::indicator`. Uma
// imagem fixa no .qrc teria uma cor só, legível em um dos temas; então o SVG é
// escrito aqui, com a cor do token, numa pasta temporária que vive enquanto o
// app viver.
QString gravarTraco(const QString& nome, const QString& pontos, const QColor& cor,
                    int largura, int altura, double espessura) {
    static QTemporaryDir pasta;
    if (!pasta.isValid()) return {};

    const QString svg =
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%2' "
            "viewBox='0 0 %1 %2'><polyline points='%3' fill='none' stroke='%4' "
            "stroke-width='%5' stroke-linecap='round' stroke-linejoin='round'/></svg>")
            .arg(largura)
            .arg(altura)
            .arg(pontos, cor.name(QColor::HexRgb))
            .arg(espessura);

    // Nome com o modo: trocar de tema com o app aberto grava um arquivo NOVO.
    // Reescrever o mesmo caminho não adiantaria — o Qt guarda a imagem em
    // cache pelo caminho e continuaria pintando a seta da cor antiga.
    const QString caminho = pasta.filePath(
        nome + (emVigor == Modo::Escuro ? QStringLiteral("-escuro.svg")
                                        : QStringLiteral("-claro.svg")));
    QFile f(caminho);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(svg.toUtf8());
    return QStringLiteral("url(%1)").arg(caminho);
}

// Troca cada `@nome` da folha pela cor do token, e os desenhos (`@seta-cima`,
// `@visto`...) por `url(caminho)`. Os desenhos vão primeiro: `@seta-baixo`
// não é token de cor e viraria aviso. Um `@nome` que não existe fica como está e vira
// aviso no console — o Qt descartaria a regra em silêncio, e é melhor alguém
// ver o erro de digitação.
QString resolverTokens(QString folha) {
    // Comentários fora antes: eles citam tokens como exemplo ("`@nome`"), e
    // cada citação viraria um aviso falso de token desconhecido.
    static const QRegularExpression comentario(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    folha.remove(comentario);

    const QColor seta = token("text-3");
    // `@seta-baixo-texto` ANTES de `@seta-baixo`: o nome curto é prefixo do
    // longo, e trocado primeiro deixaria um "-texto" solto na folha.
    folha.replace(QStringLiteral("@seta-baixo-texto"),
                  gravarTraco(QStringLiteral("seta-baixo-texto"), QStringLiteral("1,2 5,6 9,2"),
                              token("text"), 10, 8, 1.8));
    folha.replace(QStringLiteral("@seta-cima"),
                  gravarTraco(QStringLiteral("seta-cima"), QStringLiteral("1,6 5,2 9,6"), seta, 10, 8, 1.6));
    folha.replace(QStringLiteral("@seta-baixo"),
                  gravarTraco(QStringLiteral("seta-baixo"), QStringLiteral("1,2 5,6 9,2"), seta, 10, 8, 1.6));
    folha.replace(QStringLiteral("@seta-direita"),
                  gravarTraco(QStringLiteral("seta-direita"), QStringLiteral("3,1 7,5 3,9"), seta, 10, 10, 1.6));
    folha.replace(QStringLiteral("@visto"),
                  gravarTraco(QStringLiteral("visto"), QStringLiteral("3,8.5 6.5,12 13,4.5"),
                              token("on-accent"), 16, 16, 2.0));

    static const QRegularExpression re(QStringLiteral("@([a-z0-9-]+)"));
    QString saida;
    saida.reserve(folha.size());
    qsizetype ultimo = 0;
    auto it = re.globalMatch(folha);
    while (it.hasNext()) {
        const auto m = it.next();
        saida += QStringView(folha).mid(ultimo, m.capturedStart() - ultimo);
        const QByteArray nome = m.captured(1).toLatin1();
        const QColor c = token(nome.constData());
        saida += c.isValid() ? emQss(c) : m.captured(0);
        ultimo = m.capturedEnd();
    }
    saida += QStringView(folha).mid(ultimo);
    return saida;
}

// --------------------------------------------------------------------------
// Fonte
// --------------------------------------------------------------------------

// Cadeia de famílias, não uma só: pedir "Inter" e não ter Inter instalado faz
// o Qt cair na fonte padrão, que no Linux ainda pode ser uma bitmap dos anos
// 90. Cada entrada é a melhor fonte de interface de uma plataforma; a primeira
// que existir vence, e o fim da lista é o genérico que sempre existe.
//
// NÃO embutimos uma fonte no .qrc: seriam algumas centenas de KB no binário e
// uma licença a carregar, para ganhar consistência que a lista abaixo já dá em
// qualquer máquina dos últimos dez anos.
QStringList familias() {
    return {
        QStringLiteral("Source Sans 3"),          // a do design system
        QStringLiteral("Inter"),                  // se o usuário tiver
        QStringLiteral("Segoe UI Variable Text"), // Windows 11
        QStringLiteral("Segoe UI"),               // Windows 10
        QStringLiteral("SF Pro Text"),            // macOS
        QStringLiteral("Cantarell"),              // GNOME
        QStringLiteral("Noto Sans"),              // a maioria das distros
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("sans-serif"),
    };
}

// Tamanho base do app. Parte do que o sistema pediu — é a escala de fonte de
// acessibilidade, e ignorá-la é o tipo de "modernização" que deixa alguém sem
// conseguir ler o app. Só levanta um piso: certos ambientes Linux ainda
// entregam 8pt, pequeno demais para uma tabela de prazos.
qreal tamanhoBase() {
    const qreal doSistema = QApplication::font().pointSizeF();
    if (doSistema <= 0) return 10.0;              // fonte definida em pixel
    return doSistema < 9.5 ? 9.5 : doSistema;
}

} // namespace

// ---------------------------------------------------------------------------

Modo modoDoSistema() {
    // Escotilha de fuga: SIGAA_TEMA=claro|escuro vence o sistema. Existe porque
    // a metade do tema que o desenvolvedor NAO usa e a que apodrece sem
    // ninguem ver — quem trabalha no escuro so descobre o contraste ruim do
    // claro pelo relato de um usuario. Tambem serve a quem roda num ambiente
    // que informa o tema errado, o que ainda e comum fora de GNOME e KDE.
    const QByteArray forcado = qgetenv("SIGAA_TEMA").toLower();
    if (forcado == "claro") return Modo::Claro;
    if (forcado == "escuro") return Modo::Escuro;

    if (const auto* h = QGuiApplication::styleHints()) {
        if (h->colorScheme() == Qt::ColorScheme::Dark) return Modo::Escuro;
    }
    // `Unknown` cai aqui: melhor claro que um app que abre preto para quem nao
    // pediu tema escuro.
    return Modo::Claro;
}

Modo modo() { return emVigor; }

QColor token(const char* nome) {
    for (const auto& t : tokens()) {
        if (qstrcmp(t.nome, nome) == 0) {
            return emVigor == Modo::Escuro ? t.escuro : t.claro;
        }
    }
    qWarning("tema: token desconhecido '%s'", nome);
    return {};
}

QFont fonte(Papel papel) {
    QFont f = QApplication::font();
    f.setFamilies(familias());

    const qreal base = tamanhoBase();
    switch (papel) {
    case Papel::Titulo:
        // 22/14 no design system: título de tela é o maior texto do app, e
        // em negrito cheio — é ele que diz "você está aqui".
        f.setPointSizeF(base * 1.55);
        f.setWeight(QFont::Bold);
        // Título grande com espaçamento normal parece esticado; fechar um
        // pouco é o que o olho lê como "tipografia cuidada".
        f.setLetterSpacing(QFont::PercentageSpacing, 99);
        break;
    case Papel::Subtitulo:
        f.setPointSizeF(base * 1.12);
        f.setWeight(QFont::DemiBold);
        break;
    case Papel::Corpo:
        f.setPointSizeF(base);
        break;
    case Papel::Numero:
        f.setPointSizeF(base);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        // Algarismos de largura fixa. Numa coluna de datas e contagens, sem
        // isto cada linha alinha num lugar diferente e a tabela "treme" ao
        // ordenar. A feature é ignorada por fontes que não a tenham.
        f.setFeature("tnum", 1);
#endif
        break;
    case Papel::Legenda:
        f.setPointSizeF(base * 0.88);
        break;
    }
    return f;
}

namespace cor {
QColor atrasado() { return token("danger"); }
QColor urgente() { return token("warn"); }
QColor inferido() { return token("warn"); }
QColor apagado() { return token("text-3"); }
QColor sucesso() { return token("ok"); }
QColor acento() { return token("accent"); }
} // namespace cor

namespace {

// Rótulo de coluna do design system: 12/700 em caixa-alta, espaçado. Em C++ e
// não no .qss pela mesma razão de sempre — tamanho de fonte sai da fonte do
// sistema, e caixa-alta não é propriedade que o parser de QSS conheça.
void ajustarCabecalho(QHeaderView* h) {
    if (!h) return;
    QFont f = fonte(Papel::Legenda);
    f.setWeight(QFont::Bold);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::PercentageSpacing, 107);
    h->setFont(f);
    // No viewport também, e não é redundância: com folha de estilo ativa o
    // viewport resolve a própria fonte no polish e para de herdar a do
    // cabeçalho — e é nele que o texto é pintado. Só no cabeçalho, a mudança
    // não aparecia.
    h->viewport()->setFont(f);
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

} // namespace

void ajustarLista(QAbstractItemView* v) {
    if (!v) return;

    v->setFrameShape(QFrame::NoFrame);
    // Sem zebra: as linhas se separam por um filete (ver estilo.qss). Zebra e
    // filete juntos são duas marcações dizendo a mesma coisa.
    v->setAlternatingRowColors(false);

    // `setWordWrap` nao existe na base: cada view concreta tem o seu. Sem ele,
    // uma celula que cresce em altura desalinha a linha inteira, e os titulos
    // do SIGAA sao longos o bastante para isso acontecer em metade das linhas.
    if (auto* tv = qobject_cast<QTableView*>(v)) {
        tv->setWordWrap(false);
        tv->setShowGrid(false);
        tv->verticalHeader()->setVisible(false);
        // Altura derivada da fonte, nunca em pixel fixo: quem aumenta a fonte
        // do sistema veria o texto cortado dentro de uma linha de 24 px.
        tv->verticalHeader()->setDefaultSectionSize(
            v->fontMetrics().height() + esp(5));
        tv->horizontalHeader()->setHighlightSections(false);
        ajustarCabecalho(tv->horizontalHeader());
    } else if (auto* arv = qobject_cast<QTreeView*>(v)) {
        arv->setWordWrap(false);
        arv->header()->setHighlightSections(false);
        ajustarCabecalho(arv->header());
        // NÃO uniformiza a altura: com ela o Qt mede a PRIMEIRA linha e repete
        // o número, então a linha de grupo (sem distintivo, mais baixa) ditava
        // a altura das de material — e a etiqueta de faltas saía cortada.
        // O custo de medir linha a linha é irrelevante numa semana de aulas.
        arv->setUniformRowHeights(false);
        arv->setIndentation(esp(3));
    }
}

void esticarColuna(QAbstractItemView* v, int coluna) {
    if (!v) return;
    QHeaderView* h = nullptr;
    if (auto* tv = qobject_cast<QTableView*>(v)) h = tv->horizontalHeader();
    else if (auto* arv = qobject_cast<QTreeView*>(v)) h = arv->header();
    if (!h || coluna < 0 || coluna >= h->count()) return;

    // Interactive nas demais: quem quiser alargar "Turma" para ler o nome
    // inteiro continua podendo arrastar. Stretch só na coluna escolhida.
    h->setSectionResizeMode(QHeaderView::Interactive);

    // Devolve o respiro que o cálculo de largura não enxergou. Sem isto toda
    // coluna medida por conteúdo nasce estreita demais para o próprio texto.
    for (int c = 0; c < h->count(); ++c) {
        if (c == coluna || h->isSectionHidden(c)) continue;
        h->resizeSection(c, h->sectionSize(c) + 2 * kRespiroCelula);
    }

    h->setSectionResizeMode(coluna, QHeaderView::Stretch);
    h->setStretchLastSection(false);
}

namespace {

// Mede e fixa o mínimo. Separada do filtro de eventos porque roda duas vezes:
// na instalação, para a janela já nascer do tamanho certo, e a cada mudança de
// largura do rótulo.
//
// A ALTURA VEM DO PRÓPRIO QLabel (`heightForWidth`), e não de um cálculo com
// QFontMetrics: o rótulo pode ter moldura e padding vindos da folha de estilo
// — "recado" tem 10 px em cima e embaixo —, e refazer essa conta aqui seria a
// mesma armadilha de kRespiroCelula, com um número duplicado que ninguém
// atualiza junto. Trocar o texto para medir não pisca: o repintar só acontece
// quando o laço devolve o controle ao Qt, e aí o texto verdadeiro já voltou.
void medirEReservar(QLabel* r, const QStringList& textos) {
    const int largura = r->width();
    if (largura <= 0 || textos.isEmpty()) return;

    const QString original = r->text();
    int maior = 0;
    for (const QString& t : textos) {
        r->setText(t);
        maior = std::max(maior, r->heightForWidth(largura));
    }
    r->setText(original);

    if (maior > 0 && maior != r->minimumHeight()) r->setMinimumHeight(maior);
}

// Recalcula quando a largura muda — é a única coisa que muda a quebra de
// linha, e portanto a altura. Vive como filho do rótulo, então morre com ele.
class Reservador : public QObject {
public:
    Reservador(QLabel* r, QStringList textos)
        : QObject(r), rotulo_(r), textos_(std::move(textos)) {
        r->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj == rotulo_ && e->type() == QEvent::Resize) {
            const int agora = rotulo_->width();
            // Só na mudança de LARGURA: fixar o mínimo dispara um novo Resize,
            // e reagir à altura faria os dois se alimentarem em laço.
            if (agora != ultimaLargura_) {
                ultimaLargura_ = agora;
                medirEReservar(rotulo_, textos_);
            }
        }
        return QObject::eventFilter(obj, e);
    }

private:
    QLabel* rotulo_;
    QStringList textos_;
    int ultimaLargura_{-1};
};

} // namespace

void reservarAltura(QLabel* r, const QStringList& textos) {
    if (!r || textos.isEmpty()) return;
    r->setWordWrap(true);
    new Reservador(r, textos);
    medirEReservar(r, textos);
}

void aplicar(QApplication& app) {
    auto pintar = [&app] {
        emVigor = modoDoSistema();

        // Ordem obrigatória: o estilo primeiro, porque trocar de estilo
        // redefine a paleta e apagaria a nossa; a folha por último, porque ela
        // é resolvida contra a paleta em vigor.
        app.setStyle(QStringLiteral("Fusion"));
        app.setPalette(montarPaleta());
        app.setFont(fonte(Papel::Corpo));

        // Do .qrc, não do disco: um arquivo solto ao lado do .exe seria mais
        // fácil de ajustar, mas viraria "o app abriu sem estilo" na primeira
        // vez que alguém copiasse só o executável.
        QFile f(QStringLiteral(":/estilo/estilo.qss"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        app.setStyleSheet(resolverTokens(QString::fromUtf8(f.readAll())));
    };

    pintar();

    // Trocar o tema do Windows com o app aberto repinta a janela, sem
    // reiniciar. Sem isto o app ficaria claro sobre uma área de trabalho
    // escura até a próxima execução — e é justamente à noite que alguém troca.
    if (auto* h = QGuiApplication::styleHints()) {
        QObject::connect(h, &QStyleHints::colorSchemeChanged, &app,
                         [pintar](Qt::ColorScheme) { pintar(); });
    }
}

} // namespace sigaa::ui::tema
