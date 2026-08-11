#include "core/calendar/Calendario.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

namespace sigaa::calendario {
namespace {

std::string dataIcs(const DateTime& d) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d", d.year, d.month, d.day);
    return buf;
}

std::string dataHoraIcs(const DateTime& d) {
    // YYYYMMDDTHHMMSS — hora local flutuante (sem Z e sem TZID). O SIGAA nunca
    // manda offset, e horario de Brasilia sem DST torna o flutuante correto na
    // pratica: o evento cai na hora local de quem abre o calendario.
    char buf[24];
    std::snprintf(buf, sizeof buf, "%04d%02d%02dT%02d%02d00", d.year, d.month, d.day,
                  d.hour, d.minute);
    return buf;
}

// Chave de dedupe: mesma turma, mesmo dia.
std::string chaveDia(const Avaliacao& a) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s|%04d-%02d-%02d", a.idTurma.c_str(), a.quando.year,
                  a.quando.month, a.quando.day);
    return buf;
}

void linha(std::string& out, std::string_view conteudo) {
    out += dobrarLinha(conteudo);
    out += "\r\n";
}

} // namespace

// ---------------------------------------------------------------------------

std::string escaparTexto(std::string_view s) {
    // RFC 5545 §3.3.11: barra, ponto-e-vírgula, vírgula e quebra de linha.
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case ';':  o += "\\;";  break;
            case ',':  o += "\\,";  break;
            case '\n': o += "\\n";  break;
            case '\r': break;
            default:   o.push_back(c);
        }
    }
    return o;
}

std::string dobrarLinha(std::string_view linhaLogica) {
    // RFC 5545 §3.1: máximo 75 octetos por linha; continuação começa com espaço.
    // Cuidado com UTF-8: não pode cortar no meio de um caractere multibyte,
    // senão o cliente de calendário mostra lixo.
    constexpr size_t kMax = 73;
    if (linhaLogica.size() <= kMax) return std::string(linhaLogica);

    std::string out;
    size_t i = 0;
    bool primeira = true;
    while (i < linhaLogica.size()) {
        size_t take = std::min(kMax, linhaLogica.size() - i);
        // recua até um limite de caractere UTF-8 (byte que não é 10xxxxxx)
        if (i + take < linhaLogica.size()) {
            while (take > 1 &&
                   (static_cast<unsigned char>(linhaLogica[i + take]) & 0xC0) == 0x80) {
                --take;
            }
        }
        if (!primeira) out += "\r\n ";
        out.append(linhaLogica.substr(i, take));
        i += take;
        primeira = false;
    }
    return out;
}

// ---------------------------------------------------------------------------

bool aulaOcorreEm(const TopicoAula& t, const DateTime& d) {
    if (!d.valid() || !t.inicio.valid()) return false;

    auto soData = [](const DateTime& x) {
        return x.year * 10000 + x.month * 100 + x.day;
    };
    const int dia = soData(d);
    const int ini = soData(t.inicio);

    // Fim ausente ou anterior ao início: tópico de um dia só. Um `fim` que veio
    // quebrado do parser não pode alargar o intervalo — alargar faria a aula
    // aparecer em dias em que ela não acontece, que é o erro que o aluno
    // levaria a sério.
    if (!t.fim.valid() || soData(t.fim) < ini) return dia == ini;
    return dia >= ini && dia <= soData(t.fim);
}

std::vector<TopicoAula> aulasDoDia(const std::vector<TopicoAula>& topicos,
                                   const DateTime& d) {
    std::vector<TopicoAula> out;
    for (const auto& t : topicos) {
        if (aulaOcorreEm(t, d)) out.push_back(t);
    }
    return out;
}

