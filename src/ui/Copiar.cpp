#include "ui/Copiar.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QAction>
#include <QEvent>
#include <functional>
#include <QMenu>
#include <QShortcut>
#include <QStringList>
#include <QTableView>
#include <QTreeView>

namespace sigaa::ui {
namespace {

QHeaderView* cabecalho(QAbstractItemView* v) {
    if (auto* t = qobject_cast<QTableView*>(v)) return t->horizontalHeader();
    if (auto* a = qobject_cast<QTreeView*>(v)) return a->header();
    return nullptr;
}

// As colunas na ORDEM DA TELA, sem as escondidas. O usuário pode ter arrastado
// "Turma" para a frente; colar numa ordem diferente da que ele vê seria uma
// surpresa silenciosa na planilha.
QList<int> colunasVisiveis(QAbstractItemView* v) {
    QList<int> out;
    auto* h = cabecalho(v);
    const int n = v->model() ? v->model()->columnCount() : 0;
    for (int posicao = 0; posicao < n; ++posicao) {
        const int coluna = h ? h->logicalIndex(posicao) : posicao;
        if (h && h->isSectionHidden(coluna)) continue;
        out << coluna;
    }
    return out;
}

QString textoDaCelula(const QModelIndex& i) {
    // `DisplayRole`, não o valor de ordenação: quem copia quer o que estava
    // vendo. A coluna Prazo mostra "atrasado 6 dias" e ordena por -6.
    QString t = i.data(Qt::DisplayRole).toString();
    // TAB e quebra de linha destruiriam o alinhamento do TSV. Nenhum campo do
    // SIGAA tem os dois, mas um dia terá — e o estrago seria numa planilha
    // colada, longe daqui.
    t.replace(QLatin1Char('\t'), QLatin1Char(' '));
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return t;
}

// Roda uma função logo antes de o menu de contexto aparecer.
//
// Existe porque as ações do menu precisam do estado da seleção NA HORA, e os
// sinais de seleção vêm de um objeto que `setModel` substitui a cada recarga.
// O evento não tem esse problema.
class AtualizadorDeMenu : public QObject {
public:
    AtualizadorDeMenu(std::function<void()> f, QObject* pai)
        : QObject(pai), f_(std::move(f)) {}

protected:
    bool eventFilter(QObject* alvo, QEvent* ev) override {
        if (ev->type() == QEvent::ContextMenu) f_();
        return QObject::eventFilter(alvo, ev);   // nunca consome: só observa
    }

private:
    std::function<void()> f_;
};

} // namespace

void copiarSelecao(QAbstractItemView* v) {
    if (!v || !v->model() || !v->selectionModel()) return;

    const QModelIndexList linhas = v->selectionModel()->selectedRows();
    if (linhas.isEmpty()) return;

    const QList<int> colunas = colunasVisiveis(v);
    if (colunas.isEmpty()) return;

    QStringList saida;

    // Cabeçalho só a partir de duas linhas: para uma linha ele é mais atrapalho
    // que ajuda, e quem copia uma linha quase sempre quer colar num texto.
    if (linhas.size() > 1) {
        if (auto* h = cabecalho(v)) {
            QStringList nomes;
            for (int c : colunas) {
                nomes << v->model()->headerData(c, h->orientation()).toString();
            }
            saida << nomes.join(QLatin1Char('\t'));
        }
    }

    for (const QModelIndex& linha : linhas) {
        QStringList celulas;
        for (int c : colunas) {
            celulas << textoDaCelula(linha.sibling(linha.row(), c));
        }
        // Linha de grupo (um cabeçalho de seção da árvore) vem com o resto
        // vazio. Sai assim mesmo: ela dá o contexto de onde as linhas abaixo
        // vieram, e quem selecionou o grupo inteiro esperava vê-lo.
        saida << celulas.join(QLatin1Char('\t'));
    }

    QApplication::clipboard()->setText(saida.join(QLatin1Char('\n')));
}

void habilitarCopia(QAbstractItemView* v) {
    if (!v) return;

    // Várias linhas de uma vez. É o padrão do Qt, mas os formulários podem ter
    // sido salvos com outro modo — garantir aqui evita a surpresa de Ctrl+C
    // copiar sempre uma linha só.
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // `WidgetWithChildrenShortcut`: o atalho vale quando o foco está na lista,
    // e não rouba o Ctrl+C da janela inteira — senão copiar de um campo de
    // texto ao lado passaria a copiar a tabela.
    auto* atalho = new QShortcut(QKeySequence::Copy, v);
    atalho->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(atalho, &QShortcut::activated, v, [v] { copiarSelecao(v); });

    // `ActionsContextMenu` em vez de um menu montado à mão: o menu passa a ser
    // a lista de ações DA VIEW, e quem tiver mais o que oferecer ali só precisa
    // chamar `addAction`.
    //
    // A primeira versão montava o menu aqui dentro, com Copiar e nada mais —
    // e o resultado foi um botão direito sobre uma prova que oferecia copiar
    // mas não editar, com os botões de edição escondidos numa barra acima. O
    // menu de contexto tem de oferecer o que se faz com AQUELE item.
    auto* copiar = new QAction(QObject::tr("Copiar"), v);
    copiar->setShortcut(QKeySequence::Copy);
    copiar->setShortcutVisibleInContextMenu(true);
    QObject::connect(copiar, &QAction::triggered, v, [v] { copiarSelecao(v); });
    v->addAction(copiar);

    auto* separador = new QAction(v);
    separador->setSeparator(true);
    v->addAction(separador);

    v->setContextMenuPolicy(Qt::ActionsContextMenu);

    // O rótulo conta quantas linhas vão: "Copiar 3 linhas" é a confirmação de
    // que a seleção é a que a pessoa acha que é, antes de ela colar em algum
    // lugar e descobrir que veio uma só.
    auto renomear = [v, copiar] {
        const int n = v->selectionModel() ? v->selectionModel()->selectedRows().size() : 0;
        copiar->setEnabled(n > 0);
        copiar->setText(n > 1 ? QObject::tr("Copiar %1 linhas").arg(n)
                              : QObject::tr("Copiar linha"));
    };
    // Atualizado quando o menu VAI ABRIR, e não por sinal de seleção.
    //
    // `setModel` troca o modelo de seleção, então uma conexão feita agora
    // morreria na primeira recarga da lista — e este `habilitarCopia` roda
    // antes de haver modelo nenhum, quando `selectionModel()` ainda é nulo.
    // O evento de menu de contexto chega sempre, e sempre com a seleção
    // corrente.
    v->viewport()->installEventFilter(new AtualizadorDeMenu(renomear, v));
    renomear();
}

} // namespace sigaa::ui
