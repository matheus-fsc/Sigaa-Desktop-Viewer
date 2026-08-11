#include "core/parse/TurmaParser.h"

#include <regex>

#include "core/jsf/JsfForm.h"

namespace sigaa::parse {
namespace {

// Cobre as formas que os professores usam de fato:
//   "Primeira avaliação", "1ª Avaliação", "Segunda avaliação",
//   "Avaliação substitutiva", "Prova 2", "Exame", "Seminário", "Entrega ..."
const std::regex& reAvaliacao() {
    static const std::regex re(
        R"((?:^|\W)(avalia)|(?:^|\W)(prova)|(?:^|\W)(exame)|(?:^|\W)(semin)|)"
        R"((?:^|\W)(substitutiva)|(?:^|\W)(2\W*chamada)|(?:^|\W)(entrega))",
        std::regex::icase);
    return re;
}

// Títulos que CONTÊM as palavras acima mas não são prova. Vistos em produção:
//   "Revisão pré-prova"   -> aula de revisão
//   "Auto-avaliação ..."  -> tópico de plano de ensino
//   "Aula de dúvidas"     -> não casaria, mas fica documentado
//
// Inventar uma prova é pior que perder uma: o aluno estuda para o dia errado
// e desconfia do app. Na dúvida, não marcar.
const std::regex& reNaoEhAvaliacao() {
    static const std::regex re(
        R"(revis|pr[eé]\W*prova|d[uú]vida|auto\W*avalia|plano de ensino|)"
        R"(gabarito|correç|corre[çc][ãa]o|vista de prova|resultado)",
        std::regex::icase);
    return re;
}

} // namespace

std::string tipoDoIcone(std::string_view src) {
    // "/sigaa/img/porta_arquivos/icones/tarefa.png" -> "tarefa"
    const auto fim = src.rfind(".png");
    if (fim == std::string_view::npos) return {};
    const auto ini = src.rfind('/', fim);
    if (ini == std::string_view::npos) return {};
    return std::string(src.substr(ini + 1, fim - ini - 1));
}

std::vector<MaterialTopico> parseMateriais(const html::Node& bloco) {
    std::vector<MaterialTopico> out;

    // `.item` é a linha de material dentro do tópico. Cada uma tem, nesta
    // ordem: o ícone do tipo, o <a> que dispara o jsfcljs, e um
    // `.descricao-item` opcional com o prazo escrito por extenso.
    for (const auto& item : bloco.select(".item")) {
        MaterialTopico m;

        if (const auto icone = item.selectFirst("img[src*='porta_arquivos/icones']")) {
            m.tipo = tipoDoIcone(icone.attr("src"));
        }

        // O <a> pode não existir: material que o professor removeu deixa a
        // linha com o texto e sem link. Guardar sem id ainda vale — o aluno vê
        // que existiu —, mas nada o torna baixável.
        for (const auto& a : item.select("a[onclick]")) {
            const auto params = jsf::parseJsfcljsParams(a.attr("onclick"));
            for (const auto& [k, v] : params) {
                if (k == "id") m.id = v;
            }
            m.titulo = a.text();
            if (!m.id.empty()) break;
        }
        if (m.titulo.empty()) continue;

        if (const auto d = item.selectFirst(".descricao-item")) m.descricao = d.textoVisivel();

        out.push_back(std::move(m));
    }
    return out;
}

int anoDoPeriodo(std::string_view periodo) {
    static const std::regex re(R"((\d{4})\.\d)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_search(periodo.begin(), periodo.end(), m, re)) return 0;
    return std::stoi(m[1].str());
}

bool pareceAvaliacao(std::string_view titulo) {
    if (std::regex_search(titulo.begin(), titulo.end(), reNaoEhAvaliacao())) return false;
    return std::regex_search(titulo.begin(), titulo.end(), reAvaliacao());
}

bool parseTituloTopico(std::string_view titulo, std::string& nome, DateTime& inicio,
                       DateTime& fim) {
    // "Nome do tópico (dd/MM/yyyy - dd/MM/yyyy)"
    static const std::regex re(
        R"(^\s*(.*?)\s*\((\d{2})/(\d{2})/(\d{4})\s*-\s*(\d{2})/(\d{2})/(\d{4})\)\s*$)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_match(titulo.begin(), titulo.end(), m, re)) {
        nome = html::trim(titulo);
        return false;
    }
    nome = html::trim(m[1].str());
    inicio = DateTime{std::stoi(m[4].str()), std::stoi(m[3].str()),
                      std::stoi(m[2].str()), 0, 0, false};
    fim = DateTime{std::stoi(m[7].str()), std::stoi(m[6].str()),
                   std::stoi(m[5].str()), 0, 0, false};
    return true;
}

bool parseDataPainel(std::string_view s, int anoLetivo, DateTime& quando,
                     std::string& horarioBruto) {
    // "04/09 6T34"  (codigo de horario SIGAA)
    // "29/09 15:45" (hora de relogio)
    // O painel NAO traz o ano — vem do periodo letivo da turma.
    static const std::regex re(R"(\s*(\d{2})/(\d{2})\s*(.*)$)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_match(s.begin(), s.end(), m, re)) return false;

    quando = DateTime{};
    quando.day = std::stoi(m[1].str());
    quando.month = std::stoi(m[2].str());
    quando.year = anoLetivo;

    horarioBruto = html::trim(m[3].str());

    // Se for HH:MM, aproveita como hora real. Codigo SIGAA ("6T34") fica
    // guardado em bruto: converter exige a tabela de turnos da instituicao.
    static const std::regex reHora(R"(^(\d{1,2}):(\d{2})$)");
    std::smatch mh;
    if (std::regex_match(horarioBruto, mh, reHora)) {
        quando.hour = std::stoi(mh[1].str());
        quando.minute = std::stoi(mh[2].str());
        quando.hasTime = true;
    }
    return quando.valid();
}

// ---------------------------------------------------------------------------

ConteudoTurma parseTurmaVirtual(const html::Document& doc, const std::string& idTurma,
                                const std::string& turmaNome,
                                const std::string& periodo) {
    ConteudoTurma out;
    const int ano = anoDoPeriodo(periodo);

    // Turma sem nada cadastrado — estado normal, nao erro (RECON §3.3).
    out.semTopicos = static_cast<bool>(doc.selectFirst(".intro-aval"));

    // --- tópicos de aula ---------------------------------------------------
    for (const auto& bloco : doc.select(".topico-aula")) {
        const auto tituloEl = bloco.selectFirst(".titulo");
        if (!tituloEl) continue;

        TopicoAula t;
        t.idTurma = idTurma;
        parseTituloTopico(tituloEl.text(), t.titulo, t.inicio, t.fim);
        if (t.titulo.empty()) continue;

        if (const auto c = bloco.selectFirst(".conteudotopico")) {
            // textoVisivel, não text: cada tópico embute o <script> do
            // drag-and-drop do RichFaces, e textContent do DOM o incluiria.
            t.conteudo = c.textoVisivel();
            t.materiais = parseMateriais(c);
        }
        out.topicos.push_back(t);

        // Fallback de avaliação: o título do tópico denuncia uma prova.
        if (t.inicio.valid() && pareceAvaliacao(t.titulo)) {
            Avaliacao a;
            a.idTurma = idTurma;
            a.turmaNome = turmaNome;
            a.descricao = t.titulo;
            a.quando = t.inicio;
            a.fonte = FonteAvaliacao::TopicoAula;
            out.avaliacoes.push_back(std::move(a));
        }
    }

    // --- painel "Avaliações" (fonte primária) -------------------------------
    for (const auto& painel : doc.select("div.rich-stglpanel")) {
        const auto head = painel.selectFirst(".rich-stglpanel-header");
        if (!head) continue;
        const std::string titulo = head.text();
        if (titulo.find("Avalia") == std::string::npos) continue;

        for (const auto& li : painel.select("ul.menu-direita > li")) {
            const auto data = li.selectFirst("span.data");
            const auto desc = li.selectFirst("span.descricao");
            if (!data || !desc) continue;

            Avaliacao a;
            a.idTurma = idTurma;
            a.turmaNome = turmaNome;
            a.descricao = desc.text();
            a.fonte = FonteAvaliacao::PainelAvaliacoes;
            if (!parseDataPainel(data.text(), ano, a.quando, a.horarioBruto)) continue;
            out.avaliacoes.push_back(std::move(a));
        }
    }

    return out;
}

} // namespace sigaa::parse
