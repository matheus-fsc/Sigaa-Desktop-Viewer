#pragma once
// Notícias da Turma Virtual, em três páginas (estrutura em
// tests/fixtures/noticias_*.html, capturadas do site real):
//
//  - página inicial da turma: `div#ultimaNoticia`, com título e data/hora no
//    <h4>, o texto em `span.conteudoNoticia` e "Cadastrado por" num <small>.
//    Sem `id`, mas com o texto — e a coleta já abre essa página de qualquer
//    jeito, então é a notícia mais nova sem custo nenhum.
//  - aba Notícias (/sigaa/ava/NoticiaTurma/listar.jsf): `table.listing` com
//    Título | Data | ícone "Visualizar", cujo jsfcljs leva o `id` da notícia.
//  - detalhe ("Visualização de Notícia"): Título, Data com hora e o texto em
//    `td.conteudoNoticia`.
//
// Parsers puros sobre HTML, como os outros: navegar e decidir o que buscar
// mora em sync/.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ItemNoticia {
    std::string id;
    std::string titulo;
    DateTime data;   // só o dia: a lista não mostra a hora
};

struct ListaNoticias {
    std::vector<ItemNoticia> itens;
    // "Nenhum item foi encontrado": turma sem notícia, e não falha de parse.
    bool vazioConfirmado{false};
    // A página tem cara da aba Notícias? Sessão expirada devolve 200 com
    // outra tela, e "sem notícias" seria mentir a partir de uma falha.
    bool pareceAbaNoticias{false};
};

ListaNoticias parseListaNoticias(const html::Document& doc);

struct DetalheNoticia {
    bool pareceDetalhe{false};
    std::string titulo;
    DateTime data;
    std::string conteudoHtml;   // já limpo
};

DetalheNoticia parseDetalheNoticia(const html::Document& doc);

// A "Última Notícia" da página inicial da turma. nullopt quando a turma não
// tem nenhuma (o bloco não aparece).
std::optional<Noticia> parseUltimaNoticia(const html::Document& docTurma,
                                          const std::string& idTurma,
                                          const std::string& turmaNome);

// Reduz o HTML do editor do SIGAA ao que se lê: <p>, <br>, <strong>/<b>,
// <em>/<i>, <u>, listas e <a href>. Fica o texto de todo o resto (os <span>
// com fonte e tamanho que o editor cola em cada parágrafo, <div>, <font>), e
// cai todo atributo que não seja o href de um link http(s). Exposto para
// teste.
std::string limparHtmlNoticia(std::string_view html);

// O texto de uma notícia sem marcação, numa linha só — para o aviso, que não
// renderiza HTML. Entende as entidades que a limpeza deixa (&nbsp;, &amp;...).
std::string textoDaNoticia(std::string_view htmlLimpo);

// Chave que junta a mesma notícia vinda da lista e da página da turma:
// turma, título normalizado e o DIA (a lista não tem hora).
std::string chaveNoticia(const std::string& idTurma, const std::string& titulo,
                         const DateTime& data);

} // namespace sigaa::parse
