#pragma once
// "Ver Notas" da turma — o item do painel "Alunos" no menu da turma virtual.
//
// Chega-se nele como na frequência: um jsfcljs comum sobre `formMenu`
// (`abrirAbaPorRotulo`). A resposta é uma página de relatório
// (#relatorio-paisagem-container) com UMA tabela, "Alunos Matriculados", que
// para o aluno tem só a linha dele.
//
// A ESTRUTURA ESTÁ NO CABEÇALHO, não no corpo. A linha `tr#trAval` tem, na
// ordem das colunas:
//   - um <th id="aval_<id>"> por avaliação cadastrada (o texto é a sigla);
//   - um <th id="unid"> que FECHA cada unidade (a nota da unidade).
// Os detalhes de cada avaliação moram em <input type=hidden> com o mesmo id:
// abrevAval_<id>, denAval_<id>, pesoAval_<id>, notaAval_<id> (nota máxima).
// E o método de cada unidade em #tipoUnid1..3 ("P" ponderada, "A"
// aritmética, "S" soma). É dessa mesma informação que o JavaScript da página
// monta a dica "Cálculo: ((T1 * 15) + (P1 * 35))/50".
//
// O corpo segue a mesma ordem: matrícula, nome, as colunas das unidades, e
// então Reposição, Resultado, Faltas e Situação.
//
// A CAPTURA que guiou isto (tests/fixtures/notas_rede.html, 07/10/2026):
// Unid. 1 com T1 (peso 15) e P1 (peso 35), Unid. 2 sem avaliação cadastrada,
// T1 = P1 = nota da unidade = 0,0, Faltas = 0 — no mesmo dia em que o mapa de
// frequência da turma contava 20 faltas. Ver `Notas::faltas`.

#include <string>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ResultadoNotas {
    Notas notas;

    // A resposta é MESMO a planilha de notas? Sem isto, uma sessão expirada
    // viraria "nenhuma nota lançada" — que é um estado legítimo e enganaria.
    bool pareceNotas{false};
};

ResultadoNotas parseNotas(const html::Document& doc, const std::string& idTurma,
                          const std::string& turmaNome);

// "7,5" -> 7.5; "" ou lixo -> nullopt. Exposto para os testes.
std::optional<double> numeroBr(const std::string& s);

} // namespace sigaa::parse
