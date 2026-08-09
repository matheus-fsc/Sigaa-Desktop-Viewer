#pragma once
// Aba "Arquivos" da Turma Virtual — /sigaa/ava/ArquivoTurma/listar_discente.jsf
//
// Estrutura confirmada contra o site real em docs/RECON.md §1.6.1: uma
// `table.listing` dentro do form `formAva`, com Título | Descrição | Tópico de
// Aula | (ícone de baixar). O ícone é um jsfcljs cujo parâmetro `id` é a chave
// estável do arquivo.
//
// Duas coisas que este parser NÃO faz, de propósito:
//
//  - não baixa nada. Download é POST no `formAva` e mora em sync/, porque
//    precisa de sessão viva; parser é função pura sobre HTML.
//  - não confunde lista vazia com erro. `<p class="empty-listing">` é o estado
//    normal de várias turmas — o professor simplesmente não publicou material.
//    Tratar isso como falha encheria a UI de alarme sobre nada.

#include <string>
#include <vector>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ListaArquivos {
    std::vector<ArquivoTurma> arquivos;

    // O SIGAA disse "Nenhum item foi encontrado". Diferente de `arquivos`
    // vazio por falha de parse: um é resposta, o outro é bug nosso.
    bool vazioConfirmado{false};

    // A página tem a cara da aba de arquivos? Se não, provavelmente caímos
    // noutra tela (sessão expirada devolve 200 com HTML de login).
    bool pareceAbaArquivos{false};
};

ListaArquivos parseArquivos(const html::Document& doc, const std::string& idTurma,
                            const std::string& turmaNome);

} // namespace sigaa::parse
