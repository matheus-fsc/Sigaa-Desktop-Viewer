#include "core/report/TurmaMd.h"

#include <algorithm>
#include <cstdio>
#include <regex>
#include <sstream>

namespace sigaa::report {
namespace {

std::string br(const DateTime& d) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", d.day, d.month, d.year);
    return std::string(buf);
}

// "03/08/2026", ou "03/08/2026 a 10/08/2026" quando o professor datou um bloco.
std::string periodo(const DateTime& a, const DateTime& b) {
    if (!a.valid()) return {};
    if (!b.valid() || b.toIso() == a.toIso()) return br(a);
    return br(a) + " a " + br(b);
}

// Markdown trata '#', '*', '_' e '|' como marcação. Um título de aula que
// comece com "## " viraria um cabeçalho e quebraria a hierarquia do arquivo —
// e a hierarquia é justamente o que a IA vai usar para se orientar.
std::string seguro(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '|') o += "\\|";
        else if (c == '\n' || c == '\r') o += ' ';
        else o += c;
    }
    // Só o começo importa para os marcadores de bloco; no meio da linha eles
    // são inofensivos e escapá-los encheria o texto de barras.
    if (!o.empty() && (o[0] == '#' || o[0] == '-' || o[0] == '>' || o[0] == '*')) {
        o.insert(o.begin(), '\\');
    }
    return o;
}

// Só o dia: o tópico às vezes vem com hora e a frequência nunca vem, e
// "03/08 00:00 < 03/08 19:00" não pode tirar a falta da aula do mesmo dia.
int chaveDia(const DateTime& d) { return d.year * 10000 + d.month * 100 + d.day; }

// A falta cai no tópico cujo dia (ou bloco de dias) a contém.
bool noTopico(const DiaFrequencia& f, const TopicoAula& t) {
    if (!t.inicio.valid() || !f.data.valid()) return false;
    const int dia = chaveDia(f.data);
    const int ini = chaveDia(t.inicio);
    const int fim = t.fim.valid() ? chaveDia(t.fim) : ini;
    return dia >= ini && dia <= fim;
}

void escreverFalta(std::ostringstream& o, const DiaFrequencia& f) {
    o << br(f.data);
    if (f.faltas > 0) o << " (" << f.faltas << (f.faltas == 1 ? " aula)" : " aulas)");
}

