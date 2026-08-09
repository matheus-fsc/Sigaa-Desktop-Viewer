#include "core/parse/ArquivoParser.h"

#include "core/jsf/JsfForm.h"

namespace sigaa::parse {
namespace {

// O `id` avulso do jsfcljs — a chave do arquivo. O outro par do literal é o id
// do componente JSF, que é posicional; ignorá-lo é intencional (RECON §1.6.1).
std::string idDoOnclick(const std::string& onclick) {
    for (const auto& [k, v] : jsf::parseJsfcljsParams(onclick)) {
        if (k == "id") return v;
    }
    return {};
}

} // namespace

ListaArquivos parseArquivos(const html::Document& doc, const std::string& idTurma,
                            const std::string& turmaNome) {
    ListaArquivos out;

    // "Arquivos" no legend é o que distingue esta aba das outras, que
    // compartilham o mesmo form `formAva` e a mesma moldura.
    for (const auto& l : doc.select("fieldset legend")) {
        if (l.text().find("Arquivos") != std::string::npos) out.pareceAbaArquivos = true;
    }
    for (const auto& p : doc.select("p.empty-listing")) {
        (void)p;
        out.vazioConfirmado = true;
    }

    for (const auto& linha : doc.select("table.listing tbody tr")) {
        const auto tds = linha.select("td");
        if (tds.size() < 4) continue;   // linha de cabeçalho ou de paginação

        const auto a = linha.selectFirst("a[onclick]");
        if (!a) continue;
        const std::string id = idDoOnclick(a.attr("onclick"));
        // Sem `id` não há como baixar. Melhor omitir a linha do que mostrar um
        // botão que não faz nada — o usuário clicaria e concluiria que o app
        // está quebrado, sem saber que o arquivo é que não tem endereço.
        if (id.empty()) continue;

        ArquivoTurma arq;
        arq.idTurma = idTurma;
        arq.turmaNome = turmaNome;
        arq.idArquivo = id;
        arq.titulo = tds[0].text();
        arq.descricao = tds[1].text();
        arq.topico = tds[2].text();
        if (arq.titulo.empty()) continue;
        out.arquivos.push_back(std::move(arq));
    }

    return out;
}

} // namespace sigaa::parse
