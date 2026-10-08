#include "core/parse/NotasParser.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace sigaa::parse {
namespace {

std::string valorDoInput(const html::Document& doc, const std::string& id) {
    const auto n = doc.selectFirst("input#" + id);
    return n ? html::trim(n.attr("value")) : std::string();
}

std::optional<int> inteiroBr(const std::string& s) {
    const std::string t = html::trim(s);
    if (t.empty()) return std::nullopt;
    char* fim = nullptr;
    const long v = std::strtol(t.c_str(), &fim, 10);
    if (fim == t.c_str()) return std::nullopt;
    return static_cast<int>(v);
}

} // namespace

std::optional<double> numeroBr(const std::string& s) {
    std::string t = html::trim(s);
    if (t.empty()) return std::nullopt;
    for (char& c : t) {
        if (c == ',') c = '.';
    }
    char* fim = nullptr;
    const double v = std::strtod(t.c_str(), &fim);
    if (fim == t.c_str()) return std::nullopt;
    return v;
}

ResultadoNotas parseNotas(const html::Document& doc, const std::string& idTurma,
                          const std::string& turmaNome) {
    ResultadoNotas out;
    out.notas.idTurma = idTurma;
    out.notas.turmaNome = turmaNome;

    // Guarda: a tabela de relatório com a linha de avaliações. O <caption>
    // "Alunos Matriculados" sozinho não basta — outras listagens do SIGAA o
    // usam também.
    if (!doc.selectFirst("div.notas table.tabelaRelatorio") || !doc.selectFirst("tr#trAval")) {
        return out;
    }
    out.pareceNotas = true;

    // --- estrutura, pelo cabeçalho -----------------------------------------
    // Cada coluna de dado do corpo, na ordem: avaliação (índice em
    // `avaliacoes` da unidade) ou nota da unidade (-1).
    struct Coluna {
        int unidade;
        int avaliacao;
    };
    std::vector<Coluna> colunas;

    UnidadeNota atual;
    atual.numero = 1;
    for (const auto& th : doc.select("tr#trAval th")) {
        const std::string id = th.attr("id");
        if (id.rfind("aval_", 0) == 0) {
            const std::string chave = id.substr(5);
            AvaliacaoNota a;
            a.abrev = valorDoInput(doc, "abrevAval_" + chave);
            if (a.abrev.empty()) a.abrev = html::trim(th.text());
            a.denominacao = valorDoInput(doc, "denAval_" + chave);
            a.peso = numeroBr(valorDoInput(doc, "pesoAval_" + chave));
            a.notaMaxima = numeroBr(valorDoInput(doc, "notaAval_" + chave));
            colunas.push_back({atual.numero, static_cast<int>(atual.avaliacoes.size())});
            atual.avaliacoes.push_back(std::move(a));
        } else if (id == "unid") {
            const std::string tipo = valorDoInput(doc, "tipoUnid" + std::to_string(atual.numero));
            atual.metodo = tipo.empty() ? 0 : tipo[0];
            colunas.push_back({atual.numero, -1});
            out.notas.unidades.push_back(std::move(atual));
            atual = UnidadeNota{};
            atual.numero = static_cast<int>(out.notas.unidades.size()) + 1;
        }
    }

    // --- valores, pela linha do aluno ----------------------------------------
    for (const auto& tr : doc.select("div.notas tbody tr")) {
        const auto tds = tr.select("td");
        // matrícula + nome + colunas das unidades + 4 finais
        if (tds.size() < colunas.size() + 6) continue;

        for (size_t i = 0; i < colunas.size(); ++i) {
            const auto v = numeroBr(tds[2 + i].text());
            auto& u = out.notas.unidades[static_cast<size_t>(colunas[i].unidade - 1)];
            if (colunas[i].avaliacao < 0) {
                u.nota = v;
            } else {
                u.avaliacoes[static_cast<size_t>(colunas[i].avaliacao)].nota = v;
            }
        }
        const size_t f = 2 + colunas.size();
        out.notas.reposicao = numeroBr(tds[f].text());
        out.notas.resultado = numeroBr(tds[f + 1].text());
        out.notas.faltas = inteiroBr(tds[f + 2].text());
        out.notas.situacao = html::trim(tds[f + 3].text());
        break;   // a página do aluno tem só a linha dele
    }

    return out;
}

} // namespace sigaa::parse
