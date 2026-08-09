#pragma once
// Emulação da navegação JSF/RichFaces do SIGAA.
//
// O SIGAA não tem links de verdade: um "clique" é uma chamada
//
//     jsfcljs(document.getElementById('<formId>'), {'k1':'v1','k2':'v2'}, '')
//
// que injeta os pares como <input hidden> no <form> e o submete.
// Reproduzir isso é POSTar, para o action do form:
//
//     <inputs próprios do form>  +  <pares do jsfcljs>
//
// Verificado empiricamente — ver docs/RECON.md §1.3. Os forms do portal têm
// só 2 inputs (o nome do form e o javax.faces.ViewState), então o corpo típico
// tem 4 campos. NÃO é preciso serializar a página inteira.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/parse/Html.h"

namespace sigaa::jsf {

// Os pares extraídos do literal de objeto do jsfcljs, em ordem de aparição.
using Params = std::vector<std::pair<std::string, std::string>>;

// Um <form> JSF já digerido.
struct Form {
    std::string id;
    std::string action;                 // caminho relativo, ex. "/sigaa/portais/discente/discente.jsf"
    std::string viewState;              // valor de javax.faces.ViewState
    Params fields;                      // todos os inputs/selects/textareas com name

    bool valid() const { return !id.empty() && !action.empty(); }

    // Monta o corpo application/x-www-form-urlencoded.
    // `extra` sobrescreve campos de mesmo nome (é o que o jsfcljs faz).
    std::string buildPostBody(const Params& extra = {}) const;
};

// Um "link" clicável do SIGAA: o form alvo + os params que o jsfcljs injeta.
struct Command {
    std::string formId;
    Params params;
    std::string label;                  // texto do <a>, para debug/log
};

// --- parsing -------------------------------------------------------------

// Lê um <form> pelo id. Retorna nullopt se não existir.
std::optional<Form> parseForm(const html::Document& doc, std::string_view formId);

// Lê um <form> a partir do nó do próprio form.
std::optional<Form> parseForm(const html::Node& formNode);

// Extrai o literal de objeto de uma chamada jsfcljs dentro de um onclick.
// Aceita as duas formas vistas no SIGAA:
//   jsfcljs(document.getElementById('f'),{'a':'b'},'')
//   var a=function(){...};var b=function(){...jsfcljs(...)...};return ...
Params parseJsfcljsParams(std::string_view onclick);

// Nome do form referenciado pelo getElementById dentro do onclick.
std::string parseJsfcljsFormId(std::string_view onclick);

// Varre o documento e devolve todo <a onclick> que dispara um jsfcljs.
std::vector<Command> findCommands(const html::Document& doc);

// Atalho: encontra o Command cujo label casa com `substring` (case-sensitive).
std::optional<Command> findCommandByLabel(const html::Document& doc,
                                          std::string_view substring);

// --- codificação ---------------------------------------------------------

std::string urlEncode(std::string_view s);

} // namespace sigaa::jsf
