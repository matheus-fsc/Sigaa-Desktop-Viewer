#pragma once
// Icones do app, vindos do .qrc (recursos/icones/*.svg).
//
// Os SVGs monocromaticos sao desenhados em preto e REPINTADOS aqui com a cor
// do tema. Isso resolve o problema que um .png colorido no repositorio nao
// resolve: o Windows troca entre claro e escuro em tempo de execucao, e um
// icone cinza-escuro fixo fica ilegivel na barra de ferramentas escura.
//
// Quem usa estes icones precisa reagir a QEvent::PaletteChange e pedi-los de
// novo — ver JanelaPrincipal::changeEvent(). Icone e pixmap: e tirado uma vez
// e nao se atualiza sozinho quando a paleta muda.

#include <QIcon>
#include <QString>

class QWidget;

namespace sigaa::ui {

// Icone monocromatico tingido com `cor`. `nome` e o nome do arquivo sem
// extensao ("atualizar", "conta", ...).
QIcon icone(const QString& nome, const QColor& cor);

// Mesma coisa, tirando as cores da paleta de `alvo`: texto de botao no estado
// normal, texto-sobre-realce no estado ligado (QIcon::On). A variante On e o
// que mantem o icone visivel quando o QSS pinta o botao marcado de azul.
QIcon icone(const QString& nome, const QWidget* alvo);

// Icone da aplicacao. Colorido, sem tingimento — identidade nao segue tema.
QIcon iconeApp();

} // namespace sigaa::ui
