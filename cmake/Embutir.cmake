# Gera um .cpp com os arquivos de uma pasta como vetores de bytes, para o
# servidor do acesso mobile servir sem ler o disco (src/web/Paginas.h).
#
# Uso: cmake -DORIGEM=<pasta> -DDESTINO=<arquivo.cpp> -P Embutir.cmake
#
# Roda como comando de build, e não no configure: assim mexer no app.js e
# compilar basta, sem lembrar de reconfigurar.

file(GLOB arquivos RELATIVE "${ORIGEM}" "${ORIGEM}/*")
list(SORT arquivos)

set(saida "// GERADO por cmake/Embutir.cmake a partir de src/web/pagina/. Não edite.\n")
string(APPEND saida "#include \"web/Paginas.h\"\n\n#include <cstring>\n\nnamespace sigaa::web {\nnamespace {\n\n")

set(tabela "")
set(i 0)
foreach(nome IN LISTS arquivos)
  if(IS_DIRECTORY "${ORIGEM}/${nome}")
    continue()
  endif()
  get_filename_component(ext "${nome}" LAST_EXT)
  string(TOLOWER "${ext}" ext)
  if(ext STREQUAL ".html")
    set(tipo "text/html; charset=utf-8")
  elseif(ext STREQUAL ".css")
    set(tipo "text/css; charset=utf-8")
  elseif(ext STREQUAL ".js")
    set(tipo "text/javascript; charset=utf-8")
  elseif(ext STREQUAL ".svg")
    set(tipo "image/svg+xml")
  elseif(ext STREQUAL ".png")
    set(tipo "image/png")
  elseif(ext STREQUAL ".webmanifest")
    set(tipo "application/manifest+json")
  else()
    message(FATAL_ERROR "Embutir: extensão sem tipo conhecido: ${nome}")
  endif()

  file(READ "${ORIGEM}/${nome}" hex HEX)
  string(LENGTH "${hex}" n)
  math(EXPR tamanho "${n} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  # Quebra de linha a cada 24 bytes, para o arquivo gerado continuar legível
  # num diff e não estourar o limite de linha de algum compilador.
  string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n    " bytes "${bytes}")

  string(APPEND saida "const unsigned char d${i}[] = {\n    ${bytes}0x00};\n\n")
  string(APPEND tabela "    {\"/${nome}\", \"${tipo}\", d${i}, ${tamanho}},\n")
  math(EXPR i "${i} + 1")
endforeach()

string(APPEND saida "const Pagina kPaginas[] = {\n${tabela}};\n\n} // namespace\n\n")
string(APPEND saida [=[
const Pagina* acharPagina(std::string_view caminho) {
    if (caminho == "/") caminho = "/index.html";
    for (const auto& p : kPaginas) {
        if (caminho == p.caminho) return &p;
    }
    return nullptr;
}

} // namespace sigaa::web
]=])

# Só regrava quando muda: um arquivo novo a cada build recompilaria o alvo
# inteiro sem motivo.
if(EXISTS "${DESTINO}")
  file(READ "${DESTINO}" antigo)
  if(antigo STREQUAL saida)
    return()
  endif()
endif()
file(WRITE "${DESTINO}" "${saida}")
