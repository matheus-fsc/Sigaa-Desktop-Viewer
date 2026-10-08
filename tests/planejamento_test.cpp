// Planejamento de estudo.
//
// O que importa aqui não é o número exato de minutos de cada sessão — esse é
// ajuste fino e vai mudar —, e sim as promessas que o aluno lê no plano:
// nunca estudar para a prova no dia dela ou depois, nunca passar das horas
// livres que ele informou, nunca perder um check, e dizer quando não cabe.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/planejamento/Planejamento.h"
#include "core/store/Database.h"

using namespace sigaa;
using namespace sigaa::planejamento;

namespace {

DateTime dia(int y, int m, int d) {
    DateTime t;
    t.year = y;
    t.month = m;
    t.day = d;
    return t;
}

// Quinta-feira.
const DateTime kHoje = dia(2026, 10, 1);

ProvaAlvo prova(const std::string& turma, const std::string& desc, DateTime data,
                int topicos = 6) {
    ProvaAlvo p;
    p.idTurma = turma;
    p.turmaNome = "Turma " + turma;
    p.descricao = desc;
    p.data = data;
    p.topicos = topicos;
    return p;
}

int minutosDaProva(const Plano& pl, const std::string& turma, const std::string& desc) {
    int m = 0;
    for (const auto& s : pl.sessoes) {
        if (s.idTurma == turma && s.prova == desc) m += s.minutos;
    }
    return m;
}

bool temDica(const Plano& pl, TipoDica t) {
    for (const auto& d : pl.dicas) {
        if (d.tipo == t) return true;
    }
    return false;
}

} // namespace

TEST_CASE("planejamento: dificuldade e topicos mudam o tempo necessario", "[planejamento]") {
    const auto p = prova("1", "P1", dia(2026, 10, 20), 6);
    CHECK(minutosNecessarios(p, Dificuldade::Facil) < minutosNecessarios(p, Dificuldade::Media));
    CHECK(minutosNecessarios(p, Dificuldade::Media) < minutosNecessarios(p, Dificuldade::Dificil));

    auto semTopicos = p;
    semTopicos.topicos = -1;
    CHECK(minutosNecessarios(semTopicos, Dificuldade::Media) ==
          minutosNecessarios(p, Dificuldade::Media));

    auto muitos = p;
    muitos.topicos = 500;
    CHECK(minutosNecessarios(muitos, Dificuldade::Dificil) <= 1200);
    CHECK(minutosNecessarios(p, Dificuldade::Media) % 30 == 0);
}

TEST_CASE("planejamento: sessoes so entre hoje e a vespera da prova", "[planejamento]") {
    const auto p = prova("1", "P1", dia(2026, 10, 15));
    const auto pl = planejar({p}, {}, Preferencias{}, {}, kHoje);

    REQUIRE_FALSE(pl.sessoes.empty());
    for (const auto& s : pl.sessoes) {
        CHECK_FALSE(s.dia < kHoje);
        CHECK(s.dia < p.data);
        CHECK(s.minutos % 30 == 0);
    }
    CHECK(minutosDaProva(pl, "1", "P1") == minutosNecessarios(p, Dificuldade::Media));
    CHECK(pl.deficit.empty());
}

TEST_CASE("planejamento: nunca passa das horas livres do dia", "[planejamento]") {
    Preferencias prefs;
    prefs.minutosPorDia = {60, 60, 60, 60, 60, 90, 0};
    const std::vector<ProvaAlvo> provas{prova("1", "P1", dia(2026, 10, 12), 12),
                                        prova("2", "P1", dia(2026, 10, 14), 12),
                                        prova("3", "P1", dia(2026, 10, 16), 12)};
    const auto pl = planejar(provas, {}, prefs, {}, kHoje);

    std::map<std::string, int> porDia;
    for (const auto& s : pl.sessoes) porDia[s.dia.toIso()] += s.minutos;
    // Domingo (0 min) não recebe nada.
    CHECK(porDia.count("2026-10-04") == 0);
    CHECK(porDia.count("2026-10-11") == 0);
    for (const auto& [d, m] : porDia) {
        INFO(d);
        CHECK(m <= 90);
    }
}

