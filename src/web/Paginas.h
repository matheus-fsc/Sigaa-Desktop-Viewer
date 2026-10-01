#pragma once
// As páginas do acesso mobile, EMBUTIDAS no executável.
//
// Os arquivos de src/web/pagina/ viram um vetor de bytes na compilação
// (cmake/Embutir.cmake). Servir do disco abriria duas perguntas que não
// precisam existir: "qual pasta?" (o AppImage não tem uma gravável ao lado do
// binário) e "e se alguém trocar o app.js nessa pasta?". Embutido, o que o
// celular recebe é exatamente o que foi compilado.

#include <cstddef>
#include <string_view>

namespace sigaa::web {

struct Pagina {
    const char* caminho;   // "/app.js"
    const char* tipo;      // "text/javascript; charset=utf-8"
    const unsigned char* dados;
    std::size_t tamanho;
};

// Nulo quando o caminho não é de nenhuma página. "/" é o index.html.
const Pagina* acharPagina(std::string_view caminho);

} // namespace sigaa::web
