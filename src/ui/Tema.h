#pragma once
// A ÚNICA autoridade sobre cor, tipografia e espaçamento do sigaa-ui.
//
// POR QUE ISTO EXISTE — e o que muda em relação ao que havia antes:
//
//   `estilo.qss` tinha (e mantém) a regra "nenhuma cor literal": tudo sai de
//   `palette(...)`, que o Qt resolve contra a paleta viva. Foi essa regra que
//   salvou o app quando o Windows está no tema escuro — um "#f5f5f5" cravado
//   viraria branco sobre branco e ninguém testa isso.
//
//   O efeito colateral era o app herdar, junto com a paleta do sistema, o
//   visual padrão do Qt. As duas coisas vinham no mesmo pacote e só uma
//   interessava.
//
//   Este arquivo separa as duas: a paleta passa a ser NOSSA, definida aqui em
//   duas variantes (clara e escura) e escolhida pelo tema do sistema. O `.qss`
//   continua sem uma cor literal sequer — `palette(base)`, `palette(mid)` e
//   companhia agora resolvem contra estas cores. A regra fica mais forte, não
//   mais fraca: passa a existir UM lugar onde a cor é decidida.
//
// SOBRE O ESTILO FUSION: `aplicar` troca o estilo nativo por Fusion, de
// propósito. O estilo nativo do Windows 11 ignora QPalette em boa parte dos
// controles e desenha por cima do QSS — com ele, metade das cores daqui não
// apareceria, e as que aparecessem brigariam com as que não. Fusion honra a
// paleta inteira nas três plataformas. O preço é abrir mão da aparência
// nativa, que é exatamente o que se pede ao redesenhar.

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStringList>

class QAbstractItemView;
class QLabel;
class QApplication;

