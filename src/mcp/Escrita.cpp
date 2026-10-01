#include "mcp/Escrita.h"

#include <ctime>
#include <deque>
#include <map>
#include <mutex>

#include "core/parse/Html.h"
#include "core/util/Texto.h"

namespace sigaa::mcp {

using nlohmann::json;

namespace {

constexpr int kMaxTexto = 2000;
constexpr int kMaxTopico = 200;
constexpr int kMaxTopicos = 30;
constexpr int kEscritasPorMinuto = 60;

std::mutex mutexLimite;
std::deque<std::time_t> escritas;

// Uma janela deslizante de um minuto. Por processo, que é a vida de uma
// sessão do agente: o servidor nasce e morre com ele.
bool dentroDoLimite() {
    std::lock_guard<std::mutex> g(mutexLimite);
    const std::time_t agora = std::time(nullptr);
    while (!escritas.empty() && agora - escritas.front() >= 60) escritas.pop_front();
    if (static_cast<int>(escritas.size()) >= kEscritasPorMinuto) return false;
    escritas.push_back(agora);
    return true;
}

std::int64_t agora() { return static_cast<std::int64_t>(std::time(nullptr)); }

// --- validação ---------------------------------------------------------------

struct Erro {
    std::string msg;
};

std::string textoObrigatorio(const json& a, const char* k, int maximo) {
    if (!a.contains(k) || !a[k].is_string()) throw Erro{std::string("falta `") + k + "` (texto)"};
    std::string v = html::collapseWhitespace(a[k].get<std::string>());
    if (v.empty()) throw Erro{std::string("`") + k + "` está vazio"};
    if (static_cast<int>(v.size()) > maximo) {
        throw Erro{std::string("`") + k + "` passa de " + std::to_string(maximo) + " caracteres"};
    }
    return v;
}

std::string textoOpcional(const json& a, const char* k, int maximo) {
    if (!a.contains(k) || a[k].is_null()) return {};
    if (!a[k].is_string()) throw Erro{std::string("`") + k + "` tem de ser texto"};
    std::string v = html::collapseWhitespace(a[k].get<std::string>());
    if (static_cast<int>(v.size()) > maximo) {
        throw Erro{std::string("`") + k + "` passa de " + std::to_string(maximo) + " caracteres"};
    }
    return v;
}

int inteiroObrigatorio(const json& a, const char* k, int minimo, int maximo) {
    if (!a.contains(k) || !a[k].is_number_integer()) {
        throw Erro{std::string("falta `") + k + "` (número inteiro)"};
    }
    const int v = a[k].get<int>();
    if (v < minimo || v > maximo) {
        throw Erro{std::string("`") + k + "` tem de estar entre " + std::to_string(minimo) +
                   " e " + std::to_string(maximo)};
    }
    return v;
}

// O dia: hoje por padrão; nunca no futuro — registro é do que aconteceu.
DateTime diaPassado(const json& a) {
    const std::string s = textoOpcional(a, "quando", 32);
    if (s.empty()) {
        DateTime h = hojeLocal();
        h.hasTime = false;
        h.hour = h.minute = 0;
        return h;
    }
    DateTime d = lerDataIso(s);
    if (!d.valid()) throw Erro{"`quando` tem de ser uma data aaaa-mm-dd"};
    d.hasTime = false;
    d.hour = d.minute = 0;
    if (diasAteHoje(d) > 0) throw Erro{"`quando` está no futuro; registre só o que já aconteceu"};
    if (diasAteHoje(d) < -366) throw Erro{"`quando` é de mais de um ano atrás"};
    return d;
}

const Turma& turmaObrigatoria(Contexto& c, const json& a) {
    const std::string q = textoObrigatorio(a, "turma", 200);
    const auto e = acharTurma(c.snapshot(), q);
    if (!e.turma) throw Erro{e.erro};
    c.turmaUsada = e.turma->idTurma;
    return *e.turma;
}

// O tópico casa com algum tópico de aula da turma? O agente pode nomear um
// subtópico ("FIRST/FOLLOW" dentro de "Análise LL(1)"); só avisamos.
std::string topicoConhecido(Contexto& c, const std::string& idTurma, const std::string& topico) {
    const std::string q = util::dobrar(topico);
    for (const auto& t : c.snapshot().topicos) {
        if (t.idTurma != idTurma) continue;
        const std::string d = util::dobrar(t.titulo);
        if (d == q || d.find(q) != std::string::npos || q.find(d) != std::string::npos) return t.titulo;
    }
    return {};
}

Resultado comValidacao(const char* prefixo, const std::function<Resultado()>& f) {
    try {
        return f();
    } catch (const Erro& e) {
        return Resultado::falha(prefixo + e.msg + ".");
    }
}

std::string duracao(int minutos) {
    const int h = minutos / 60, m = minutos % 60;
    if (h == 0) return std::to_string(m) + " min";
    return std::to_string(h) + "h" + (m ? (m < 10 ? "0" : "") + std::to_string(m) : "");
}

// --- as ferramentas ----------------------------------------------------------

Resultado registrarEstudo(Contexto& c, const json& a) {
    return comValidacao("Não registrei: ", [&] {
        const Turma& t = turmaObrigatoria(c, a);
        estudo::RegistroEstudo r;
        r.origem = c.cliente();
        r.criadoEm = agora();
        r.idTurma = t.idTurma;
        r.minutos = inteiroObrigatorio(a, "minutos", 1, 720);
        r.quando = diaPassado(a);
        r.prova = textoOpcional(a, "prova", kMaxTopico);
        r.observacao = textoOpcional(a, "observacao", kMaxTexto);
        if (a.contains("topicos")) {
            if (!a["topicos"].is_array()) throw Erro{"`topicos` tem de ser uma lista de textos"};
            if (static_cast<int>(a["topicos"].size()) > kMaxTopicos) {
                throw Erro{"mais de " + std::to_string(kMaxTopicos) + " tópicos numa sessão"};
            }
            for (const auto& x : a["topicos"]) {
                if (!x.is_string()) throw Erro{"`topicos` tem de ser uma lista de textos"};
                std::string v = html::collapseWhitespace(x.get<std::string>());
                if (v.empty()) continue;
                if (static_cast<int>(v.size()) > kMaxTopico) throw Erro{"um tópico passa de 200 caracteres"};
                r.topicos.push_back(v);
            }
        }
        if (!dentroDoLimite()) throw Erro{"limite de 60 registros por minuto atingido; espere um pouco"};
        const auto id = c.db().registrarEstudo(r);
        if (!id) throw Erro{"o banco recusou: " + c.db().erro()};

        int semana = 0;
        for (const auto& x : c.db().carregarRegistrosEstudo(t.idTurma)) {
            if (diasAteHoje(x.quando) > -7) semana += x.minutos;
        }
        Resultado res;
        res.dados = {{"id", id}, {"turma", t.nome}, {"minutos", r.minutos},
                     {"minutos_na_semana", semana}};
        res.texto = "Registrado: " + duracao(r.minutos) + " de estudo em " + t.nome +
                    (r.topicos.empty() ? "" : " (" + std::to_string(r.topicos.size()) + " tópico(s))") +
                    ". Nos últimos 7 dias: " + duracao(semana) + ".";
        return res;
    });
}

Resultado registrarDesempenho(Contexto& c, const json& a) {
    return comValidacao("Não registrei: ", [&] {
        const Turma& t = turmaObrigatoria(c, a);
        estudo::Desempenho d;
        d.origem = c.cliente();
        d.criadoEm = agora();
        d.idTurma = t.idTurma;
        d.topico = textoObrigatorio(a, "topico", kMaxTopico);
        d.total = inteiroObrigatorio(a, "total", 1, 500);
        d.acertos = inteiroObrigatorio(a, "acertos", 0, d.total);
        d.tipo = textoOpcional(a, "tipo", 20);
        if (d.tipo.empty()) d.tipo = "exercicio";
        if (d.tipo != "simulado" && d.tipo != "exercicio" && d.tipo != "revisao") {
            throw Erro{"`tipo` tem de ser simulado, exercicio ou revisao"};
        }
        d.quando = diaPassado(a);
        if (!dentroDoLimite()) throw Erro{"limite de 60 registros por minuto atingido; espere um pouco"};
        const auto id = c.db().registrarDesempenho(d);
        if (!id) throw Erro{"o banco recusou: " + c.db().erro()};

        const std::string conhecido = topicoConhecido(c, t.idTurma, d.topico);
        Resultado res;
        res.dados = {{"id", id}, {"turma", t.nome}, {"topico", d.topico},
                     {"aproveitamento", static_cast<double>(d.acertos) / d.total},
                     {"topico_de_aula", conhecido.empty() ? json(nullptr) : json(conhecido)}};
        res.texto = "Registrado: " + std::to_string(d.acertos) + " de " + std::to_string(d.total) +
                    " em \"" + d.topico + "\" (" + t.nome + ")." +
                    (conhecido.empty() ? " O tópico não casa com nenhum tópico de aula registrado; "
                                         "se for um deles, use o nome igual ao de topicos_de_aula."
                                       : "");
        return res;
    });
}

Resultado marcarFoco(Contexto& c, const json& a) {
    return comValidacao("Não registrei: ", [&] {
        const Turma& t = turmaObrigatoria(c, a);
        estudo::PontoFoco f;
        f.origem = c.cliente();
        f.criadoEm = f.atualizadoEm = agora();
        f.idTurma = t.idTurma;
        f.topico = textoObrigatorio(a, "topico", kMaxTopico);
        f.nivel = inteiroObrigatorio(a, "nivel", 1, 3);
        f.motivo = textoObrigatorio(a, "motivo", kMaxTexto);
        if (!dentroDoLimite()) throw Erro{"limite de 60 registros por minuto atingido; espere um pouco"};
        bool atualizou = false;
        const auto id = c.db().marcarFoco(f, &atualizou);
        if (!id) throw Erro{"o banco recusou: " + c.db().erro()};
        Resultado res;
        res.dados = {{"id", id}, {"turma", t.nome}, {"topico", f.topico}, {"nivel", f.nivel},
                     {"atualizado", atualizou}};
        res.texto = std::string(atualizou ? "Ponto de foco atualizado" : "Ponto de foco marcado") +
                    ": \"" + f.topico + "\" em " + t.nome + " (nível " + std::to_string(f.nivel) +
                    "). Ele aparece para o aluno no app; use resolver_foco com id " +
                    std::to_string(id) + " quando ele superar.";
        return res;
    });
}

Resultado resolverFoco(Contexto& c, const json& a) {
    return comValidacao("Não registrei: ", [&] {
        if (!a.contains("id") || !a["id"].is_number_integer()) throw Erro{"falta `id` (de marcar_foco ou meu_progresso)"};
        const auto id = a["id"].get<std::int64_t>();
        const std::string motivo = textoOpcional(a, "motivo", kMaxTexto);
        if (!dentroDoLimite()) throw Erro{"limite de 60 registros por minuto atingido; espere um pouco"};
        if (!c.db().resolverFoco(id, motivo, agora())) {
            throw Erro{"não há ponto de foco aberto com id " + std::to_string(id)};
        }
        Resultado res;
        res.dados = {{"id", id}, {"resolvido", true}};
        res.texto = "Ponto de foco " + std::to_string(id) + " resolvido.";
        return res;
    });
}

Resultado meuProgresso(Contexto& c, const json& a) {
    return comValidacao("", [&] {
        std::string idTurma;
        std::string nomeTurma;
        if (a.contains("turma") && a["turma"].is_string() && !a["turma"].get<std::string>().empty()) {
            const Turma& t = turmaObrigatoria(c, a);
            idTurma = t.idTurma;
            nomeTurma = t.nome;
        }
        std::map<std::string, std::string> nomes;
        for (const auto& t : c.snapshot().turmas) nomes[t.idTurma] = t.nome;

        // Tempo por turma: total e últimos 7 dias.
        std::map<std::string, std::pair<int, int>> tempo;
        json sessoes = json::array();
        for (const auto& r : c.db().carregarRegistrosEstudo(idTurma)) {
            auto& [total, semana] = tempo[r.idTurma];
            total += r.minutos;
            if (diasAteHoje(r.quando) > -7) semana += r.minutos;
            if (sessoes.size() < 5) {
                sessoes.push_back({{"turma", nomes[r.idTurma]}, {"quando", r.quando.toIso()},
                                   {"minutos", r.minutos}, {"topicos", r.topicos},
                                   {"origem", r.origem}});
            }
        }
        json porTurma = json::array();
        for (const auto& [id, t] : tempo) {
            porTurma.push_back({{"turma", nomes[id]}, {"minutos_total", t.first},
                                {"minutos_7_dias", t.second}});
        }

        // Desempenho por tópico, somado.
        std::map<std::pair<std::string, std::string>, std::pair<int, int>> acum;
        std::map<std::pair<std::string, std::string>, std::string> ultimo;
        for (const auto& d : c.db().carregarDesempenho(idTurma)) {
            const auto chave = std::make_pair(d.idTurma, util::dobrar(d.topico));
            acum[chave].first += d.acertos;
            acum[chave].second += d.total;
            if (!ultimo.count(chave)) ultimo[chave] = d.topico;
        }
        json desempenho = json::array();
        for (const auto& [k, v] : acum) {
            desempenho.push_back({{"turma", nomes[k.first]}, {"topico", ultimo[k]},
                                  {"acertos", v.first}, {"total", v.second},
                                  {"aproveitamento", static_cast<double>(v.first) / v.second}});
        }

        json focos = json::array();
        for (const auto& f : c.db().carregarFocos(idTurma, true)) {
            focos.push_back({{"id", f.id}, {"turma", nomes[f.idTurma]}, {"topico", f.topico},
                             {"nivel", f.nivel}, {"motivo", f.motivo}, {"origem", f.origem}});
        }

        std::string md = "# Progresso" + (nomeTurma.empty() ? std::string() : " — " + nomeTurma) + "\n\n";
        if (porTurma.empty() && desempenho.empty() && focos.empty()) {
            md += "Nada registrado ainda. Use registrar_estudo, registrar_desempenho e "
                  "marcar_foco durante a sessão.\n";
        }
        for (const auto& t : porTurma) {
            md += "- " + t["turma"].get<std::string>() + ": " + duracao(t["minutos_7_dias"]) +
                  " nos últimos 7 dias (" + duracao(t["minutos_total"]) + " no total)\n";
        }
        if (!desempenho.empty()) {
            md += "\n## Desempenho por tópico\n\n";
            for (const auto& d : desempenho) {
                md += "- " + d["topico"].get<std::string>() + " (" + d["turma"].get<std::string>() +
                      "): " + std::to_string(d["acertos"].get<int>()) + "/" +
                      std::to_string(d["total"].get<int>()) + "\n";
            }
        }
        if (!focos.empty()) {
            md += "\n## Pontos de foco abertos\n\n";
            for (const auto& f : focos) {
                md += "- [id " + std::to_string(f["id"].get<std::int64_t>()) + ", nível " +
                      std::to_string(f["nivel"].get<int>()) + "] " + f["topico"].get<std::string>() +
                      " (" + f["turma"].get<std::string>() + "): " + f["motivo"].get<std::string>() + "\n";
            }
        }
        Resultado res;
        res.dados = {{"tempo", porTurma}, {"ultimas_sessoes", sessoes},
                     {"desempenho", desempenho}, {"focos_abertos", focos}};
        res.texto = md;
        return res;
    });
}

json turmaProp() {
    return {{"type", "string"}, {"description", "id, código ou parte do nome da turma"}};
}

json dataProp() {
    return {{"type", "string"}, {"description", "aaaa-mm-dd; padrão hoje; nunca no futuro"}};
}

} // namespace

void zerarLimiteDeEscrita() {
    std::lock_guard<std::mutex> g(mutexLimite);
    escritas.clear();
}

const std::vector<Ferramenta>& ferramentasDeEscrita() {
    static const std::vector<Ferramenta> fs = [] {
        std::vector<Ferramenta> v;
        Ferramenta f;

        f = {"registrar_estudo", "Registrar estudo",
             "Grava no app uma sessão de estudo que você fez com o aluno: quantos minutos, "
             "os tópicos e, se for o caso, para qual prova. Chame ao fim da sessão. O aluno vê "
             "o tempo no app.",
             {{"type", "object"},
              {"properties",
               {{"turma", turmaProp()},
                {"minutos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 720}}},
                {"topicos", {{"type", "array"}, {"items", {{"type", "string"}}}, {"maxItems", kMaxTopicos}}},
                {"prova", {{"type", "string"}, {"description", "ex.: Prova 2"}}},
                {"quando", dataProp()},
                {"observacao", {{"type", "string"}, {"maxLength", kMaxTexto}}}}},
              {"required", {"turma", "minutos"}}},
             registrarEstudo, Permissao::Escrita, false};
        v.push_back(f);

        f = {"registrar_desempenho", "Registrar desempenho",
             "Grava quantas questões o aluno acertou num tópico (simulado, exercício ou "
             "revisão). Um registro por tópico; use o nome do tópico como aparece em "
             "topicos_de_aula sempre que possível.",
             {{"type", "object"},
              {"properties",
               {{"turma", turmaProp()},
                {"topico", {{"type", "string"}, {"maxLength", kMaxTopico}}},
                {"acertos", {{"type", "integer"}, {"minimum", 0}}},
                {"total", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
                {"tipo", {{"type", "string"}, {"enum", {"simulado", "exercicio", "revisao"}}}},
                {"quando", dataProp()}}},
              {"required", {"turma", "topico", "acertos", "total"}}},
             registrarDesempenho, Permissao::Escrita, false};
        v.push_back(f);

        f = {"marcar_foco", "Marcar ponto de foco",
             "Marca um ponto em que o aluno precisa focar ou tem dificuldade, com o motivo. "
             "Nível 1 = atenção, 2 = dificuldade, 3 = crítico (cai na próxima prova e ele não "
             "domina). Marcar de novo o mesmo tópico atualiza o ponto aberto, não duplica.",
             {{"type", "object"},
              {"properties",
               {{"turma", turmaProp()},
                {"topico", {{"type", "string"}, {"maxLength", kMaxTopico}}},
                {"nivel", {{"type", "integer"}, {"minimum", 1}, {"maximum", 3}}},
                {"motivo", {{"type", "string"}, {"maxLength", kMaxTexto}}}}},
              {"required", {"turma", "topico", "nivel", "motivo"}}},
             marcarFoco, Permissao::Escrita, false};
        v.push_back(f);

        f = {"resolver_foco", "Resolver ponto de foco",
             "Fecha um ponto de foco quando o aluno mostrou que superou a dificuldade. O id "
             "vem de marcar_foco ou de meu_progresso.",
             {{"type", "object"},
              {"properties",
               {{"id", {{"type", "integer"}}}, {"motivo", {{"type", "string"}, {"maxLength", kMaxTexto}}}}},
              {"required", {"id"}}},
             resolverFoco, Permissao::Escrita, false};
        v.push_back(f);

        f = {"meu_progresso", "Meu progresso",
             "O que já foi registrado por agentes: horas de estudo por turma (total e últimos "
             "7 dias), desempenho por tópico e os pontos de foco abertos, com id. Chame no "
             "começo de uma sessão para não repetir o que o aluno já domina.",
             {{"type", "object"}, {"properties", {{"turma", turmaProp()}}}},
             meuProgresso, Permissao::Leitura, true};
        v.push_back(f);
        return v;
    }();
    return fs;
}

} // namespace sigaa::mcp
