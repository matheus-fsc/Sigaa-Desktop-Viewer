#include "ui/Distintivos.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>

#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

// Raio e respiro da etiqueta. Não saem de tema::esp() porque não são margem de
// layout: são a forma do desenho, e mudá-los junto com o espaçamento das telas
// deformaria a etiqueta sem ninguém pedir.
constexpr int kRaio = 9;
constexpr int kPadH = 8;   // respiro horizontal dentro da etiqueta
constexpr int kPadV = 3;

Tom tomDe(const QModelIndex& i) {
    const QVariant v = i.data(PapelDistintivo);
    if (!v.isValid()) return Tom::Nenhum;
    const int n = v.toInt();
    if (n < static_cast<int>(Tom::Neutro) || n > static_cast<int>(Tom::Apagado)) {
        return Tom::Nenhum;
    }
    return static_cast<Tom>(n);
}

// A cor do texto da etiqueta. O fundo sai dela com opacidade baixa — um par
// (tinta, tinta a 14%) é legível nos dois temas sem precisar de uma segunda
// tabela de cores para manter em sincronia.
QColor tintaDe(Tom t) {
    switch (t) {
    case Tom::Sucesso:  return tema::cor::sucesso();
    case Tom::Urgente:  return tema::cor::urgente();
    case Tom::Atrasado: return tema::cor::atrasado();
    case Tom::Apagado:  return tema::cor::apagado();
    case Tom::Neutro:
    case Tom::Nenhum:
        break;
    }
    return tema::cor::apagado();
}

QColor fundoDe(Tom t, const QPalette& p) {
    if (t == Tom::Neutro) return p.color(QPalette::AlternateBase);
    QColor c = tintaDe(t);
    // Opacidade, e não uma cor clara fixa: assim o mesmo código funciona sobre
    // a linha branca, sobre a linha zebrada e sobre a seleção azul, que é onde
    // um "#FDECEC" cravado apareceria como mancha.
    // ~16% no escuro e ~13% no claro: os "soft" do design system.
    c.setAlpha(tema::modo() == tema::Modo::Escuro ? 44 : 34);
    return c;
}

} // namespace

void DelegadoDistintivo::paint(QPainter* p, const QStyleOptionViewItem& opcao,
                               const QModelIndex& indice) const {
    const Tom t = tomDe(indice);
    const QString texto = indice.data(Qt::DisplayRole).toString();
    if (t == Tom::Nenhum || texto.isEmpty()) {
        QStyledItemDelegate::paint(p, opcao, indice);
        return;
    }

    // O fundo da linha (zebra, hover, seleção) é desenhado pelo estilo, não
    // por nós: reimplementá-lo seria perder o realce de seleção e o hover que
    // o QSS define. Só o texto é substituído pela etiqueta.
    QStyleOptionViewItem base(opcao);
    initStyleOption(&base, indice);
    base.text.clear();
    const QWidget* w = base.widget;
    const QStyle* estilo = w ? w->style() : QApplication::style();
    estilo->drawControl(QStyle::CE_ItemViewItem, &base, p, w);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    // Negrito, como no design system: a pílula é o que exige ação na linha, e
    // é por ela que o olho entra.
    QFont fonteEtiqueta = opcao.font;
    fonteEtiqueta.setWeight(QFont::Bold);
    p->setFont(fonteEtiqueta);

    const QFontMetrics fm(fonteEtiqueta);
    // A etiqueta encolhe antes da célula: numa coluna estreitada pelo usuário,
    // um retângulo maior que a célula vazaria por cima da coluna vizinha.
    const int larguraMax = opcao.rect.width() - 2 * kPadH;
    const QString mostrado = fm.elidedText(texto, Qt::ElideRight, larguraMax);

    const int larg = fm.horizontalAdvance(mostrado) + 2 * kPadH;
    const int alt = fm.height() + 2 * kPadV;
    QRect etiqueta(0, 0, qMin(larg, opcao.rect.width()), alt);
    // Encostada à esquerda, centrada na vertical: a coluna de estado é lida em
    // varredura vertical, e etiquetas centradas horizontalmente fariam a borda
    // esquerda serrilhar a cada linha de largura diferente.
    etiqueta.moveLeft(opcao.rect.left() + 2);
    etiqueta.moveTop(opcao.rect.top() + (opcao.rect.height() - alt) / 2);

    QPainterPath forma;
    forma.addRoundedRect(etiqueta, kRaio, kRaio);
    p->fillPath(forma, fundoDe(t, opcao.palette));

    // A seleção é um azul "soft" translúcido, então a tinta do tom continua
    // legível sobre ela — trocar pela cor do texto apagaria a informação.
    p->setPen(tintaDe(t));
    p->drawText(etiqueta, Qt::AlignCenter, mostrado);
    p->restore();
}

QSize DelegadoDistintivo::sizeHint(const QStyleOptionViewItem& opcao,
                                   const QModelIndex& indice) const {
    QSize s = QStyledItemDelegate::sizeHint(opcao, indice);

    // ALTURA MÍNIMA EM TODA CÉLULA, tenha ela distintivo ou não.
    //
    // É o que dá à QTreeView o mesmo respiro que a QTableView recebe do
    // `defaultSectionSize` — a árvore não tem esse ajuste, e a agenda ficava
    // com linhas de 20 px logo acima de uma tabela de 42, parecendo dois apps
    // colados. Pior: a etiqueta de faltas não cabia em 20 px e saía cortada.
    //
    // Derivada da fonte, nunca em pixel fixo: quem aumenta a fonte do sistema
    // veria o texto espremido numa linha de altura fixa.
    s.setHeight(qMax(s.height(), opcao.fontMetrics.height() + 2 * kPadV + 10));

    if (tomDe(indice) == Tom::Nenhum) return s;
    // Conta o respiro da etiqueta: sem isto o resizeColumnsToContents mede só
    // o texto e a coluna fica estreita demais, cortando a etiqueta na abertura
    // da janela — antes de o usuário ter chance de arrastar a divisória.
    //
    // Medida em NEGRITO, que é como a etiqueta é pintada: o sizeHint da base
    // mede a fonte normal, e a diferença cortava a última letra.
    QFont negrito = opcao.font;
    negrito.setWeight(QFont::Bold);
    const int texto = QFontMetrics(negrito).horizontalAdvance(
        indice.data(Qt::DisplayRole).toString());
    s.setWidth(qMax(s.width(), texto + 6) + 2 * kPadH + 4);
    return s;
}

} // namespace sigaa::ui
