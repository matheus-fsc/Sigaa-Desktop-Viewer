#include "core/parse/PortalParser.h"

#include <algorithm>
#include <functional>
#include <regex>

#include "core/jsf/JsfForm.h"

namespace sigaa::parse {
namespace {

// Pega o valor de uma chave nos params do jsfcljs.
std::string param(const jsf::Params& p, std::string_view key) {
    for (const auto& [k, v] : p) {
        if (k == key) return v;
    }
    return {};
}

jsf::Params paramsDe(const html::Node& a) {
    return jsf::parseJsfcljsParams(a.attr("onclick"));
}

// "ADMINISTRAÇÃO (2026.2)" -> {"ADMINISTRAÇÃO", "2026.2"}
std::pair<std::string, std::string> separaNomePeriodo(std::string_view s) {
    static const std::regex re(R"(^(.*?)\s*\((\d{4}\.\d)\)\s*$)");
    std::match_results<std::string_view::const_iterator> m;
    if (std::regex_match(s.begin(), s.end(), m, re)) {
        return {html::trim(m[1].str()), m[2].str()};
    }
    return {html::trim(s), {}};
}

} // namespace

// ---------------------------------------------------------------------------

DateTime parseDataHora(std::string_view s) {
    DateTime dt;
    static const std::regex re(R"((\d{2})/(\d{2})/(\d{4})(?:\s+(\d{1,2}):(\d{2}))?)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_search(s.begin(), s.end(), m, re)) return dt;

    dt.day = std::stoi(m[1].str());
    dt.month = std::stoi(m[2].str());
    dt.year = std::stoi(m[3].str());
    if (m[4].matched) {
        dt.hour = std::stoi(m[4].str());
        dt.minute = std::stoi(m[5].str());
        dt.hasTime = true;
    }
    return dt;
}

std::string hashAtualizacao(std::string_view data, std::string_view idTurma,
                            std::string_view texto) {
    // Não é criptografia — só precisa ser estável e barato para dedupe.
    std::string chave;
    chave.reserve(data.size() + idTurma.size() + texto.size() + 2);
    chave.append(data).push_back('|');
    chave.append(idTurma).push_back('|');
    chave.append(texto);

    const size_t h = std::hash<std::string>{}(chave);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016zx", h);
    return buf;
}

// ---------------------------------------------------------------------------

std::vector<Turma> parseTurmas(const html::Document& doc) {
    std::vector<Turma> out;

    // Os <td id="linha_<idTurma>"> vêm na mesma ordem das linhas de turma e
    // são a forma mais estável de recuperar o idTurma (RECON §1.10).
    std::vector<std::string> ids;
    for (const auto& td : doc.select("td[id]")) {
        const std::string id = td.attr("id");
        if (id.rfind("linha_", 0) == 0) ids.push_back(id.substr(6));
    }

    size_t i = 0;
    for (const auto& form : doc.select("form")) {
        const std::string fid = form.attr("id");
        if (fid.rfind("form_acessarTurmaVirtual", 0) != 0) continue;

        const auto a = form.selectFirst("a[onclick]");
        if (!a) continue;

        Turma t;
        t.frontEndId = param(paramsDe(a), "frontEndIdTurma");
        if (t.frontEndId.empty()) continue;

        auto [nome, periodo] = separaNomePeriodo(a.text());
        t.nome = nome;
        t.periodo = periodo;
        if (i < ids.size()) t.idTurma = ids[i];
        ++i;

        out.push_back(std::move(t));
    }

    // Segunda passada: local, horário e PERÍODO — todos fora do <form>.
    //
    // O período NÃO está no link da turma (o texto é só "ADMINISTRAÇÃO"); ele
    // vive numa linha-cabeçalho <td colspan> com "2026.2" antes do bloco de
    // turmas. Sem ele, `anoDoPeriodo()` devolve 0, as datas do painel de
    // avaliações ficam inválidas e são descartadas em silêncio — o calendário
    // perde todas as provas cadastradas formalmente e fica só com a heurística.
    static const std::regex rePeriodo(R"(^\s*\d{4}\.\d\s*$)");
    std::string periodoAtual;
    size_t k = 0;

    for (const auto& tr : doc.select("tr")) {
        auto tds = tr.select("td");
        if (tds.size() == 1) {
            const std::string t = tds[0].text();
            if (std::regex_match(t, rePeriodo)) periodoAtual = html::trim(t);
            continue;
        }
        if (!tr.selectFirst("form[id^='form_acessarTurmaVirtual']")) continue;
        if (k >= out.size()) break;

        auto infos = tr.select("td.info");
        if (infos.size() > 0) out[k].local = infos[0].text();
        if (infos.size() > 1) out[k].horario = infos[1].text();
        if (out[k].periodo.empty()) out[k].periodo = periodoAtual;
        ++k;
    }

    return out;
}

// ---------------------------------------------------------------------------

std::vector<Atualizacao> parseAtualizacoes(const html::Document& doc) {
    std::vector<Atualizacao> out;

    for (const auto& tbl : doc.select("#formAtualizacoesTurmas .rotator table")) {
        auto linhas = tbl.select("tr");
        if (linhas.size() < 2) continue;

        const auto a = linhas[0].selectFirst("a[onclick]");
        if (!a) continue;

        Atualizacao u;
        u.idTurma = param(paramsDe(a), "idTurma");

        auto [nome, periodo] = separaNomePeriodo(a.text());
        u.turmaNome = nome;

        const std::string cabecalho = linhas[0].text();
        u.data = parseDataHora(cabecalho);
        u.texto = linhas[1].text();

        u.hash = hashAtualizacao(u.data.toIso(), u.idTurma, u.texto);
        out.push_back(std::move(u));
    }
    return out;
}

// ---------------------------------------------------------------------------

std::vector<Atividade> parseAtividades(const html::Document& doc) {
    std::vector<Atividade> out;

    const auto form = [&] {
        for (const auto& f : doc.select("form")) {
            if (f.attr("id") == "formAtividades") return f;
        }
        return html::Node{};
    }();
    if (!form) return out;

    std::string periodoAtual;

    for (const auto& tr : form.select("tr")) {
        // Linha separadora de período: um único <td colspan> com "2026.2".
        auto tds = tr.select("td");
        if (tds.size() == 1) {
            const std::string t = tds[0].text();
            if (std::regex_match(t, std::regex(R"(\d{4}\.\d)"))) periodoAtual = t;
            continue;
        }
        if (tds.size() < 3) continue;

        const auto link = tr.selectFirst("a[onclick]");
        if (!link) continue;
        const auto ps = paramsDe(link);

        Atividade at;
        at.atividadeId = param(ps, "id");
        at.idTurma = param(ps, "idTurma");
        if (at.atividadeId.empty()) continue;

        at.periodo = periodoAtual;
        at.titulo = link.text();
        at.prazo = parseDataHora(tds[1].text());

        // Ícone -> status (RECON §3.1)
        if (const auto img = tds[0].selectFirst("img")) {
            const std::string src = img.attr("src");
            if (src.find("check.png") != std::string::npos)
                at.status = StatusAtividade::Concluida;
            else if (src.find("prova_semana.png") != std::string::npos)
                at.status = StatusAtividade::NaSemana;
            else
                at.status = StatusAtividade::Pendente;
        }

        // "Tarefa:" vem num <strong>; o nome da turma é o texto antes do <br>.
        if (const auto s = tds[2].selectFirst("strong")) {
            std::string tipo = s.text();
            if (!tipo.empty() && tipo.back() == ':') tipo.pop_back();
            at.tipo = tipo;
        }
        {
            std::string bloco = tds[2].text();
            const std::string marcador = at.tipo.empty() ? std::string{} : at.tipo + ":";
            const size_t corte = marcador.empty() ? std::string::npos : bloco.find(marcador);
            at.turmaNome = html::trim(corte == std::string::npos ? bloco
                                                                 : bloco.substr(0, corte));
        }

        out.push_back(std::move(at));
    }
    return out;
}

// ---------------------------------------------------------------------------

Snapshot parsePortal(const html::Document& doc) {
    Snapshot s;
    s.turmas = parseTurmas(doc);
    s.atualizacoes = parseAtualizacoes(doc);
    s.atividades = parseAtividades(doc);

    // Preenche o nome da turma nas atividades a partir do cadastro, quando o
    // texto do portal vier truncado ou vazio.
    for (auto& a : s.atividades) {
        if (!a.turmaNome.empty()) continue;
        auto it = std::find_if(s.turmas.begin(), s.turmas.end(),
                               [&](const Turma& t) { return t.idTurma == a.idTurma; });
        if (it != s.turmas.end()) a.turmaNome = it->nome;
    }
    return s;
}

} // namespace sigaa::parse
