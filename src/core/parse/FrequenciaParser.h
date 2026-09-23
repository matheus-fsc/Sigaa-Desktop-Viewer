#pragma once
// Mapa de frequência do aluno — /sigaa/ava/FrequenciaAluno/mapa.jsf
//
// Chega-se nele pelo item "Frequência" do painel "Alunos" no menu da turma,
// que é um jsfcljs comum sobre `formMenu` — a mesma máquina de
// `abrirAbaPorRotulo`, sem nada de especial.
//
// A PÁGINA TEM DUAS METADES, e o parser lê as duas por razões diferentes:
//
//   1. A TABELA `table.listing`: uma linha por encontro, com data e situação
//      ("Presente", "2 Falta(s)", "Não Registrada"). Serve para MOSTRAR quais
//      dias, e é a única fonte do sinal "o professor deixou dia em branco".
//
//   2. O RODAPÉ `div.botoes-show`: os totais que o próprio SIGAA calculou —
//      presenças registradas, aulas com registro, e aulas definidas pela carga
//      horária do componente.
//
// OS TOTAIS SÃO A FONTE DA VERDADE, e não a soma das linhas. A tabela escreve
// "Presente" sem dizer de quantas aulas foi feito aquele encontro; um dia de
// dois horários seguidos vale 2. Somar linhas obrigaria a adivinhar esse
// tamanho — e a média (aulas ÷ encontros) quebra na turma que tem um encontro
// de tamanho diferente. O rodapé já traz os números exatos.
//
// A CAPTURA que guiou isto (tests/fixtures/frequencia_rede.html) fecha a conta
// e confirma o desenho: 13 linhas, 12 com registro × 2 aulas = 24 = "Aulas com
// Registro"; 9 presentes × 2 = 18 = "Presenças Registradas"; e a 13ª linha é
// "Não Registrada", que não entra em nenhum dos dois.

#include <string>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ResultadoFrequencia {
    Frequencia frequencia;

    // A resposta é MESMO o mapa de frequência? Distingue "o professor não
    // lançou nada" de "caímos noutra tela" (sessão expirada, menu diferente).
    // Sem isto, uma sessão morta viraria "parabéns, zero faltas".
    bool pareceMapaDeFrequencia{false};
};

ResultadoFrequencia parseFrequencia(const html::Document& doc,
                                    const std::string& idTurma,
                                    const std::string& turmaNome);

} // namespace sigaa::parse
