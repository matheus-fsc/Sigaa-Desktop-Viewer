#pragma once
// Onde ficam os materiais baixados, para quem não é a UI.
//
// A UI pergunta ao Qt (`QStandardPaths::DocumentsLocation`) e grava em
// <Documentos>/SIGAA/<Turma>/. O servidor MCP roda dentro do sigaa-cli, sem
// Qt, e precisa achar a MESMA pasta. Na prática quem registra o servidor passa
// `--materiais` com o caminho que a UI usa (docs/MCP.md, D3); isto é o recuo
// para quando ninguém passou.

#include <string>

namespace sigaa::util {

// A pasta de documentos do usuário, em UTF-8:
//   - Linux: $XDG_DOCUMENTS_DIR, senão a linha XDG_DOCUMENTS_DIR de
//     ~/.config/user-dirs.dirs (é ela que diz "Documentos" num sistema em
//     português), senão ~/Documents;
//   - Windows: %USERPROFILE%\Documents (uma pasta redirecionada pelo OneDrive
//     escapa daqui — por isso o `--materiais` explícito é o caminho normal);
//   - macOS: ~/Documents.
// Vazia quando nem a pasta pessoal se descobre.
std::string pastaDocumentos();

// <Documentos>/SIGAA, a base que a UI usa para os materiais.
std::string pastaMateriaisPadrao();

} // namespace sigaa::util