std::vector<Avaliacao> mesclarAvaliacoes(std::vector<Avaliacao> todas) {
    // Painel antes de tópico: assim o primeiro a ocupar a chave é o confiável.
    std::stable_sort(todas.begin(), todas.end(),
                     [](const Avaliacao& a, const Avaliacao& b) {
                         return static_cast<int>(a.fonte) < static_cast<int>(b.fonte);
                     });

    std::vector<Avaliacao> out;
    std::map<std::string, size_t> vistos;
    for (auto& a : todas) {
        if (!a.quando.valid()) continue;
        const std::string k = chaveDia(a);
        if (vistos.count(k)) continue;   // já temos essa turma nesse dia
        vistos[k] = out.size();
        out.push_back(std::move(a));
    }

    std::sort(out.begin(), out.end(),
              [](const Avaliacao& a, const Avaliacao& b) { return a.quando < b.quando; });
    return out;
}

// ---------------------------------------------------------------------------

std::string gerarIcs(const Snapshot& s, const OpcoesIcs& op) {
    std::string o;
    linha(o, "BEGIN:VCALENDAR");
    linha(o, "VERSION:2.0");
    linha(o, "PRODID:-//sigaa-viewer//PT-BR//");
    linha(o, "CALSCALE:GREGORIAN");
    linha(o, "METHOD:PUBLISH");
    linha(o, "X-WR-CALNAME:SIGAA");

    auto alarme = [&](std::string& dst, std::string_view descricao) {
        if (op.lembreteMinutos <= 0) return;
        linha(dst, "BEGIN:VALARM");
        linha(dst, "ACTION:DISPLAY");
        linha(dst, "DESCRIPTION:" + escaparTexto(descricao));
        linha(dst, "TRIGGER:-PT" + std::to_string(op.lembreteMinutos) + "M");
        linha(dst, "END:VALARM");
    };

    // --- prazos de atividades ------------------------------------------------
    for (const auto& a : s.atividades) {
        if (!a.prazo.valid()) continue;
        if (a.status == StatusAtividade::Concluida) continue;   // não polui o calendário

        linha(o, "BEGIN:VEVENT");
        linha(o, "UID:" + op.prefixoUid + "-ativ-" + a.atividadeId + "-" + a.idTurma +
                     "@sigaa");
        if (a.prazo.hasTime) {
            linha(o, "DTSTART:" + dataHoraIcs(a.prazo));
            linha(o, "DURATION:PT30M");
        } else {
            linha(o, "DTSTART;VALUE=DATE:" + dataIcs(a.prazo));
        }
        linha(o, "SUMMARY:" + escaparTexto(a.turmaNome + " - " + a.titulo));
        std::string desc = a.tipo.empty() ? "Prazo" : a.tipo;
        desc += " no SIGAA. Confira sempre no sistema oficial.";
        linha(o, "DESCRIPTION:" + escaparTexto(desc));
        linha(o, "CATEGORIES:SIGAA,PRAZO");
        alarme(o, "Entrega amanha: " + a.titulo);
        linha(o, "END:VEVENT");
    }

    // --- avaliações ----------------------------------------------------------
    for (const auto& a : mesclarAvaliacoes(s.avaliacoes)) {
        linha(o, "BEGIN:VEVENT");
        linha(o, "UID:" + op.prefixoUid + "-prova-" + a.idTurma + "-" +
                     dataIcs(a.quando) + "@sigaa");
        if (a.quando.hasTime) {
            linha(o, "DTSTART:" + dataHoraIcs(a.quando));
            linha(o, "DURATION:PT2H");
        } else {
            linha(o, "DTSTART;VALUE=DATE:" + dataIcs(a.quando));
        }
        linha(o, "SUMMARY:" + escaparTexto(a.turmaNome + " - " + a.descricao));

        std::string desc = "Avaliacao no SIGAA.";
        if (!a.horarioBruto.empty()) desc += " Horario: " + a.horarioBruto + ".";
        if (a.fonte == FonteAvaliacao::TopicoAula) {
            // Honestidade com o usuário: isto veio de heurística sobre o
            // título do tópico, não de um agendamento formal do professor.
            desc += " (inferido do topico de aula — confirme com o professor)";
        }
        linha(o, "DESCRIPTION:" + escaparTexto(desc));
        linha(o, "CATEGORIES:SIGAA,PROVA");
        alarme(o, "Prova amanha: " + a.turmaNome);
        linha(o, "END:VEVENT");
    }

    linha(o, "END:VCALENDAR");
    return o;
}

} // namespace sigaa::calendario
