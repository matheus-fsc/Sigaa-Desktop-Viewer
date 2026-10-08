#include "web/Api.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <set>

#include <nlohmann/json.hpp>

#include "core/avaliacao/Ajustes.h"
#include "core/calendar/Calendario.h"
#include "core/config/Instituicao.h"
#include "core/frequencia/Presenca.h"
#include "core/parse/NoticiaParser.h"
#include "core/store/Database.h"
#include "core/util/Caminho.h"
#include "mcp/Leitura.h"

namespace sigaa::web {
namespace {

using nlohmann::json;
namespace ch = std::chrono;

// --- datas -------------------------------------------------------------------
// As contas de semana precisam de aritmética de dias, que DateTime não tem.
// O calendário civil do <chrono> faz isso sem fuso: DateTime já é hora local.

ch::sys_days paraDias(const DateTime& d) {
    return ch::sys_days{ch::year{d.year} / ch::month{static_cast<unsigned>(d.month)} /
                        ch::day{static_cast<unsigned>(d.day)}};
}

DateTime deDias(ch::sys_days s) {
    const ch::year_month_day ymd{s};
    DateTime d;
    d.year = static_cast<int>(ymd.year());
    d.month = static_cast<int>(static_cast<unsigned>(ymd.month()));
    d.day = static_cast<int>(static_cast<unsigned>(ymd.day()));
    return d;
}

// ISO: 1 = segunda ... 7 = domingo, como em Calendario.h.
int diaSemana(const DateTime& d) {
    return static_cast<int>(ch::weekday{paraDias(d)}.iso_encoding());
}

DateTime segundaDe(const DateTime& d) { return deDias(paraDias(d) - ch::days{diaSemana(d) - 1}); }

bool mesmoDia(const DateTime& a, const DateTime& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

const char* nomeDia(int iso) {
    static const char* nomes[] = {"segunda", "terça", "quarta", "quinta",
                                  "sexta",   "sábado", "domingo"};
    return (iso >= 1 && iso <= 7) ? nomes[iso - 1] : "";
}

json data(const DateTime& d) { return d.valid() ? json(d.toIso()) : json(nullptr); }

// --- vocabulário ---------------------------------------------------------------

std::string estadoProva(avaliacao::Estado e) {
    using avaliacao::Estado;
    switch (e) {
        case Estado::DoSigaa:    return "do_sigaa";
        case Estado::Inferida:   return "deduzida";
        case Estado::Confirmada: return "confirmada";
        case Estado::Editada:    return "corrigida";
        case Estado::Criada:     return "criada";
        case Estado::Conflitada: return "conflito";
    }
    return "do_sigaa";
}

std::string situacao(SituacaoDia s) {
    switch (s) {
        case SituacaoDia::Presente: return "presente";
        case SituacaoDia::Falta:    return "falta";
        case SituacaoDia::NaoRegistrada: break;
    }
    return "nao_registrada";
}

std::string estadoDia(frequencia::EstadoDia e) {
    switch (e) {
        case frequencia::EstadoDia::DoSigaa:          return "do_sigaa";
        case frequencia::EstadoDia::MarcadaPeloAluno: return "marcada";
        case frequencia::EstadoDia::Conflitada:       return "conflito";
        case frequencia::EstadoDia::NaoRegistrada:    break;
    }
    return "nao_registrada";
}

const Frequencia* frequenciaDe(const Snapshot& s, const std::string& idTurma) {
    for (const auto& f : s.frequencias) {
        if (f.idTurma == idTurma) return &f;
    }
    return nullptr;
}

// "n de k faltas" ou nulo. Nulo quer dizer "o professor não lançou nada",
// que é diferente de zero faltas — a mesma distinção do traço na Agenda.
json faltas(const Snapshot& s, const std::string& idTurma) {
    const Frequencia* f = frequenciaDe(s, idTurma);
    if (!f || !f->temDados) return nullptr;
    return {{"faltas", f->faltas()}, {"limite", f->limiteFaltas()}, {"reprovado", f->reprovado()}};
}

json turmaCurta(const Turma& t) {
    return {{"id", t.idTurma},       {"nome", t.nome},   {"codigo", t.codigo},
            {"horario", t.horario},  {"local", t.local}, {"periodo", t.periodo}};
}

Resposta ok(const json& j) {
    Resposta r;
    r.corpo = j.dump();
    return r;
}

Resposta erro(int status, const std::string& msg) {
    Resposta r;
    r.status = status;
    r.corpo = json{{"erro", msg}}.dump();
    return r;
}

// Ids do SIGAA são números. Qualquer outra coisa num segmento de URL é erro
// do cliente — e é também o que impede um id de virar pedaço de caminho.
bool soDigitos(const std::string& s) {
    return !s.empty() && s.size() <= 20 &&
           std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// --- rotas ---------------------------------------------------------------------

std::string periodoDe(const Snapshot& s) {
    std::string p;
    for (const auto& t : s.turmas) p = std::max(p, t.periodo);
    return p;
}

std::vector<const Atividade*> prazosAbertos(const Snapshot& s) {
    std::vector<const Atividade*> v;
    for (const auto& a : s.atividades) {
        if (a.status == StatusAtividade::Concluida || !a.prazo.valid()) continue;
        // Uma semana de atraso ainda é assunto; um mês já não é.
        if (mcp::diasAteHoje(a.prazo) < -7) continue;
        v.push_back(&a);
    }
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->prazo < b->prazo; });
    return v;
}

json jsonPrazo(const Atividade& a) {
    return {{"turma_id", a.idTurma},
            {"turma", a.turmaNome},
            {"titulo", a.titulo},
            {"tipo", a.tipo},
            {"prazo", a.prazo.toIso()},
            {"dias_ate", mcp::diasAteHoje(a.prazo)}};
}

Resposta resumo(mcp::Contexto& c, store::Database& db) {
    const Snapshot& s = c.snapshot();
    int provas = 0;
    for (const auto& p : c.provas()) {
        const int n = mcp::diasAteHoje(p.av.quando);
        if (n >= 0 && n <= 30) ++provas;
    }
    int novidades = 0;
    for (const auto& a : s.atualizacoes) {
        if (a.data.valid() && mcp::diasAteHoje(a.data) >= -7) ++novidades;
    }
    json ultimo = nullptr;
    if (auto v = db.lerMeta("ultimo_sync")) {
        try {
            ultimo = std::stoll(*v);
        } catch (...) {
        }
    }
    return ok({{"instituicao", config::selecionada().nome},
               {"periodo", periodoDe(s)},
               {"ultimo_sync", ultimo},
               {"hoje", mcp::hojeLocal().toIso()},
               {"contagens",
                {{"agenda", prazosAbertos(s).size()},
                 {"provas", provas},
                 {"turmas", s.turmas.size()},
                 {"novidades", novidades}}}});
}

Resposta agenda(mcp::Contexto& c, const std::map<std::string, std::string>& q) {
    const Snapshot& s = c.snapshot();
    DateTime hoje = mcp::hojeLocal();
    hoje.hasTime = false;
    hoje.hour = hoje.minute = 0;

    DateTime ref = hoje;
    if (auto it = q.find("semana"); it != q.end()) {
        const DateTime d = mcp::lerDataIso(it->second);
        if (d.valid()) ref = d;
    }
    const DateTime ini = segundaDe(ref);
    const int deslocamento =
        static_cast<int>((paraDias(ini) - paraDias(segundaDe(hoje))).count() / 7);

    json dias = json::array();
    for (int i = 0; i < 7; ++i) {
        const DateTime d = deDias(paraDias(ini) + ch::days{i});
        const int iso = diaSemana(d);

        struct Linha {
            int ordem;
            json j;
        };
        std::vector<Linha> linhas;
        for (const auto& t : s.turmas) {
            // O tópico que o professor registrou para este dia, se houver.
            std::string topico;
            for (const auto& tp : s.topicos) {
                if (tp.idTurma == t.idTurma && calendario::aulaOcorreEm(tp, d, t.horario)) {
                    topico = tp.titulo;
                    break;
                }
            }
            bool naGrade = false;
            for (const auto& b : calendario::lerHorario(t.horario)) {
                if (!b.dias.count(iso)) continue;
                naGrade = true;
                json j = {{"turma_id", t.idTurma}, {"turma", t.nome}, {"codigo", t.codigo},
                          {"horario", b.codigo()}, {"local", t.local},
                          {"topico", topico.empty() ? json(nullptr) : json(topico)},
                          {"extra", false}, {"faltas", faltas(s, t.idTurma)}};
                linhas.push_back({b.ordem(), std::move(j)});
                topico.clear();   // dois blocos no mesmo dia: o tópico vai no primeiro
            }
            // Aula registrada num dia que a grade não prevê: reposição.
            if (!naGrade && !topico.empty()) {
                json j = {{"turma_id", t.idTurma}, {"turma", t.nome}, {"codigo", t.codigo},
                          {"horario", nullptr}, {"local", t.local}, {"topico", topico},
                          {"extra", true}, {"faltas", faltas(s, t.idTurma)}};
                linhas.push_back({1000, std::move(j)});
            }
        }
        std::stable_sort(linhas.begin(), linhas.end(),
                         [](const Linha& a, const Linha& b) { return a.ordem < b.ordem; });
        json aulas = json::array();
        for (auto& l : linhas) aulas.push_back(std::move(l.j));
        dias.push_back({{"data", d.toIso()}, {"dia_semana", nomeDia(iso)},
                        {"hoje", mesmoDia(d, hoje)}, {"aulas", aulas}});
    }

    json prazos = json::array();
    for (const Atividade* a : prazosAbertos(s)) prazos.push_back(jsonPrazo(*a));

    return ok({{"semana", {{"inicio", ini.toIso()},
                           {"fim", deDias(paraDias(ini) + ch::days{6}).toIso()},
                           {"deslocamento", deslocamento}}},
               {"coletado", !s.turmas.empty()},
               {"dias", dias},
               {"prazos", prazos}});
}

json jsonProva(mcp::Contexto& c, const avaliacao::Efetiva& p) {
    json j = {{"turma_id", p.av.idTurma},
              {"turma", p.av.turmaNome},
              {"descricao", p.av.descricao},
              {"data", data(p.av.quando)},
              {"tem_hora", p.av.quando.hasTime},
              {"dias_ate", mcp::diasAteHoje(p.av.quando)},
              {"estado", estadoProva(p.estado)},
              {"nota", p.nota}};
    if (p.quandoSigaa.valid() && p.quandoSigaa.toIso() != p.av.quando.toIso()) {
        j["data_sigaa"] = p.quandoSigaa.toIso();
    }
    for (const auto& t : c.snapshot().turmas) {
        if (t.idTurma == p.av.idTurma && !t.local.empty()) j["local"] = t.local;
    }
    return j;
}

Resposta provas(mcp::Contexto& c) {
    json lista = json::array();
    for (const auto& p : c.provas()) {
        if (p.av.quando.valid()) lista.push_back(jsonProva(c, p));
    }
    return ok({{"provas", lista}});
}

const avaliacao::Efetiva* proximaProva(mcp::Contexto& c, const std::string& idTurma) {
    for (const auto& p : c.provas()) {
        if (p.av.idTurma == idTurma && p.av.quando.valid() && mcp::diasAteHoje(p.av.quando) >= 0) {
            return &p;   // a lista já vem em ordem cronológica
        }
    }
    return nullptr;
}

bool baixado(mcp::Contexto& c, const ArquivoTurma& a) {
    return !mcp::caminhoDoArquivo(c, a).empty();
}

Resposta turmas(mcp::Contexto& c) {
    const Snapshot& s = c.snapshot();
    json lista = json::array();
    for (const auto& t : s.turmas) {
        json j = turmaCurta(t);
        j["faltas"] = faltas(s, t.idTurma);
        const auto* p = proximaProva(c, t.idTurma);
        j["proxima_prova"] = p ? json{{"descricao", p->av.descricao},
                                      {"data", p->av.quando.toIso()},
                                      {"dias_ate", mcp::diasAteHoje(p->av.quando)}}
                               : json(nullptr);
        int arquivos = 0, salvos = 0;
        for (const auto& a : s.arquivos) {
            if (a.idTurma != t.idTurma) continue;
            ++arquivos;
            if (baixado(c, a)) ++salvos;
        }
        j["arquivos"] = arquivos;
        j["baixados"] = salvos;
        lista.push_back(std::move(j));
    }
    return ok({{"turmas", lista}});
}

Resposta turma(mcp::Contexto& c, const std::string& id) {
    const Snapshot& s = c.snapshot();
    const Turma* t = nullptr;
    for (const auto& x : s.turmas) {
        if (x.idTurma == id) t = &x;
    }
    if (!t) return erro(404, "turma não encontrada");

    // Arquivos por título, para o tópico apontar o arquivo que já está no
    // disco: o tópico só sabe o nome do material, o arquivo sabe o id.
    json arquivos = json::array();
    for (const auto& a : s.arquivos) {
        if (a.idTurma != id) continue;
        arquivos.push_back({{"id", a.idArquivo}, {"titulo", a.titulo}, {"topico", a.topico},
                            {"baixado", baixado(c, a)}});
    }

    json topicos = json::array();
    for (const auto& tp : s.topicos) {
        if (tp.idTurma != id) continue;
        json mats = json::array();
        for (const auto& m : tp.materiais) mats.push_back({{"titulo", m.titulo}, {"tipo", m.tipo}});
        topicos.push_back({{"titulo", tp.titulo}, {"inicio", data(tp.inicio)},
                           {"fim", data(tp.fim)}, {"conteudo", tp.conteudo},
                           {"materiais", mats}});
    }

    // Texto puro: o HTML do professor não é renderizado no celular. É conteúdo
    // de terceiro, e a página não tem por que confiar nele.
    json noticias = json::array();
    for (const auto& n : c.db().carregarNoticias(id)) {
        noticias.push_back({{"titulo", n.titulo}, {"data", data(n.data)}, {"autor", n.autor},
                            {"texto", parse::textoDaNoticia(n.conteudoHtml)}});
    }

    json freq = nullptr;
    if (const Frequencia* f = frequenciaDe(s, id); f && f->temDados) {
        json dias = json::array();
        for (const auto& d : frequencia::efetivos(*f, c.db().carregarMarcacoes(id))) {
            dias.push_back({{"data", data(d.data)}, {"situacao", situacao(d.situacao)},
                            {"faltas", d.faltas}, {"estado", estadoDia(d.estado)}});
        }
        freq = {{"faltas", f->faltas()},          {"limite", f->limiteFaltas()},
                {"presencas", f->presencas},      {"aulas_registradas", f->aulasComRegistro},
                {"aulas_ch", f->aulasPelaCH},     {"reprovado", f->reprovado()},
                {"dias", dias}};
    }

    json provasDaTurma = json::array();
    for (const auto& p : c.provas()) {
        if (p.av.idTurma == id && p.av.quando.valid()) provasDaTurma.push_back(jsonProva(c, p));
    }

    return ok({{"turma", turmaCurta(*t)},
               {"topicos", topicos},
               {"arquivos", arquivos},
               {"noticias", noticias},
               {"frequencia", freq},
               {"provas", provasDaTurma}});
}

Resposta novidades(mcp::Contexto& c) {
    std::vector<const Atualizacao*> v;
    for (const auto& a : c.snapshot().atualizacoes) v.push_back(&a);
    std::stable_sort(v.begin(), v.end(), [](auto* a, auto* b) { return b->data < a->data; });
    if (v.size() > 100) v.resize(100);
    json lista = json::array();
    for (const auto* a : v) {
        lista.push_back({{"turma_id", a->idTurma}, {"turma", a->turmaNome},
                         {"data", data(a->data)}, {"texto", a->texto}});
    }
    return ok({{"novidades", lista}});
}

Resposta arquivo(mcp::Contexto& c, const std::string& idTurma, const std::string& idArquivo) {
    // O caminho sai do manifesto da pasta da turma, nunca da URL: os dois ids
    // só servem para ACHAR o arquivo na lista que o SIGAA publicou.
    for (const auto& a : c.snapshot().arquivos) {
        if (a.idTurma != idTurma || a.idArquivo != idArquivo) continue;
        const std::string cam = mcp::caminhoDoArquivo(c, a);
        if (cam.empty()) return erro(404, "este material ainda não foi baixado no computador");
        Resposta r;
        r.arquivo = cam;
        r.nomeArquivo = util::paraUtf8(util::deUtf8(cam).filename());
        r.tipo = mcp::tipoMime(r.nomeArquivo);
        return r;
    }
    return erro(404, "arquivo não encontrado");
}

// "/api/turmas/123/x" -> {"turmas", "123", "x"}
std::vector<std::string> segmentos(const std::string& caminho) {
    std::vector<std::string> v;
    size_t i = 5;   // depois de "/api/"
    while (i <= caminho.size()) {
        const size_t f = caminho.find('/', i);
        const std::string s = caminho.substr(i, f == std::string::npos ? std::string::npos : f - i);
        if (!s.empty()) v.push_back(s);
        if (f == std::string::npos) break;
        i = f + 1;
    }
    return v;
}

} // namespace

Resposta responder(const Fonte& f, const std::string& caminho,
                   const std::map<std::string, std::string>& query) {
    if (caminho.rfind("/api/", 0) != 0) return erro(404, "rota desconhecida");
    const auto seg = segmentos(caminho);
    if (seg.empty()) return erro(404, "rota desconhecida");

    // Aberto a cada pedido, como no MCP: o app sincroniza com o servidor no ar,
    // e a próxima tela já tem de mostrar o que chegou.
    store::Database db(f.banco, store::Database::Abertura::SoExistente);
    if (!db.aberto()) return erro(503, "o banco do app ainda não existe neste computador");
    mcp::Contexto c(db, f.materiais, "web", f.banco);

    const std::string& r = seg[0];
    if (seg.size() == 1) {
        if (r == "resumo") return resumo(c, db);
        if (r == "agenda") return agenda(c, query);
        if (r == "provas") return provas(c);
        if (r == "turmas") return turmas(c);
        if (r == "novidades") return novidades(c);
    }
    if (r == "turmas" && seg.size() == 2 && soDigitos(seg[1])) return turma(c, seg[1]);
    if (r == "arquivos" && seg.size() == 3 && soDigitos(seg[1]) && soDigitos(seg[2])) {
        return arquivo(c, seg[1], seg[2]);
    }
    return erro(404, "rota desconhecida");
}

} // namespace sigaa::web
