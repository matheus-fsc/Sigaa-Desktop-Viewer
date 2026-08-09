#include "core/report/HtmlReport.h"

#include <algorithm>
#include <sstream>

namespace sigaa::report {
namespace {

std::string esc(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

std::string dataCurta(const DateTime& d) {
    if (!d.valid()) return "--/--";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d/%02d", d.day, d.month);
    return buf;
}

std::string hora(const DateTime& d) {
    if (!d.hasTime) return {};
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", d.hour, d.minute);
    return buf;
}

const char* classeStatus(StatusAtividade s) {
    switch (s) {
        case StatusAtividade::Concluida: return "ok";
        case StatusAtividade::NaSemana:  return "urgente";
        default:                         return "";
    }
}

const char* iconeStatus(StatusAtividade s) {
    switch (s) {
        case StatusAtividade::Concluida: return "&#10003;";  // check
        case StatusAtividade::NaSemana:  return "&#9888;";   // aviso
        default:                         return "&#183;";
    }
}

constexpr const char* kCss = R"(
:root{--bg:#faf9f7;--fg:#1e1c1a;--mut:#6f6a64;--line:#e3ded7;--card:#fff;
      --urg:#b4462f;--ok:#4a7c59;--accent:#2f4858}
@media(prefers-color-scheme:dark){:root{--bg:#16171a;--fg:#eceae7;--mut:#9a948c;
      --line:#2c2e33;--card:#1e2024;--urg:#e0745c;--ok:#7fae8b;--accent:#9fc0d4}}
*{box-sizing:border-box}
body{margin:0;padding:2.5rem 1.25rem;background:var(--bg);color:var(--fg);
     font:15px/1.55 ui-sans-serif,system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:60rem;margin:0 auto}
h1{font-size:1.5rem;margin:0 0 .25rem;letter-spacing:-.01em}
.sub{color:var(--mut);font-size:.85rem;margin-bottom:2rem}
h2{font-size:.75rem;text-transform:uppercase;letter-spacing:.09em;
   color:var(--mut);margin:2.25rem 0 .75rem;font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
      overflow:hidden}
table{width:100%;border-collapse:collapse}
td,th{padding:.6rem .85rem;text-align:left;border-bottom:1px solid var(--line);
      vertical-align:top}
tr:last-child td{border-bottom:0}
th{font-size:.7rem;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);
   font-weight:600}
.data{white-space:nowrap;font-variant-numeric:tabular-nums;color:var(--mut);width:1%}
.turma{font-size:.78rem;color:var(--mut);display:block}
.ico{width:1%;text-align:center;color:var(--mut)}
tr.urgente .ico{color:var(--urg)} tr.urgente .titulo{font-weight:600}
tr.ok .ico{color:var(--ok)} tr.ok .titulo{color:var(--mut);text-decoration:line-through}
.vazio{padding:1.1rem .85rem;color:var(--mut);font-size:.88rem}
code{font:12px ui-monospace,SFMono-Regular,Consolas,monospace;color:var(--mut)}
footer{margin-top:3rem;color:var(--mut);font-size:.76rem;border-top:1px solid var(--line);
       padding-top:1rem}
)";

} // namespace

std::string renderHtml(const Snapshot& s) {
    std::ostringstream o;

    // Atividades ordenadas por prazo — é a pergunta que o app existe pra responder.
    auto ativ = s.atividades;
    std::sort(ativ.begin(), ativ.end(),
              [](const Atividade& a, const Atividade& b) { return a.prazo < b.prazo; });

    auto atual = s.atualizacoes;
    std::sort(atual.begin(), atual.end(),
              [](const Atualizacao& a, const Atualizacao& b) { return b.data < a.data; });

    o << "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      << "<title>SIGAA — meus prazos</title><style>" << kCss << "</style></head><body><main>";

    o << "<h1>Meus prazos</h1><div class=\"sub\">"
      << ativ.size() << " atividade(s)";
    if (!s.avaliacoes.empty()) o << " &middot; " << s.avaliacoes.size() << " prova(s)";
    o << " &middot; " << s.turmas.size() << " turma(s)";
    if (s.minutosSessaoRestantes) {
        o << " &middot; sessão expira em " << *s.minutosSessaoRestantes << " min";
    }
    o << "</div>";

    // --- atividades --------------------------------------------------------
    o << "<h2>Próximos prazos</h2><div class=\"card\">";
    if (ativ.empty()) {
        o << "<div class=\"vazio\">Nenhuma atividade pendente.</div>";
    } else {
        o << "<table>";
        for (const auto& a : ativ) {
            o << "<tr class=\"" << classeStatus(a.status) << "\">"
              << "<td class=\"ico\">" << iconeStatus(a.status) << "</td>"
              << "<td class=\"data\">" << dataCurta(a.prazo);
            if (const auto h = hora(a.prazo); !h.empty()) o << "<br><small>" << h << "</small>";
            o << "</td><td><span class=\"titulo\">" << esc(a.titulo) << "</span>"
              << "<span class=\"turma\">" << esc(a.turmaNome);
            if (!a.tipo.empty()) o << " &middot; " << esc(a.tipo);
            o << "</span></td></tr>";
        }
        o << "</table>";
    }
    o << "</div>";

    // --- provas ------------------------------------------------------------
    auto provas = s.avaliacoes;
    std::sort(provas.begin(), provas.end(),
              [](const Avaliacao& a, const Avaliacao& b) { return a.quando < b.quando; });

    if (!provas.empty()) {
        o << "<h2>Provas</h2><div class=\"card\"><table>";
        for (const auto& a : provas) {
            o << "<tr><td class=\"data\">" << dataCurta(a.quando);
            if (const auto h = hora(a.quando); !h.empty()) {
                o << "<br><small>" << h << "</small>";
            }
            o << "</td><td><span class=\"titulo\">" << esc(a.descricao) << "</span>"
              << "<span class=\"turma\">" << esc(a.turmaNome);
            if (!a.horarioBruto.empty() && !a.quando.hasTime) {
                o << " &middot; " << esc(a.horarioBruto);
            }
            // Honestidade sobre a procedência: heurística sobre título de
            // tópico não tem o mesmo peso que agendamento formal do professor.
            if (a.fonte == FonteAvaliacao::TopicoAula) {
                o << " &middot; <em>inferido do tópico, confirme</em>";
            }
            o << "</span></td></tr>";
        }
        o << "</table></div>";
    }

    // --- atualizações ------------------------------------------------------
    o << "<h2>Atualizações das turmas</h2><div class=\"card\">";
    if (atual.empty()) {
        o << "<div class=\"vazio\">Nada novo.</div>";
    } else {
        o << "<table>";
        for (const auto& u : atual) {
            o << "<tr><td class=\"data\">" << dataCurta(u.data) << "</td>"
              << "<td>" << esc(u.texto)
              << "<span class=\"turma\">" << esc(u.turmaNome) << "</span></td></tr>";
        }
        o << "</table>";
    }
    o << "</div>";

    // --- turmas ------------------------------------------------------------
    o << "<h2>Turmas</h2><div class=\"card\">";
    if (s.turmas.empty()) {
        o << "<div class=\"vazio\">Nenhuma turma encontrada.</div>";
    } else {
        o << "<table><tr><th>Disciplina</th><th>Local</th><th>Horário</th><th>id</th></tr>";
        for (const auto& t : s.turmas) {
            o << "<tr><td>" << esc(t.nome);
            if (!t.codigo.empty()) o << "<span class=\"turma\">" << esc(t.codigo) << "</span>";
            o << "</td><td>" << esc(t.local) << "</td>"
              << "<td><code>" << esc(t.horario) << "</code></td>"
              << "<td><code>" << esc(t.idTurma) << "</code></td></tr>";
        }
        o << "</table>";
    }
    o << "</div>";

    o << "<footer>Gerado por sigaa-viewer. Dados lidos do SIGAA — "
         "confira sempre no sistema oficial antes de confiar num prazo.</footer>";
    o << "</main></body></html>";
    return o.str();
}

} // namespace sigaa::report
