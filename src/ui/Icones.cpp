#include "ui/Icones.h"

#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QWidget>

namespace sigaa::ui {
namespace {

// Tamanhos gerados. QIcon escolhe o mais proximo e reescala, entao a lista so
// precisa cobrir os pontos onde o resultado importa: 16 (menu e bandeja), 24
// (barra de ferramentas), 32/48 (o mesmo em telas 200%).
constexpr int kTamanhos[] = {16, 20, 24, 32, 48, 64};

QString caminho(const QString& nome) {
    return QStringLiteral(":/icones/%1.svg").arg(nome);
}

// Mantem o alfa do desenho e troca todo o resto pela cor pedida. E o truque
// que permite um unico SVG servir tema claro e escuro.
QPixmap tingir(const QPixmap& origem, const QColor& cor) {
    QPixmap saida(origem.size());
    saida.setDevicePixelRatio(origem.devicePixelRatio());
    saida.fill(Qt::transparent);

    QPainter p(&saida);
    p.drawPixmap(0, 0, origem);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(QRect(QPoint(0, 0), origem.size()), cor);
    p.end();
    return saida;
}

void preencher(QIcon& destino, const QIcon& base, const QColor& cor,
               QIcon::State estado) {
    for (const int t : kTamanhos) {
        const QPixmap pm = base.pixmap(t, t);
        if (pm.isNull()) continue;   // sem o plugin de SVG nao ha o que tingir
        destino.addPixmap(tingir(pm, cor), QIcon::Normal, estado);
    }
}

} // namespace

QIcon icone(const QString& nome, const QColor& cor) {
    const QIcon base(caminho(nome));
    QIcon saida;
    preencher(saida, base, cor, QIcon::Off);
    return saida;
}

QIcon icone(const QString& nome, const QWidget* alvo) {
    const QPalette pal = alvo ? alvo->palette() : QPalette();
    const QIcon base(caminho(nome));

    QIcon saida;
    preencher(saida, base, pal.color(QPalette::ButtonText), QIcon::Off);
    preencher(saida, base, pal.color(QPalette::HighlightedText), QIcon::On);
    return saida;
}

QIcon iconeApp() { return QIcon(caminho(QStringLiteral("app"))); }

} // namespace sigaa::ui
