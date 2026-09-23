#include "ui/Tema.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTableView>
#include <QTreeView>

namespace sigaa::ui::tema {
namespace {

Modo emVigor = Modo::Claro;

// --------------------------------------------------------------------------
// Paletas
// --------------------------------------------------------------------------
//
// Os nomes dos papéis não são livres: o `.qss` já usa `palette(mid)` como cor
// de BORDA e `palette(alternate-base)` como superfície de realce (fundo de
// cartão, hover de botão). Trocar o sentido de um deles aqui redesenharia o
// app inteiro de viés. Então `Mid` é sempre cinza de borda, e `AlternateBase`
// é sempre uma superfície um passo acima do fundo.

struct Cores {
    QColor janela;        // fundo da janela
    QColor texto;         // texto sobre a janela
    QColor base;          // fundo de campo e de lista
    QColor superficie;    // AlternateBase: cartão, zebra, hover
    QColor borda;         // Mid
    QColor secundario;    // PlaceholderText: o cinza legível
    QColor destaque;      // Highlight
    QColor sobreDestaque; // HighlightedText
    QColor desabilitado;
    QColor dica;          // fundo de tooltip

    QColor atrasado;
    QColor urgente;
    QColor apagado;
    QColor sucesso;
};

// Claro. O fundo não é branco puro: #FFFFFF de parede com tabelas brancas em
// cima apaga a separação entre o conteúdo e a moldura, e é o que faz um app
// parecer uma planilha. A janela recua um tom e as superfícies sobem.
const Cores& claro() {
    static const Cores c{
        /*janela*/ QColor(0xF6, 0xF7, 0xF9),
        /*texto*/ QColor(0x1B, 0x1F, 0x24),
        /*base*/ QColor(0xFF, 0xFF, 0xFF),
        /*superficie*/ QColor(0xEF, 0xF1, 0xF4),
        /*borda*/ QColor(0xDC, 0xE0, 0xE6),
        /*secundario*/ QColor(0x66, 0x6E, 0x7A),
        /*destaque*/ QColor(0x2F, 0x62, 0xE8),
        /*sobreDestaque*/ QColor(0xFF, 0xFF, 0xFF),
        /*desabilitado*/ QColor(0xA3, 0xAA, 0xB4),
        /*dica*/ QColor(0x1B, 0x1F, 0x24),

        /*atrasado*/ QColor(0xC0, 0x2A, 0x2A),
        /*urgente*/ QColor(0xB4, 0x5B, 0x04),
        /*apagado*/ QColor(0x8A, 0x92, 0x9E),
        /*sucesso*/ QColor(0x1B, 0x7F, 0x4B),
    };
    return c;
}

// Escuro. Nada de preto puro: #000 com texto branco produz o halo que cansa a
// vista em leitura longa, e a lista de prazos é para ser lida.
const Cores& escuro() {
    static const Cores c{
        /*janela*/ QColor(0x15, 0x18, 0x1D),
        /*texto*/ QColor(0xE3, 0xE7, 0xEC),
        /*base*/ QColor(0x1B, 0x1F, 0x26),
        /*superficie*/ QColor(0x23, 0x28, 0x31),
        /*borda*/ QColor(0x32, 0x39, 0x45),
        /*secundario*/ QColor(0x8B, 0x94, 0xA3),
        /*destaque*/ QColor(0x5B, 0x8D, 0xEF),
        /*sobreDestaque*/ QColor(0x0D, 0x11, 0x17),
        /*desabilitado*/ QColor(0x5A, 0x63, 0x70),
        /*dica*/ QColor(0x2B, 0x31, 0x3B),

        // Mais claras e menos saturadas que as do tema claro: a mesma tinta que
        // se lê sobre branco vira um borrão escuro sobre #15181D.
        /*atrasado*/ QColor(0xFF, 0x6B, 0x6B),
        /*urgente*/ QColor(0xF0, 0xA1, 0x3E),
        /*apagado*/ QColor(0x76, 0x7F, 0x8C),
        /*sucesso*/ QColor(0x54, 0xC3, 0x8A),
    };
    return c;
}

const Cores& atual() { return emVigor == Modo::Escuro ? escuro() : claro(); }

QPalette montarPaleta(const Cores& c) {
    QPalette p;

    p.setColor(QPalette::Window, c.janela);
    p.setColor(QPalette::WindowText, c.texto);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.superficie);
    p.setColor(QPalette::Text, c.texto);
    p.setColor(QPalette::PlaceholderText, c.secundario);
    p.setColor(QPalette::Button, c.base);
    p.setColor(QPalette::ButtonText, c.texto);
    p.setColor(QPalette::Mid, c.borda);
    p.setColor(QPalette::Midlight, c.superficie);
    p.setColor(QPalette::Dark, c.secundario);
    p.setColor(QPalette::Light, c.base);
    p.setColor(QPalette::Shadow, c.borda);
    p.setColor(QPalette::Highlight, c.destaque);
    p.setColor(QPalette::HighlightedText, c.sobreDestaque);
    p.setColor(QPalette::Link, c.destaque);
    p.setColor(QPalette::LinkVisited, c.destaque);
    p.setColor(QPalette::ToolTipBase, c.dica);
    p.setColor(QPalette::ToolTipText, emVigor == Modo::Escuro ? c.texto : c.base);

    // O grupo Disabled é obrigatório, não enfeite: sem ele o Qt deriva o
    // cinza de desabilitado da paleta PADRÃO, não desta — e no tema escuro o
    // resultado é um botão desabilitado com texto quase invisível. Os botões
    // desta janela ficam desabilitados o tempo todo durante o sync.
    for (const auto papel : {QPalette::WindowText, QPalette::Text,
                             QPalette::ButtonText, QPalette::HighlightedText}) {
        p.setColor(QPalette::Disabled, papel, c.desabilitado);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.superficie);

    return p;
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

QFont fonte(Papel papel) {
    QFont f = QApplication::font();
    f.setFamilies(familias());

    const qreal base = tamanhoBase();
    switch (papel) {
    case Papel::Titulo:
        f.setPointSizeF(base * 1.5);
        f.setWeight(QFont::DemiBold);
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
QColor atrasado() { return atual().atrasado; }
QColor urgente() { return atual().urgente; }
QColor inferido() { return atual().urgente; }
QColor apagado() { return atual().apagado; }
QColor sucesso() { return atual().sucesso; }
} // namespace cor

void ajustarLista(QAbstractItemView* v) {
    if (!v) return;

    v->setFrameShape(QFrame::NoFrame);

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
    } else if (auto* arv = qobject_cast<QTreeView*>(v)) {
        arv->setWordWrap(false);
        arv->header()->setHighlightSections(false);
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

void aplicar(QApplication& app) {
    auto pintar = [&app] {
        emVigor = modoDoSistema();

        // Ordem obrigatória: o estilo primeiro, porque trocar de estilo
        // redefine a paleta e apagaria a nossa; a folha por último, porque ela
        // é resolvida contra a paleta em vigor.
        app.setStyle(QStringLiteral("Fusion"));
        app.setPalette(montarPaleta(atual()));
        app.setFont(fonte(Papel::Corpo));

        // Do .qrc, não do disco: um arquivo solto ao lado do .exe seria mais
        // fácil de ajustar, mas viraria "o app abriu sem estilo" na primeira
        // vez que alguém copiasse só o executável.
        QFile f(QStringLiteral(":/estilo/estilo.qss"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
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