namespace sigaa::ui::tema {

enum class Modo { Claro, Escuro };

// O que o sistema está pedindo agora. Em plataforma que não sabe responder, o
// Qt devolve `Unknown` e assumimos claro — o oposto de um app que abre preto
// para quem não pediu tema escuro.
//
// `SIGAA_TEMA=claro` ou `SIGAA_TEMA=escuro` no ambiente vence o sistema.
Modo modoDoSistema();

// O modo em vigor. Válido só depois de `aplicar`.
Modo modo();

// Estilo + paleta + fonte + folha de estilo, nesta ordem (cada passo depende
// do anterior). Chamar UMA vez, antes de criar qualquer janela.
//
// Também assina a troca de tema do sistema: quem muda o Windows para escuro
// com o app aberto vê a janela acompanhar, sem reiniciar.
void aplicar(QApplication& app);

// ---------------------------------------------------------------------------
// Espaçamento
// ---------------------------------------------------------------------------

// Ritmo de 4px, usado em múltiplos: `esp(2)` = 8, `esp(4)` = 16. Existe para
// que margem e respiro saiam de uma escala em vez de números escolhidos um a
// um — é o que faz telas diferentes parecerem o mesmo app.
inline constexpr int esp(int passos) { return passos * 4; }

// Respiro horizontal de uma célula de tabela.
//
// TEM DE BATER com o `padding` de `QTableView::item` em estilo.qss, e existe
// aqui porque o Qt não deixa perguntar: `resizeColumnsToContents` mede o
// sizeHint do delegate, que não sabe nada do padding da folha de estilo. A
// coluna sai 20 px mais estreita do que o desenho precisa e a última palavra
// aparece cortada — "Questioná..." numa janela com meia tela vazia à direita.
// `esticarColuna` devolve essa diferença.
inline constexpr int kRespiroCelula = 10;

// ---------------------------------------------------------------------------
// Tipografia
// ---------------------------------------------------------------------------

enum class Papel {
    Titulo,      // título de tela
    Subtitulo,   // cabeçalho de seção, rótulo de cartão
    Corpo,       // padrão: tabelas, botões, formulários
    Numero,      // igual a Corpo, com algarismos de largura fixa
    Legenda,     // nota, rodapé, texto secundário
};

// Fonte do papel, SEMPRE derivada da fonte do aplicativo — nunca em pt cravado.
// Quem aumenta a fonte no Windows para enxergar espera que o app acompanhe, e
// um "font-size: 10pt" no QSS quebraria essa promessa em silêncio.
QFont fonte(Papel papel);

// ---------------------------------------------------------------------------
// Densidade de lista
// ---------------------------------------------------------------------------

// Respiro e acabamento de uma QTableView ou QTreeView: altura de linha derivada
// da fonte, sem grade, sem moldura, sem cabeçalho de linha.
//
// Existe como função em C++, e não como regra no .qss, porque nada disto é
// propriedade de folha de estilo: altura de linha é `defaultSectionSize` no
// QHeaderView, e a grade é uma flag da view. Tentar no QSS falharia em
// silêncio, que é o pior jeito de falhar.
//
// Não mexe em seleção nem em ordenação — isso é comportamento, e fica onde a
// tela decide. Chamar depois de `setupUi` e antes de instalar modelo.
void ajustarLista(QAbstractItemView* v);

// Faz `coluna` absorver a largura que sobra, e deixa as outras do tamanho do
// conteúdo (ainda arrastáveis pelo usuário).
//
// Sem isto o comportamento padrão entrega a sobra à ÚLTIMA coluna, que nestas
// tabelas é sempre a menos importante: numa janela de 1600 px a coluna "Tipo"
// ficava com 800 px de vazio enquanto "Atividade" cortava o título no meio.
// E numa janela estreita nada encolhe — a tabela só ganha uma barra de rolagem
// horizontal, e o texto que importa é o primeiro a sair de vista.
//
// Chamar DEPOIS de `setModel`: um cabeçalho sem colunas ignora o pedido.
void esticarColuna(QAbstractItemView* v, int coluna);

// Reserva, num rótulo que quebra linha, a altura do MAIOR texto que ele pode
// vir a ter — e não a do texto que ele tem agora.
//
// O QUE ISTO CONSERTA. Um QLabel com `setWordWrap(true)` informa ao layout uma
// altura mínima de poucas linhas, seja qual for o texto. Quando o texto muda
// para um mais longo, o rótulo passa a pedir mais altura do que pediu quando a
// janela nasceu — e o layout, que não pode crescer a janela já aberta, tira a
// altura de quem tem o menor mínimo. Foi o que quebrou o diálogo de Opções: ao
// ligar a rotina automática o recado ia de duas para cinco linhas, e as duas
// linhas do formulário acima colapsavam uma sobre a outra.
//
// A CURA É RESERVAR ANTES. Medindo todos os textos possíveis na largura atual
// e fixando o mínimo no maior deles, mudar o texto deixa de mexer no layout:
// não há o que redistribuir, porque o espaço já está lá. Recalcula a cada
// mudança de largura do rótulo, que é quando a quebra de linha muda.
//
// `textos` são as variantes que o rótulo pode exibir — o chamador é quem as
// conhece. Uma lista vazia não faz nada.
void reservarAltura(QLabel* r, const QStringList& textos);

// ---------------------------------------------------------------------------
// Cores com significado
// ---------------------------------------------------------------------------
//
// Não decoram: dizem "isto está atrasado". A paleta do sistema não tem papel
// para isso, então são as únicas cores nomeadas fora da QPalette — e, como
// tudo aqui, existem em duas versões, porque o vermelho que se lê sobre branco
// some sobre #15181D.
namespace cor {
QColor atrasado();   // prazo vencido
QColor urgente();    // vence em até 2 dias / prova nos próximos 7
QColor inferido();   // dado deduzido, não cadastrado pelo professor
QColor apagado();    // concluído ou já passado: presente, sem peso
QColor sucesso();    // baixado, disponível offline
} // namespace cor

} // namespace sigaa::ui::tema
