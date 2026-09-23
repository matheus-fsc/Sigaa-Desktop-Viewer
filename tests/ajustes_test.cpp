// Correcoes do aluno sobre datas de prova.
//
// A parte que erra em silencio aqui e o CONFLITO: o app tem de distinguir "eu
// corrigi o professor" de "o professor acabou de me corrigir", e as duas
// situacoes tem exatamente a mesma aparencia se olharmos so para "a data do
// aluno difere da do SIGAA". Quase todo teste deste arquivo existe por causa
// dessa distincao.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/avaliacao/Ajustes.h"

using namespace sigaa;
using namespace sigaa::avaliacao;

namespace {

DateTime dia(int y, int m, int d) {
    DateTime t;
    t.year = y;
    t.month = m;
    t.day = d;
    return t;
}

Avaliacao prova(const std::string& idTurma, const std::string& desc, DateTime quando,
                FonteAvaliacao fonte = FonteAvaliacao::PainelAvaliacoes) {
    Avaliacao a;
    a.idTurma = idTurma;
    a.turmaNome = "INTELIGENCIA ARTIFICIAL";
    a.descricao = desc;
    a.quando = quando;
    a.fonte = fonte;
    return a;
}

Ajuste correcao(const std::string& idTurma, const std::string& desc, DateTime nova,
                DateTime sigaaNaEpoca) {
    Ajuste a;
    a.idTurma = idTurma;
    a.descricao = desc;
    a.quando = nova;
    a.quandoSigaaNaEpoca = sigaaNaEpoca;
    return a;
}

const Efetiva* acha(const std::vector<Efetiva>& v, const std::string& desc) {
    for (const auto& e : v) {
        if (e.av.descricao == desc) return &e;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Cenario 1: o professor remarca em sala e nao atualiza a plataforma
// ---------------------------------------------------------------------------

TEST_CASE("a correcao do aluno vence enquanto o SIGAA nao mexer", "[ajustes]") {
    const std::vector<Avaliacao> sigaa = {prova("1", "Prova 1", dia(2026, 9, 20))};
    const std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};

    const auto v = efetivas(sigaa, ajs);
    REQUIRE(v.size() == 1);
    CHECK(v[0].av.quando.toIso() == "2026-09-27");
    CHECK(v[0].estado == Estado::Editada);
    // O que o SIGAA diz continua disponivel: a correcao tem de poder ser
    // CONFERIDA, nao so acreditada.
    CHECK(v[0].quandoSigaa.toIso() == "2026-09-20");
}

TEST_CASE("sem o SIGAA mexer, nenhum sync aposenta a correcao", "[ajustes]") {
    // O caso mais comum do cenario relatado: o professor nunca atualiza. A
    // correcao tem de sobreviver a sincronizacoes indefinidas — se um ciclo
    // qualquer a derrubasse, o aluno voltaria a ver a data errada sem entender
    // por que.
    const std::vector<Avaliacao> sigaa = {prova("1", "Prova 1", dia(2026, 9, 20))};
    std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};

    for (int ciclo = 0; ciclo < 10; ++ciclo) {
        auto r = reconciliar(sigaa, ajs);
        CHECK(r.conflitos.empty());
        CHECK_FALSE(r.mudou);
        ajs = r.ajustes;
    }
    REQUIRE(ajs.size() == 1);
    CHECK(ajs[0].ativo);
    CHECK(efetivas(sigaa, ajs)[0].av.quando.toIso() == "2026-09-27");
}

// ---------------------------------------------------------------------------
// Cenario 2: o professor remarca E atualiza a plataforma
// ---------------------------------------------------------------------------

TEST_CASE("o SIGAA atropela a correcao e isso vira conflito", "[ajustes]") {
    std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};
    // O professor finalmente mexeu — e nao para a data que o aluno tinha.
    const std::vector<Avaliacao> sigaa = {prova("1", "Prova 1", dia(2026, 10, 4))};

    const auto r = reconciliar(sigaa, ajs);
    REQUIRE(r.conflitos.size() == 1);
    CHECK(r.conflitos[0].doAluno.toIso() == "2026-09-27");
    CHECK(r.conflitos[0].doSigaaAntes.toIso() == "2026-09-20");
    CHECK(r.conflitos[0].doSigaaAgora.toIso() == "2026-10-04");
    CHECK(r.mudou);

