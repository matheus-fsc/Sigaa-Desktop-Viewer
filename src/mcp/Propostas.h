#pragma once
// O agente mexe no plano de estudo, e o aluno decide quanto (docs/MCP.md §5).
//
// Por tipo de mudança — horas por dia, dificuldade, sessões extras, pontos de
// foco — o aluno escolhe um modo:
//
//   - Não pode: a ferramenta recusa, e o agente fica sabendo por quê;
//   - Propõe:   a mudança vira uma proposta pendente, que aparece em
//               Progresso para o aluno aceitar, ajustar ou recusar;
//   - Aplica:   muda na hora, e a proposta fica como "aplicada", que o aluno
//               pode desfazer.
//
// E um teto de horas por dia: o agente nunca propõe nem aplica mais que isso.
//
// A regra mora aqui, e não na UI nem nas ferramentas, porque as duas pontas
// fazem a mesma coisa: o MCP aplica quando o modo deixa, a UI aplica quando o
// aluno aceita, e mudar o modo para "Aplica" aplica as pendentes. Uma conta
// só, testável sem Qt.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/estudo/Registros.h"
#include "core/store/Database.h"

namespace sigaa::mcp {

enum class Modo { NaoPode, Propoe, Aplica };

// Guardado em `meta` como "mcp.modo.<tipo>". Padrões: horas e dificuldade
// propõem (mexem no plano todo); sessões extras e pontos de foco aplicam e
// avisam (pequenos, e desfazer custa um clique).
Modo modo(store::Database& db, estudo::TipoProposta t);
bool gravarModo(store::Database& db, estudo::TipoProposta t, Modo m);

// Teto de horas por dia, em horas ("mcp.teto"). Padrão 8.
int tetoHoras(store::Database& db);
bool gravarTeto(store::Database& db, int horas);

// O valor de agora daquilo que a proposta muda: minutos disponíveis no dia,
// dificuldade (1..3). Sessão e foco não têm valor anterior: 0.
int valorAtual(store::Database& db, const estudo::Proposta& q);

// Grava uma proposta nova vinda do agente, conforme o modo do tipo dela.
// Uma pendente para a mesma coisa (o mesmo dia, a mesma turma, o mesmo
// tópico) é trocada pela nova: o aluno responde à opinião mais recente.
// Devolve o id, ou 0 com `erro` dizendo por quê.
std::int64_t propor(store::Database& db, estudo::Proposta q, std::int64_t agora,
                    std::string* erro, bool* aplicada = nullptr);

// Aplica `valor` no plano e grava a proposta como `como` (Aceita ou
// Aplicada). `q` volta atualizada.
bool aceitar(store::Database& db, estudo::Proposta& q, int valor,
             estudo::EstadoProposta como, std::int64_t agora, std::string* erro = nullptr);
// Volta o plano ao valor de antes da proposta.
bool desfazer(store::Database& db, estudo::Proposta& q, std::int64_t agora);
bool recusar(store::Database& db, estudo::Proposta& q, std::int64_t agora);
// Recusada ou desfeita volta a pendente.
bool reabrir(store::Database& db, estudo::Proposta& q);

// Ao mudar um tipo para "Aplica": as pendentes dele valem agora (as de horas
// acima do teto ficam esperando). Devolve quantas.
int aplicarPendentes(store::Database& db, estudo::TipoProposta t, std::int64_t agora);

// A mudança do agente que ainda vale para um campo da página Horas e
// dificuldade (o dia `diaSemana` das horas, ou a dificuldade de `idTurma`):
// a última aceita ou aplicada, se o valor de agora ainda é o dela. Se o
// aluno mexeu depois, a mudança deixou de ser do agente.
std::optional<estudo::Proposta> mudancaValendo(const std::vector<estudo::Proposta>& todas,
                                               estudo::TipoProposta t, const std::string& idTurma,
                                               int diaSemana, int valorAtual);

// Pendentes que o aluno vê: as de tipos que o agente ainda pode mudar. As de
// um tipo em "Não pode" ficam guardadas, escondidas.
int pendentesVisiveis(store::Database& db);

} // namespace sigaa::mcp
