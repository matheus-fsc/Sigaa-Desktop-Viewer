#pragma once
// Distintivo: o texto de uma célula desenhado como etiqueta arredondada.
//
// POR QUE UM DELEGATE, e não cor no QStandardItem:
//
//   Pintar o texto de vermelho já funcionava — `cor::atrasado()` no foreground
//   do item. O problema é que uma tabela inteira de texto colorido não tem
//   hierarquia: "atrasado 2 dias" em vermelho e "em 3 dias" em preto competem
//   com o nome da turma e o título da atividade pela mesma atenção, e o olho
//   não sabe onde pousar. A etiqueta resolve isso dando ao estado uma FORMA,
//   não só um tom — dá para achar o que está atrasado sem ler nada.
//
//   Fazer isso dentro do modelo não daria: QStandardItem sabe pintar texto e
//   fundo de célula, não desenhar. E fundo de célula inteiro seria pior que o
//   texto colorido — viraria uma tabela-semáforo, que é o que a nota em
//   `modeloPrazos` já evitava desde o começo.
//
// A DIVISÃO: o modelo diz o que a célula SIGNIFICA, gravando um `Tom` em
// `PapelDistintivo`; este arquivo decide como isso vira pixel. É a mesma
// separação que o resto de Modelos.h mantém entre o valor e a sua exibição —
// e é o que permite mudar a aparência de todos os distintivos do app num
// lugar só, sem abrir um modelo sequer.
//
// Célula sem `PapelDistintivo` cai no desenho padrão do Qt, então instalar o
// delegate numa tabela inteira é seguro: só muda o que foi marcado.

#include <QStyledItemDelegate>
#include <Qt>

class QTableView;
class QTreeView;

namespace sigaa::ui {

// O que a célula significa. NÃO é uma cor: "Urgente" continua sendo urgente
// quando o tema escuro trocar o laranja, e é o tema que decide qual laranja.
enum class Tom {
    Nenhum = 0,   // sem distintivo; desenho padrão
    Neutro,       // informação, sem alarme ("painel do professor")
    Sucesso,      // baixado, disponível offline
    Urgente,      // vence em até 2 dias, dado a confirmar
    Atrasado,     // prazo vencido
    Apagado,      // concluído ou já passado: presente, sem peso
};

// Onde o modelo grava o `Tom`. Fora da faixa usada por Modelos.h
// (UserRole+1..+4), porque os dois papéis convivem na mesma célula: a coluna
// "Prazo" tem chave de ordenação E distintivo.
inline constexpr int PapelDistintivo = Qt::UserRole + 10;

class DelegadoDistintivo : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opcao,
               const QModelIndex& indice) const override;
    QSize sizeHint(const QStyleOptionViewItem& opcao,
                   const QModelIndex& indice) const override;
};

} // namespace sigaa::ui
