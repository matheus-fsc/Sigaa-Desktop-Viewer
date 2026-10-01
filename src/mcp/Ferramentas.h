#pragma once
// Todas as ferramentas do servidor, de todas as fases (docs/MCP.md §11):
// leitura, devolução (escrita) e rede. Cada uma diz a permissão que exige;
// o servidor confere antes de rodar.

#include <string>
#include <vector>

#include "mcp/Leitura.h"

namespace sigaa::mcp {

const std::vector<Ferramenta>& todasAsFerramentas();

// Por nome; nulo se não existe.
const Ferramenta* ferramenta(const std::string& nome);

} // namespace sigaa::mcp
