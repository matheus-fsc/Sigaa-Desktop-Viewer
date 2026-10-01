#pragma once
// O kit de estudo: o caminho sem MCP (docs/MCP.md §6).
//
// Chat na web (ChatGPT, claude.ai, Gemini web) não roda servidor local. O que
// dá para fazer por ele é juntar numa pasta só o que o servidor entregaria —
// o turma.md, a matéria da prova, os PDFs dela e as instruções — para o aluno
// arrastar UMA pasta em vez de vinte arquivos.
//
// É ação do próprio aluno (botão ou comando), não de um agente: não passa pelo
// consentimento do MCP e não entra na auditoria.

#include <string>

#include "core/store/Database.h"

namespace sigaa::mcp {

struct Kit {
    bool ok{false};
    std::string erro;
    std::string pasta;          // onde ficou
    int copiados{0};            // arquivos do professor copiados
    int faltando{0};            // da matéria, mas ainda não baixados
};

// `prova` vazia = a turma inteira. `destino` vazio =
// <materiais>/<Turma>/kit-para-ia[-<prova>].
Kit exportarKit(store::Database& db, const std::string& materiais, const std::string& turma,
                const std::string& prova, const std::string& destino = {});

} // namespace sigaa::mcp
