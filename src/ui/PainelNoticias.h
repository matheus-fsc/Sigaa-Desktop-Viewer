#pragma once
// As notícias da turma, uma por vez, no topo da aba Aulas da janela da turma.
//
// Uma por vez, e não uma lista: notícia de professor é texto corrido ("leiam o
// documento tal, a entrega é dia tal"), e o que o aluno quer ao abrir a turma é
// LER a última. As setas levam às antigas, na ordem em que foram publicadas.

#include <QFrame>

#include <vector>

#include "core/model/Models.h"

class QLabel;
class QTextBrowser;
class QToolButton;

namespace sigaa::ui {

class PainelNoticias : public QFrame {
public:
    explicit PainelNoticias(QWidget* pai = nullptr);

    // Da mais nova para a mais antiga (a ordem de Database::carregarNoticias).
    // Mostra a mais nova. `coletado` distingue "a coleta nunca olhou" de
    // "olhou e a turma não tem notícia".
    void definir(std::vector<Noticia> noticias, bool coletado);

private:
    void mostrar();

    std::vector<Noticia> noticias_;   // mais nova primeiro
    int atual_{0};                    // índice em noticias_
    bool coletado_{false};

    QLabel* posicao_{nullptr};
    QToolButton* anterior_{nullptr};   // mais antiga
    QToolButton* seguinte_{nullptr};   // mais nova
    QLabel* titulo_{nullptr};
    QLabel* meta_{nullptr};
    QTextBrowser* texto_{nullptr};
};

} // namespace sigaa::ui