TEST_CASE("planejamento: aulas da grade saem do tempo de estudo", "[planejamento]") {
    Turma a;
    a.horario = "24M23";      // seg e qua, 2 aulas
    Turma b;
    b.horario = "2T3456";     // seg, 4 aulas
    const auto aulas = minutosDeAulaPorDia({a, b});
    CHECK(aulas[0] == 6 * kMinutosPorHoraAula);
    CHECK(aulas[2] == 2 * kMinutosPorHoraAula);
    CHECK(aulas[1] == 0);

    Preferencias prefs;
    prefs.minutosPorDia = {300, 300, 300, 300, 300, 300, 300};
    CHECK(minutosParaEstudo(prefs, aulas, 0) == 0);   // 6 horários passam de 300: nunca negativo
    CHECK(minutosParaEstudo(prefs, aulas, 2) == 300 - 2 * kMinutosPorHoraAula);
    CHECK(minutosParaEstudo(prefs, aulas, 1) == 300);

    // A segunda-feira, toda tomada por aula, não recebe sessão.
    const auto p = prova("1", "P1", dia(2026, 10, 20), 12);
    const auto pl = planejar({p}, {}, prefs, {}, kHoje, aulas);
    for (const auto& s : pl.sessoes) {
        INFO(s.dia.toIso());
        CHECK(s.dia.toIso() != "2026-10-05");
        CHECK(s.dia.toIso() != "2026-10-12");
        CHECK(s.dia.toIso() != "2026-10-19");
    }
}

TEST_CASE("planejamento: espalha a revisao em vez de empilhar num dia", "[planejamento]") {
    const auto p = prova("1", "P1", dia(2026, 10, 20), 12);
    const auto pl = planejar({p}, {}, Preferencias{}, {}, kHoje);

    std::set<std::string> dias;
    for (const auto& s : pl.sessoes) {
        CHECK(s.minutos <= 120);   // teto da mesma prova num dia (média)
        dias.insert(s.dia.toIso());
    }
    CHECK(dias.size() >= 3);
    // A véspera sempre tem revisão.
    CHECK(dias.count("2026-10-19") == 1);
}

TEST_CASE("planejamento: o que nao cabe vira deficit e a dica mais alta", "[planejamento]") {
    Preferencias prefs;
    prefs.minutosPorDia = {30, 30, 30, 30, 30, 30, 30};
    const auto p = prova("1", "P1", dia(2026, 10, 4), 20);
    const auto pl = planejar({p}, {}, prefs, {}, kHoje);

    REQUIRE(pl.deficit.count("1|P1") == 1);
    CHECK(pl.deficit.at("1|P1") > 0);
    REQUIRE_FALSE(pl.dicas.empty());
    CHECK(pl.dicas.front().tipo == TipoDica::Deficit);
}

TEST_CASE("planejamento: sessao feita fica e desconta da prova", "[planejamento]") {
    const auto p = prova("1", "P1", dia(2026, 10, 15));
    Sessao feita;
    feita.idTurma = "1";
    feita.turmaNome = "Turma 1";
    feita.prova = "P1";
    feita.dataProva = p.data;
    feita.dia = dia(2026, 9, 28);   // no passado
    feita.minutos = 90;
    feita.feita = true;

    const auto pl = planejar({p}, {}, Preferencias{}, {feita}, kHoje);
    int feitas = 0;
    std::set<std::string> chaves;
    for (const auto& s : pl.sessoes) {
        if (s.feita) ++feitas;
        CHECK(chaves.insert(s.chave()).second);   // chaves únicas
    }
    CHECK(feitas == 1);
    CHECK(minutosDaProva(pl, "1", "P1") == minutosNecessarios(p, Dificuldade::Media));
}

TEST_CASE("planejamento: sessao pendente que passou vira dica de atrasadas", "[planejamento]") {
    const auto p = prova("1", "P1", dia(2026, 10, 15));
    Sessao velha;
    velha.idTurma = "1";
    velha.prova = "P1";
    velha.dataProva = p.data;
    velha.dia = dia(2026, 9, 29);
    velha.minutos = 60;

    const auto pl = planejar({p}, {}, Preferencias{}, {velha}, kHoje);
    CHECK(temDica(pl, TipoDica::Atrasadas));
    for (const auto& s : pl.sessoes) CHECK_FALSE(s.dia < kHoje);
}

TEST_CASE("planejamento: provas no mesmo dia e aglomeradas geram dicas", "[planejamento]") {
    const std::vector<ProvaAlvo> provas{prova("1", "P1", dia(2026, 10, 21)),
                                        prova("2", "P1", dia(2026, 10, 21)),
                                        prova("3", "P1", dia(2026, 10, 23))};
    const auto pl = planejar(provas, {}, Preferencias{}, {}, kHoje);
    CHECK(temDica(pl, TipoDica::MesmoDia));
    CHECK(temDica(pl, TipoDica::Aglomerado));

    bool critica = false;
    for (const auto& w : pl.semanas) critica = critica || w.critica();
    CHECK(critica);
    CHECK(temDica(pl, TipoDica::Estrategia));
}