    // O ajuste e aposentado, nao apagado: e dele que sai "restaurar minha data".
    REQUIRE(r.ajustes.size() == 1);
    CHECK_FALSE(r.ajustes[0].ativo);
    CHECK(r.ajustes[0].quando.toIso() == "2026-09-27");

    // E a data que passa a valer e a do SIGAA.
    const auto v = efetivas(sigaa, r.ajustes);
    REQUIRE(v.size() == 1);
    CHECK(v[0].av.quando.toIso() == "2026-10-04");
}

TEST_CASE("professor chegando na mesma data do aluno nao e conflito", "[ajustes]") {
    // O aluno ouviu em sala que ficou para 27/09 e corrigiu; semanas depois o
    // professor cadastra 27/09. Ninguem discorda de ninguem — alarmar aqui
    // seria o alarme falso que ensina a ignorar os verdadeiros.
    std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};
    const std::vector<Avaliacao> sigaa = {prova("1", "Prova 1", dia(2026, 9, 27))};

    const auto r = reconciliar(sigaa, ajs);
    CHECK(r.conflitos.empty());
    // Mas a correcao e aposentada: ela deixou de ter o que corrigir, e manter
    // um ajuste identico ao SIGAA so daria trabalho ao proximo conflito.
    CHECK(r.mudou);
    REQUIRE(r.ajustes.size() == 1);
    CHECK_FALSE(r.ajustes[0].ativo);
    CHECK(efetivas(sigaa, r.ajustes)[0].av.quando.toIso() == "2026-09-27");
}

TEST_CASE("coleta sem a prova nao aposenta a correcao", "[ajustes]") {
    // Um sync sem --turmas traz zero avaliacoes. Tratar isso como "o professor
    // apagou a prova" aposentaria todas as correcoes de uma vez, em silencio.
    std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};

    const auto r = reconciliar({}, ajs);
    CHECK(r.conflitos.empty());
    CHECK_FALSE(r.mudou);
    CHECK(r.ajustes[0].ativo);
}

// ---------------------------------------------------------------------------
// Cenario 3: a inferida, que nao tinha como ser confirmada
// ---------------------------------------------------------------------------

TEST_CASE("inferida nasce marcada como inferida", "[ajustes]") {
    const std::vector<Avaliacao> sigaa = {
        prova("1", "Prova 1 - 20/09", dia(2026, 9, 20), FonteAvaliacao::TopicoAula)};
    const auto v = efetivas(sigaa, {});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == Estado::Inferida);
}

TEST_CASE("confirmar uma inferida muda o estado, nao a data", "[ajustes]") {
    const std::vector<Avaliacao> sigaa = {
        prova("1", "Prova 1 - 20/09", dia(2026, 9, 20), FonteAvaliacao::TopicoAula)};

    Ajuste aj;
    aj.idTurma = "1";
    aj.descricao = "Prova 1 - 20/09";
    aj.confirmada = true;
    aj.quandoSigaaNaEpoca = dia(2026, 9, 20);

    const auto v = efetivas(sigaa, {aj});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == Estado::Confirmada);
    CHECK(v[0].av.quando.toIso() == "2026-09-20");
}

TEST_CASE("confirmar e depois corrigir mostra a correcao", "[ajustes]") {
    const std::vector<Avaliacao> sigaa = {
        prova("1", "Prova 1 - 20/09", dia(2026, 9, 20), FonteAvaliacao::TopicoAula)};

    Ajuste aj = correcao("1", "Prova 1 - 20/09", dia(2026, 9, 25), dia(2026, 9, 20));
    aj.confirmada = true;

    const auto v = efetivas(sigaa, {aj});
    // Editada vence Confirmada: a data mudou, e e a mudanca que o aluno precisa
    // ver destacada.
    CHECK(v[0].estado == Estado::Editada);
    CHECK(v[0].av.quando.toIso() == "2026-09-25");
}

