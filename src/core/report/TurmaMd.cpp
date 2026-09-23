#include "core/report/TurmaMd.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace sigaa::report {
namespace {

// "03/08/2026", ou "03/08/2026 a 10/08/2026" quando o professor datou um bloco.
std::string periodo(const DateTime& a, const DateTime& b) {
    auto br = [](const DateTime& d) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", d.day, d.month, d.year);
        return std::string(buf);
    };
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

    for (const auto& t : topicos) {
        o << "### " << seguro(t.titulo) << "\n\n";
        const std::string quando = periodo(t.inicio, t.fim);
        if (!quando.empty()) o << "**Quando:** " << quando << "\n\n";
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

    return o.str();
}

} // namespace sigaa::report