TEST_CASE("planejamento: data deduzida e entrega na vespera geram dicas", "[planejamento]") {
    auto p = prova("1", "P1", dia(2026, 10, 15));
    p.inferida = true;
    EntregaAlvo e;
    e.idTurma = "2";
    e.turmaNome = "Turma 2";
    e.titulo = "Lista 3";
    e.prazo = dia(2026, 10, 14);

    const auto pl = planejar({p}, {e}, Preferencias{}, {}, kHoje);
    CHECK(temDica(pl, TipoDica::Deduzida));
    CHECK(temDica(pl, TipoDica::EntregaNaVespera));
}

TEST_CASE("planejamento: mesmas entradas, mesmo plano", "[planejamento]") {
    const std::vector<ProvaAlvo> provas{prova("1", "P1", dia(2026, 10, 12), 8),
                                        prova("2", "P2", dia(2026, 10, 19), 15)};
    const auto a = planejar(provas, {}, Preferencias{}, {}, kHoje);
    const auto b = planejar(provas, {}, Preferencias{}, {}, kHoje);
    REQUIRE(a.sessoes.size() == b.sessoes.size());
    for (size_t i = 0; i < a.sessoes.size(); ++i) {
        CHECK(a.sessoes[i].chave() == b.sessoes[i].chave());
        CHECK(a.sessoes[i].minutos == b.sessoes[i].minutos);
    }
}

TEST_CASE("planejamento: prova passada e ignorada, semanas vao de hoje a ultima prova",
          "[planejamento]") {
    const std::vector<ProvaAlvo> provas{prova("1", "P1", dia(2026, 9, 20)),
                                        prova("2", "P1", dia(2026, 10, 28))};
    const auto pl = planejar(provas, {}, Preferencias{}, {}, kHoje);
    CHECK(minutosDaProva(pl, "1", "P1") == 0);
    REQUIRE_FALSE(pl.semanas.empty());
    CHECK(pl.semanas.front().inicio.toIso() == "2026-09-28");   // segunda de hoje
    CHECK(pl.semanas.back().inicio.toIso() == "2026-10-26");
}

TEST_CASE("planejamento: duracao legivel", "[planejamento]") {
    CHECK(duracao(45) == "45 min");
    CHECK(duracao(60) == "1h");
    CHECK(duracao(90) == "1h30");
}

TEST_CASE("planejamento: preferencias e sessoes no banco", "[planejamento][database]") {
    const auto caminho = std::filesystem::temp_directory_path() / "sigaa-teste-plano.db";
    std::filesystem::remove(caminho);
    {
        store::Database db(caminho.string());
        REQUIRE(db.migrar());

        // Sem nada gravado, os padrões.
        CHECK(db.carregarPreferenciasEstudo().minutosPorDia == Preferencias{}.minutosPorDia);

        Preferencias prefs;
        prefs.minutosPorDia = {30, 60, 90, 120, 150, 180, 0};
        prefs.dificuldade["1"] = Dificuldade::Dificil;
        REQUIRE(db.gravarPreferenciasEstudo(prefs));
        const auto lidas = db.carregarPreferenciasEstudo();
        CHECK(lidas.minutosPorDia == prefs.minutosPorDia);
        CHECK(lidas.dificuldadeDe("1") == Dificuldade::Dificil);
        CHECK(lidas.dificuldadeDe("outra") == Dificuldade::Media);

        const auto p = prova("1", "P1", dia(2026, 10, 15));
        const auto pl = planejar({p}, {}, lidas, {}, kHoje);
        REQUIRE(db.substituirSessoesEstudo(pl.sessoes));
        auto guardadas = db.carregarSessoesEstudo();
        REQUIRE(guardadas.size() == pl.sessoes.size());

        // O check sobrevive ao replano.
        const std::string chave = guardadas.front().chave();
        REQUIRE(db.marcarSessaoEstudo(chave, true, 1000));
        CHECK_FALSE(db.marcarSessaoEstudo("nao|existe|2026-01-01", true, 1000));

        const auto pl2 = planejar({p}, {}, lidas, db.carregarSessoesEstudo(), kHoje);
        REQUIRE(db.substituirSessoesEstudo(pl2.sessoes));
        bool achou = false;
        for (const auto& s : db.carregarSessoesEstudo()) {
            if (s.chave() == chave) {
                achou = true;
                CHECK(s.feita);
            }
        }
        CHECK(achou);
    }
    std::error_code ec;
    std::filesystem::remove(caminho, ec);
}

// --- o que os agentes de IA registraram ------------------------------------

namespace {

estudo::RegistroEstudo estudoDoAgente(const std::string& turma, int minutos, DateTime quando,
                                      const std::string& prova = {}) {
    estudo::RegistroEstudo r;
    r.origem = "claude-code";
    r.idTurma = turma;
    r.minutos = minutos;
    r.quando = quando;
    r.prova = prova;
    return r;
}

} // namespace

