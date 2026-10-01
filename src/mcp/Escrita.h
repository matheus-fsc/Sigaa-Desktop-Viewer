#pragma once
// As ferramentas de devolução (docs/MCP.md §5): o agente grava no app o que
// observou estudando com o aluno — tempo, desempenho por tópico, pontos de
// dificuldade — e lê de volta o que já foi gravado (`meu_progresso`), para
// uma conversa nova não começar do zero.
//
// Gravar exige a permissão `escrita`; ler o progresso, só `leitura`. Tudo
// é validado antes de chegar ao banco, com mensagem que o agente entende e
// pode corrigir sozinho: um agente em laço gravando lixo é o risco aqui.

#include <vector>

#include "mcp/Leitura.h"

namespace sigaa::mcp {

const std::vector<Ferramenta>& ferramentasDeEscrita();

// Esquece o limite de gravações por minuto. Só para testes.
void zerarLimiteDeEscrita();

} // namespace sigaa::mcp