// O HTML limpo da notícia (parse::limparHtmlNoticia: só <p>, <br>, <strong>,
// <em>, <u>, listas e <a href>) em Markdown. Nada de conversor genérico: o
// conjunto de tags é fechado e pequeno, e o resultado precisa ser previsível
// para o arquivo não mudar a cada regravação.
std::string markdownDaNoticia(const std::string& html) {
    std::string t = html;
    auto troca = [&t](const std::string& de, const std::string& para) {
        for (std::size_t p = t.find(de); p != std::string::npos; p = t.find(de, p)) {
            t.replace(p, de.size(), para);
            p += para.size();
        }
    };
    // Links antes das tags genéricas: <a href="X">T</a> -> [T](X).
    static const std::regex link(R"re(<a href="([^"]*)">([\s\S]*?)</a>)re");
    t = std::regex_replace(t, link, "[$2]($1)");
    troca("<p>", "");
    troca("</p>", "\n\n");
    troca("<br>", "  \n");
    troca("<strong>", "**");
    troca("</strong>", "**");
    troca("<em>", "_");
    troca("</em>", "_");
    troca("<u>", "");
    troca("</u>", "");
    troca("<ul>", "\n");
    troca("</ul>", "\n");
    troca("<ol>", "\n");
    troca("</ol>", "\n");
    troca("<li>", "- ");
    troca("</li>", "\n");
    troca("&nbsp;", " ");
    troca("\xC2\xA0", " ");
    troca("&amp;", "&");
    troca("&lt;", "<");
    troca("&gt;", ">");
    troca("&quot;", "\"");

    // Linha por linha: tira espaço das pontas, protege quem começa com
    // marcador de bloco (a não ser o "- " de lista, que é nosso) e junta
    // linhas em branco repetidas.
    std::istringstream in(t);
    std::ostringstream out;
    std::string linha;
    int brancas = 0;
    bool primeira = true;
    while (std::getline(in, linha)) {
        const auto ini = linha.find_first_not_of(" \t");
        if (ini == std::string::npos) {
            if (!primeira) ++brancas;
            continue;
        }
        linha = linha.substr(ini);
        const bool quebraForcada = linha.size() >= 2 && linha.compare(linha.size() - 2, 2, "  ") == 0;
        while (!linha.empty() && (linha.back() == ' ' || linha.back() == '\t')) linha.pop_back();
        if (quebraForcada) linha += "  ";
        // "- " (item de lista) e "**" (negrito) no começo da linha são
        // marcação NOSSA, saída deste conversor; escapá-los apagaria a lista e
        // viraria "\**Fiquem bem!**" no parágrafo que abre em negrito.
        if (linha.rfind("- ", 0) != 0 && linha.rfind("**", 0) != 0) linha = seguro(linha);
        if (!primeira) out << (brancas > 0 ? "\n\n" : "\n");
        out << linha;
        brancas = 0;
        primeira = false;
    }
    return out.str();
}

std::string brHora(const DateTime& d) {
    std::string s = br(d);
    if (d.hasTime) {
        char buf[8];
        std::snprintf(buf, sizeof buf, " %02d:%02d", d.hour, d.minute);
        s += buf;
    }
    return s;
}

} // namespace

std::string gerarTurmaMd(const DadosTurmaMd& d) {
    std::ostringstream o;

    o << "# " << seguro(d.turma.nome) << "\n\n";
    if (!d.turma.codigo.empty()) o << "- **Código:** " << seguro(d.turma.codigo) << "\n";
    if (!d.turma.periodo.empty()) o << "- **Período:** " << seguro(d.turma.periodo) << "\n";
    if (!d.turma.horario.empty()) o << "- **Horário:** " << seguro(d.turma.horario) << "\n";
    if (!d.turma.local.empty()) o << "- **Local:** " << seguro(d.turma.local) << "\n";
    if (d.turma.cargaHoraria > 0) o << "- **Carga horária:** " << d.turma.cargaHoraria << " h\n";

    if (d.frequencia && d.frequencia->temDados) {
        o << "- **Faltas:** " << d.frequencia->faltas() << " de "
          << d.frequencia->limiteFaltas() << " permitidas\n";
    }
    o << "\n> Gerado pelo SIGAA Viewer a partir do que o professor publicou na\n"
      << "> Turma Virtual. Serve de índice para estudar — o conteúdo de cada\n"
      << "> aula está nos arquivos desta mesma pasta.\n\n";

    // --- avaliações ---------------------------------------------------------
    // Antes das aulas, de propósito: quem abre este arquivo quase sempre está
    // perguntando "o que cai na prova de quinta?", e a data da prova é o que
    // delimita a resposta.
    if (!d.provas.empty()) {
        o << "## Avaliações\n\n";
        o << "| Data | Avaliação | Horário | Origem |\n";
        o << "|---|---|---|---|\n";
        for (const auto& p : d.provas) {
            o << "| " << periodo(p.av.quando, {}) << " | " << seguro(p.av.descricao)
              << " | " << seguro(p.av.horarioBruto) << " | ";
            switch (p.estado) {
                case avaliacao::Estado::Inferida:
                    o << "deduzida de um tópico — confirme"; break;
                case avaliacao::Estado::Editada:
                    o << "data corrigida por você"; break;
                case avaliacao::Estado::Criada:
                    o << "cadastrada por você"; break;
                case avaliacao::Estado::Confirmada:
                    o << "confirmada por você"; break;
                default:
                    o << "painel do professor"; break;
            }
            o << " |\n";
        }
        o << "\n";
    }

    // --- aulas --------------------------------------------------------------
    o << "## Aulas\n\n";
    if (d.topicos.empty()) {
        o << "_O professor ainda não publicou tópicos de aula nesta turma._\n\n";
    }

    // Em ordem cronológica: é a ordem em que a matéria foi dada, e portanto a
    // ordem em que ela se estuda. O SIGAA devolve na ordem da página, que
    // muda quando o professor edita um tópico antigo.
    auto topicos = d.topicos;
    std::stable_sort(topicos.begin(), topicos.end(),
                     [](const TopicoAula& a, const TopicoAula& b) {
                         return a.inicio < b.inicio;
                     });

    // Os dias de falta, em ordem, para vincular cada um ao tópico da aula.
    std::vector<const DiaFrequencia*> faltas;
    if (d.frequencia && d.frequencia->temDados) {
        for (const auto& f : d.frequencia->dias) {
            if (f.situacao == SituacaoDia::Falta && f.data.valid()) faltas.push_back(&f);
        }
        std::stable_sort(faltas.begin(), faltas.end(),
                         [](const DiaFrequencia* a, const DiaFrequencia* b) {
                             return chaveDia(a->data) < chaveDia(b->data);
                         });
    }

    for (const auto& t : topicos) {
        o << "### " << seguro(t.titulo) << "\n\n";
        const std::string quando = periodo(t.inicio, t.fim);
        if (!quando.empty()) o << "**Quando:** " << quando << "\n\n";

        // A falta no próprio tópico: é aqui que o aluno (ou a IA) descobre
        // que aquele conteúdo foi dado sem ele e precisa ser estudado à parte.
        std::vector<const DiaFrequencia*> faltasAqui;
        for (const auto* f : faltas) {
            if (noTopico(*f, t)) faltasAqui.push_back(f);
        }
        if (!faltasAqui.empty()) {
            o << "**Falta:** ";
            for (std::size_t i = 0; i < faltasAqui.size(); ++i) {
                if (i) o << ", ";
                escreverFalta(o, *faltasAqui[i]);
            }
            o << " — você não estava nesta aula.\n\n";
        }
        if (!t.conteudo.empty()) o << seguro(t.conteudo) << "\n\n";

        if (!t.materiais.empty()) {
            o << "**Material:**\n\n";
            for (const auto& m : t.materiais) {
                o << "- " << seguro(m.titulo);
                if (!m.tipo.empty()) o << " _(" << seguro(m.tipo) << ")_";
                o << "\n";
            }
            o << "\n";
        }
    }

    // --- faltas sem tópico -------------------------------------------------
    // Dia em que o professor lançou falta mas não publicou tópico. Sumir com
    // ela esconderia justamente a aula de que o aluno não tem registro nenhum.
    std::vector<const DiaFrequencia*> orfas;
    for (const auto* f : faltas) {
        const bool coberta = std::any_of(
            topicos.begin(), topicos.end(),
            [f](const TopicoAula& t) { return noTopico(*f, t); });
        if (!coberta) orfas.push_back(f);
    }
    if (!orfas.empty()) {
        o << "## Faltas sem tópico de aula\n\n";
        for (const auto* f : orfas) {
            o << "- ";
            escreverFalta(o, *f);
            o << "\n";
        }
        o << "\n";
    }

    // --- arquivos sem aula --------------------------------------------------
    // O que o professor publicou fora de um tópico. Some seria pior que
    // desarrumado: costuma ser a lista de exercícios e o livro da disciplina.
    std::vector<const ArquivoTurma*> soltos;
    for (const auto& a : d.arquivos) {
        const bool citado = std::any_of(
            topicos.begin(), topicos.end(), [&a](const TopicoAula& t) {
                return t.titulo == a.topico;
            });
        if (!citado) soltos.push_back(&a);
    }
    if (!soltos.empty()) {
        o << "## Outros arquivos da turma\n\n";
        for (const auto* a : soltos) {
            o << "- " << seguro(a->titulo);
            if (!a->topico.empty()) o << " — _" << seguro(a->topico) << "_";
            o << "\n";
        }
        o << "\n";
    }

    // Notícias no FIM: são contexto, não o índice. O começo do arquivo é o
    // que a IA usa para se orientar (avaliações, aulas); os recados do
    // professor vêm depois, da mais nova para a mais antiga.
    if (!d.noticias.empty()) {
        o << "## Notícias\n\n";
        for (const auto& n : d.noticias) {
            o << "### " << seguro(n.titulo);
            if (n.data.valid()) o << " — " << brHora(n.data);
            o << "\n\n";
            if (!n.autor.empty()) o << "_por " << seguro(n.autor) << "_\n\n";
            if (n.conteudoHtml.empty()) {
                o << "_Texto ainda não coletado._\n\n";
            } else {
                o << markdownDaNoticia(n.conteudoHtml) << "\n\n";
            }
        }
    }

    return o.str();
}

} // namespace sigaa::report
