#pragma once
// Gera um relatório HTML autocontido a partir de um Snapshot.
//
// Serve a dois propósitos: dar visibilidade imediata sem comprometer a escolha
// de framework de UI, e servir de protótipo de layout para a Fase 3.
// Nenhum recurso externo — um arquivo, abre em qualquer navegador, offline.

#include <string>

#include "core/model/Models.h"

namespace sigaa::report {

std::string renderHtml(const Snapshot& s);

} // namespace sigaa::report
