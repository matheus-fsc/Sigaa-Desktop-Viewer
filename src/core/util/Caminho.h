#pragma once
// A ponte entre `std::string` (que neste projeto é sempre UTF-8) e
// `std::filesystem::path` (que no Windows não é).
//
// Mora num arquivo próprio porque a regra vale para o projeto inteiro e já
// custou um bug: `path(std::string)` no Windows lê os bytes na code page ANSI,
// então "PROJETO E ANÁLISE" (C3 81 em UTF-8) criou uma segunda pasta
// "PROJETO E ANÃLISE" ao lado da certa, com metade dos downloads em cada uma.
// `path::string()` faz o caminho de volta e achata o acento que não couber.

#include <filesystem>
#include <string>
#include <string_view>

namespace sigaa::util {

// UTF-8 -> path. Tolera receber windows-1252: no Windows o `argv` do CLI chega
// na code page ANSI, e o construtor de char8_t com bytes inválidos ABORTA o
// processo — um crash sem mensagem por causa de um acento no nome da pasta.
std::filesystem::path deUtf8(std::string_view s);

// path -> UTF-8.
std::string paraUtf8(const std::filesystem::path& p);

// Nome de arquivo/pasta que o sistema aceita: troca por "-" o que é proibido.
// O SIGAA usa "/" em nome de turma ("TÓPICOS I/II"), e isso viraria subpasta.
std::string nomeSeguro(std::string_view s);

// Um caminho dentro de `dir` que ainda não existe: "apostila.pdf",
// "apostila (2).pdf"... Sobrescrever calado perderia a versão que o aluno já
// anotou, e o SIGAA reaproveita nome de arquivo entre semestres.
std::filesystem::path caminhoLivre(const std::filesystem::path& dir,
                                   const std::string& nomeUtf8);

} // namespace sigaa::util