// ---------------------------------------------------------------------------
// Prova que o SIGAA nunca teve
// ---------------------------------------------------------------------------

TEST_CASE("prova criada pelo aluno entra na lista", "[ajustes]") {
    Ajuste aj;
    aj.idTurma = "9";
    aj.turmaNome = "COMPILADORES";
    aj.descricao = "Prova surpresa anunciada em sala";
    aj.quando = dia(2026, 10, 1);
    aj.horarioBruto = "15:45";
    aj.criadaPeloAluno = true;
    aj.nota = "anunciada na aula de 12/09";

    const auto v = efetivas({}, {aj});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == Estado::Criada);
    CHECK(v[0].av.turmaNome == "COMPILADORES");
    CHECK(v[0].av.horarioBruto == "15:45");
    CHECK(v[0].nota == "anunciada na aula de 12/09");
    // O SIGAA nao diz nada sobre ela: inventar uma data aqui produziria um
    // conflito que nao existe.
    CHECK_FALSE(v[0].quandoSigaa.valid());
}

TEST_CASE("prova criada pelo aluno nunca entra em conflito", "[ajustes]") {
    Ajuste aj;
    aj.idTurma = "9";
    aj.descricao = "Prova surpresa";
    aj.quando = dia(2026, 10, 1);
    aj.criadaPeloAluno = true;

    // Mesmo que o SIGAA passe a ter uma prova com a MESMA descricao nessa
    // turma, a criada pelo aluno nao e uma correcao de nada.
    const std::vector<Avaliacao> sigaa = {prova("9", "Prova surpresa", dia(2026, 11, 1))};
    const auto r = reconciliar(sigaa, {aj});
    CHECK(r.conflitos.empty());
    CHECK(r.ajustes[0].ativo);
}

// ---------------------------------------------------------------------------
// Convivencia com a mescla painel/topico que ja existia
// ---------------------------------------------------------------------------

TEST_CASE("a correcao se aplica depois da mescla, nao antes", "[ajustes]") {
    // Painel e topico descrevendo o mesmo dia: a mescla deixa so o painel. Se
    // o ajuste fosse aplicado antes, a prova apareceria duas vezes — uma
    // corrigida e outra nao, e o aluno teria de adivinhar qual vale.
    const std::vector<Avaliacao> sigaa = {
        prova("1", "Prova 1", dia(2026, 9, 20)),
        prova("1", "Prova 1 - dia 20", dia(2026, 9, 20), FonteAvaliacao::TopicoAula),
    };
    const std::vector<Ajuste> ajs = {
        correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20))};

    const auto v = efetivas(sigaa, ajs);
    REQUIRE(v.size() == 1);
    CHECK(v[0].av.descricao == "Prova 1");
    CHECK(v[0].av.quando.toIso() == "2026-09-27");
}

TEST_CASE("a lista sai em ordem cronologica com as criadas no meio", "[ajustes]") {
    const std::vector<Avaliacao> sigaa = {
        prova("1", "Prova 2", dia(2026, 11, 10)),
        prova("1", "Prova 1", dia(2026, 9, 20)),
    };
    Ajuste criada;
    criada.idTurma = "2";
    criada.descricao = "Prova de COMPILADORES";
    criada.quando = dia(2026, 10, 5);
    criada.criadaPeloAluno = true;

    const auto v = efetivas(sigaa, {criada});
    REQUIRE(v.size() == 3);
    CHECK(v[0].av.descricao == "Prova 1");
    CHECK(v[1].av.descricao == "Prova de COMPILADORES");
    CHECK(v[2].av.descricao == "Prova 2");
}

TEST_CASE("ajuste aposentado nao afeta a lista", "[ajustes]") {
    Ajuste aj = correcao("1", "Prova 1", dia(2026, 9, 27), dia(2026, 9, 20));
    aj.ativo = false;

    const std::vector<Avaliacao> sigaa = {prova("1", "Prova 1", dia(2026, 10, 4))};
    const auto v = efetivas(sigaa, {aj});
    REQUIRE(v.size() == 1);
    CHECK(v[0].av.quando.toIso() == "2026-10-04");
    CHECK(v[0].estado == Estado::DoSigaa);
}
