#pragma once
// Onde o aluno está em cada turma, de fato: faltas contra o limite e notas
// contra a média. É o ponto de partida do agente de IA (a ferramenta
// `diagnostico` do MCP) quando o período já anda e o aluno chega sem nada
// registrado no app — o caso em que "monte um plano de estudo" sem olhar
// para isto planejaria horas para uma matéria já perdida por falta.
//
// O QUE É FATO E O QUE É SUPOSIÇÃO fica separado na saída, porque decide o
// tom da conversa:
//   - faltas e limite são do SIGAA (o mapa de frequência e a regra de 75%);
//   - a nota de cada unidade é do SIGAA;
//   - a média mínima é a da UNIFEI: 60% da nota (6,0 em 10);
//   - a MÉDIA FINAL como média aritmética das unidades é suposição: a página
//     não diz como as unidades se combinam. Quem confirma é o aluno.
//   - um 0,0 lançado pode ser prova perdida ou lançamento provisório do
//     professor. Vai em `zeros`, para o agente PERGUNTAR, e não entra no
//     risco como se fosse certo.

#include <optional>
#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::estudo {

// UNIFEI: aprovação com 60% da nota, 6,0 na escala de 10 do SIGAA.
// Outra instituição grava a sua em `estudo.media_minima` no meta.
constexpr double kMediaMinimaPadrao = 6.0;

enum class RiscoFalta {
    SemDados,    // o professor não lançou frequência
    Folgado,
    Atencao,     // restam poucas faltas (um quarto do limite, no mínimo 2)
    NoLimite,    // nenhuma falta a mais
    Reprovado,   // passou do limite
};

struct SituacaoFaltas {
    RiscoFalta risco{RiscoFalta::SemDados};
    int faltas{0};
    int limite{0};
    int restam{0};              // faltas que ainda cabem; negativo = passou
    int aulasRestantes{0};      // pela carga horária, ainda sem registro
    int diasNaoRegistrados{0};  // dias que o professor deixou em branco
};

SituacaoFaltas situacaoFaltas(const Frequencia* f);

enum class RiscoNota {
    SemNota,      // nenhuma unidade com nota
    Folgado,      // precisa de até a média mínima nas que faltam
    Atencao,      // precisa de mais que a mínima, até 8
    Dificil,      // precisa de mais de 8
    SoReposicao,  // precisa de mais de 10: só com reposição/substitutiva
    Aprovado,     // todas as unidades lançadas, média >= mínima
    Abaixo,       // todas lançadas, média < mínima
};

struct SituacaoNotas {
    RiscoNota risco{RiscoNota::SemNota};
    int unidades{0};
    int lancadas{0};
    std::optional<double> mediaParcial;   // das unidades lançadas
    std::optional<double> precisa;        // média nas que faltam para a mínima
    // Avaliações com 0,0 lançado: "P1 (Unid. 1)". Ver o topo.
    std::vector<std::string> zeros;
    // Só zeros lançados: o risco pode ser lançamento provisório.
    bool soZeros{false};
};

SituacaoNotas situacaoNotas(const Notas* n, double mediaMinima = kMediaMinimaPadrao);

const char* nomeRisco(RiscoFalta r);   // "sem_dados", "folgado", ...
const char* nomeRisco(RiscoNota r);    // "sem_nota", "folgado", ...

} // namespace sigaa::estudo
