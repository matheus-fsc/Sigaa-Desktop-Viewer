#pragma once
// Aba "Participantes" da Turma Virtual — /sigaa/ava/participantes.jsf
//
// Estrutura confirmada contra a captura de rede em docs/RECON.md §1.6.3: duas
// `table.participantes`, uma por bloco, precedidas de um <legend> com a
// contagem ("Docentes (1)", "Discentes (31)").
//
// TRES ARMADILHAS que a captura revelou, e que ditam o desenho deste parser:
//
//  1. A UNIDADE E A CELULA, NAO A LINHA. O bloco de discentes e desenhado em
//     DUAS COLUNAS de participantes: cada <tr> guarda duas pessoas, em 6 <td>.
//     Foi o que enganou a primeira versao — 31 alunos em 16 linhas — e o
//     sintoma nao era lista curta, era o e-mail de um vindo com o nome do
//     seguinte grudado no fim. Cada pessoa e um <td>, reconhecido pelos
//     ROTULOS que carrega.
//
//  2. A FOTO E OPCIONAL. Eram 19 fotos para 32 pessoas. Ancorar a celula na
//     <img> perderia 13 participantes sem qualquer sinal de erro.
//
//  3. Os dois blocos tem markup DIFERENTE, inclusive na caixa do rotulo de
//     e-mail: "E-Mail:" no docente e "E-mail:" no discente. A busca e
//     case-insensitive de proposito — parear a caixa exata custaria um campo
//     vazio em metade das linhas.
//
// O papel sai da PROPRIA LINHA (tem "Matricula:"? e discente; tem
// "Departamento:"? e docente) e nao da tabela em que ela esta. E o que permite
// ler as duas tabelas com o mesmo laco: a API de html::Node nao navega para
// irmaos, entao amarrar cada tabela ao <legend> anterior nao daria.
//
// O que este parser NAO extrai, de proposito: o CPF que aparece em
// `Mensagem.show(1, '<CPF>', ...)` de cada participante, e o `idPessoa` do link
// de perfil. Ver o comentario de `Participante` em core/model/Models.h.

#include <string>
#include <vector>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ListaParticipantes {
    std::vector<Participante> participantes;

    // A pagina tem a cara da aba de participantes? Sessao expirada devolve 200
    // com a tela de login, e sem esta distincao isso viraria "turma sem
    // colegas" — uma afirmacao falsa com cara de resposta.
    bool pareceAbaParticipantes{false};

    // O que o <legend> declarou. Serve de CONFERENCIA: se o SIGAA diz 31
    // discentes e extraimos 28, o seletor quebrou e a UI precisa saber.
    // Zero = a pagina nao trouxe a contagem.
    int docentesDeclarados{0};
    int discentesDeclarados{0};
};

ListaParticipantes parseParticipantes(const html::Document& doc,
                                      const std::string& idTurma,
                                      const std::string& turmaNome);

// --- expostos para teste ---------------------------------------------------

// "Curso: X Matricula: Y E-mail: Z" + "Matr" -> "Y".
//
// O valor termina no proximo rotulo conhecido, e nao num separador fixo: o
// texto ja vem com os espacos colapsados por html::Node::text(), entao <br/> e
// <em> nao deixam marca nenhuma para cortar.
std::string campoRotulado(const std::string& texto, std::string_view rotulo);

// " Discentes (31)" -> 31. Zero se nao houver contagem.
int contagemDoLegend(std::string_view legend);

} // namespace sigaa::parse