TEST_CASE("planejamento: estudo registrado pelo agente desconta da prova", "[planejamento]") {
    const auto p1 = prova("1", "Prova 1", dia(2026, 10, 15));
    const auto p2 = prova("1", "Prova 2", dia(2026, 11, 20));
    const int precisa = minutosNecessarios(p1, Dificuldade::Media);

    DoAgente ag;
    ag.estudos = {estudoDoAgente("1", 60, dia(2026, 9, 30), "P1"),        // "P1" = Prova 1
                  estudoDoAgente("1", 90, dia(2026, 9, 30)),              // sem prova: a próxima (P1)
                  estudoDoAgente("1", 45, dia(2026, 10, 20), "Prova 1")}; // depois da prova: não conta
    const auto pl = planejar({p1, p2}, {}, Preferencias{}, {}, kHoje, {}, ag);

    CHECK(pl.comAgente.at("1|Prova 1") == 150);
    CHECK(minutosDaProva(pl, "1", "Prova 1") == precisa - 150);
    // "Prova 1" depois da Prova 1 não vai para a Prova 2.
    CHECK(pl.comAgente.count("1|Prova 2") == 0);
}

TEST_CASE("planejamento: foco aberto pede mais tempo e vira dica", "[planejamento]") {
    const auto p = prova("1", "Prova 1", dia(2026, 10, 15));
    const int base = minutosNecessarios(p, Dificuldade::Media);

    estudo::PontoFoco f;
    f.idTurma = "1";
    f.topico = "FIRST e FOLLOW";
    f.nivel = 2;
    f.motivo = "errou 2 de 3";
    DoAgente ag;
    ag.focos = {f};
    auto pl = planejar({p}, {}, Preferencias{}, {}, kHoje, {}, ag);
    CHECK(minutosDaProva(pl, "1", "Prova 1") == base + 60);
    REQUIRE(temDica(pl, TipoDica::Foco));
    for (const auto& d : pl.dicas) {
        if (d.tipo == TipoDica::Foco) CHECK(d.texto.find("FIRST e FOLLOW") != std::string::npos);
    }

    // Resolvido, não pesa nem aparece.
    ag.focos[0].resolvido = true;
    pl = planejar({p}, {}, Preferencias{}, {}, kHoje, {}, ag);
    CHECK(minutosDaProva(pl, "1", "Prova 1") == base);
    CHECK_FALSE(temDica(pl, TipoDica::Foco));
}

TEST_CASE("planejamento: sessao extra do agente reserva o dia e soma a prova", "[planejamento]") {
    const auto p = prova("1", "Prova 1", dia(2026, 10, 15));
    const int base = minutosNecessarios(p, Dificuldade::Media);

    estudo::Proposta q;
    q.tipo = estudo::TipoProposta::Sessao;
    q.estado = estudo::EstadoProposta::Aceita;
    q.idTurma = "1";
    q.dia = dia(2026, 10, 3);
    q.para = 60;
    q.topico = "Laplace";
    DoAgente ag;
    ag.propostas = {q};
    auto pl = planejar({p}, {}, Preferencias{}, {}, kHoje, {}, ag);
    CHECK(minutosDaProva(pl, "1", "Prova 1") == base + 60);
    int noDia = 0;
    for (const auto& s : pl.sessoes) {
        if (s.dia.toIso() == "2026-10-03") noDia += s.minutos;
    }
    CHECK(noDia >= 60);

    // Pendente ou desfeita não entra.
    for (auto e : {estudo::EstadoProposta::Pendente, estudo::EstadoProposta::Desfeita}) {
        ag.propostas[0].estado = e;
        pl = planejar({p}, {}, Preferencias{}, {}, kHoje, {}, ag);
        CHECK(minutosDaProva(pl, "1", "Prova 1") == base);
    }
}

TEST_CASE("planejamento: pendente do passado fica guardada com desde", "[planejamento]") {
    const auto caminho = (std::filesystem::temp_directory_path() / "sigaa-teste-desde.db").string();
    std::filesystem::remove(caminho);
    {
        store::Database db(caminho);
        REQUIRE(db.migrar());
        Sessao velha;
        velha.idTurma = "1";
        velha.prova = "Prova 1";
        velha.dataProva = dia(2026, 10, 15);
        velha.dia = dia(2026, 9, 28);
        velha.minutos = 60;
        Sessao nova = velha;
        nova.dia = dia(2026, 10, 2);
        REQUIRE(db.substituirSessoesEstudo({velha, nova}));
        // Replano: a velha (antes de hoje) fica; a nova é trocada.
        REQUIRE(db.substituirSessoesEstudo({}, "2026-10-01"));
        const auto ss = db.carregarSessoesEstudo();
        REQUIRE(ss.size() == 1);
        CHECK(ss[0].dia.toIso() == "2026-09-28");
    }
    std::filesystem::remove(caminho);
}
