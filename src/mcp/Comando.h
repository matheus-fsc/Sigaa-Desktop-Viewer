#pragma once
// `sigaa-cli mcp` (e `SIGAA-Viewer.AppImage mcp`): o servidor MCP para
// agentes de IA e o que o administra (docs/MCP.md).
//
// Mora na biblioteca, e não no CLI, porque o AppImage só expõe o sigaa-ui:
// ele também precisa atender `mcp`, sem abrir janela nenhuma.
//
//   sigaa-cli mcp [--banco <db>] [--materiais <pasta>]   serve pelo stdio
//   sigaa-cli mcp permitir <categoria>...                 libera (leitura,
//   sigaa-cli mcp bloquear <categoria>...                 arquivos, escrita,
//   sigaa-cli mcp estado                                  rede, ou "todas")
//   sigaa-cli mcp instalar <cliente> [--imprimir]         registra no agente
//   sigaa-cli mcp remover <cliente>                       desfaz o registro
//   sigaa-cli mcp clientes                                os agentes detectados
//   sigaa-cli mcp kit <turma> [<prova>] [--destino <dir>] pasta para chat web

namespace sigaa::mcp {

// argv[1] == "mcp". Devolve o código de saída do processo.
int comando(int argc, char** argv);

} // namespace sigaa::mcp
