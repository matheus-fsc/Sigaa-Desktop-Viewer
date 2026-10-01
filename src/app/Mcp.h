#pragma once
// `sigaa-cli mcp`: o servidor MCP para agentes de IA (docs/MCP.md).
//
//   sigaa-cli mcp [--banco <db>] [--materiais <pasta>]   serve pelo stdio
//   sigaa-cli mcp permitir <categoria>...                 libera (leitura,
//   sigaa-cli mcp bloquear <categoria>...                 arquivos, escrita,
//   sigaa-cli mcp estado                                  rede, ou "todas")
//   sigaa-cli mcp instalar <cliente> [--imprimir]         registra no agente
//   sigaa-cli mcp remover <cliente>                       desfaz o registro

namespace sigaa::app {

int cmdMcp(int argc, char** argv);

} // namespace sigaa::app
