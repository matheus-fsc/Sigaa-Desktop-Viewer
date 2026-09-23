#pragma once
// Copiar linhas de uma lista para a área de transferência.
//
// POR QUE ISTO PRECISA EXISTIR: as tabelas do app são o resumo do semestre, e
// a coisa mais natural a fazer com um resumo é levá-lo para outro lugar — a
// planilha de estudo, a mensagem para o colega, o prompt de uma IA. Sem
// copiar, a saída é digitar de novo ou tirar print, e print não se cola numa
// planilha.
//
// O Qt NÃO traz isso pronto: `QAbstractItemView` sabe selecionar várias linhas
// e não sabe copiá-las. Ctrl+C numa QTableView do Qt não faz nada.
//
// FORMATO: TSV — colunas separadas por TAB, linhas por \n. É o que o Excel, o
// LibreOffice e o Google Sheets colam em células separadas sem perguntar nada.
// CSV exigiria decidir o que fazer com a vírgula que aparece em todo nome de
// atividade; o TAB não aparece em nenhum campo do SIGAA.

class QAbstractItemView;

namespace sigaa::ui {

// Copia as linhas selecionadas. Sem seleção, não faz nada — silenciosamente,
// porque Ctrl+C sem seleção é engano de dedo, não pedido de ajuda.
//
// Vai o que está VISÍVEL: respeita a ordem das colunas na tela, pula as
// escondidas, e usa o texto exibido, não o valor cru. Quem copia quer o que
// estava vendo — "atrasado 6 dias", não "-6".
void copiarSelecao(QAbstractItemView* v);

// Liga Ctrl+C (e o Copiar do menu de contexto) na view.
void habilitarCopia(QAbstractItemView* v);

} // namespace sigaa::ui
