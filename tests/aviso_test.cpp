// Testes da politica de notificacao. Logica pura: nenhuma chamada ao SO.
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/notify/Aviso.h"

using namespace sigaa;
using namespace sigaa::notify;

namespace {

sync::Evento ev(sync::TipoEvento t, const std::string& titulo,
                const std::string& turma = "EDO") {
    sync::Evento e;
    e.tipo = t;
    e.titulo = titulo;
    e.turmaNome = turma;
    e.chave = titulo;
    return e;
}

} // namespace

TEST_CASE("sem novidade nao interrompe o usuario", "[aviso]") {
    sync::ResultadoDiff d;
    CHECK_FALSE(montarAviso(d).has_value());
}

TEST_CASE("primeira execucao nao notifica", "[aviso]") {
    // Mesma razao do DiffEngine: a linha de base nao e novidade. Sem isso o
    // primeiro sync dispararia um alerta com dezenas de itens.
    sync::ResultadoDiff d;
    d.primeiraExecucao = true;
    d.eventos = {ev(sync::TipoEvento::AtividadeNova, "Tarefa 1")};
    CHECK_FALSE(montarAviso(d).has_value());
}

TEST_CASE("coleta suspeita notifica e e urgente", "[aviso]") {
    // O pior modo de falha e o silencioso: o parser quebra e o aluno conclui
    // que nao tem prazo. Este e o unico caso em que avisamos sobre uma falha.
    sync::ResultadoDiff d;
    d.suspeito = true;
    d.eventos = {ev(sync::TipoEvento::ColetaSuspeita, "A coleta parece incompleta")};

    const auto a = montarAviso(d);
    REQUIRE(a.has_value());
    CHECK(a->urgente);
    // Assercoes em ASCII: o corpo e UTF-8 e "ausencia" nao casa com "ausência".
    CHECK(a->corpo.find("vazia") != std::string::npos);
    CHECK(a->corpo.find("SIGAA") != std::string::npos);
}

TEST_CASE("suspeita vence mesmo com outros eventos na lista", "[aviso]") {
    sync::ResultadoDiff d;
    d.suspeito = true;
    d.eventos = {ev(sync::TipoEvento::AtividadeNova, "Tarefa 1"),
                 ev(sync::TipoEvento::ColetaSuspeita, "incompleta")};

    const auto a = montarAviso(d);
    REQUIRE(a.has_value());
    // Nao pode virar "2 novidades": nao ha novidade, ha falha.
    CHECK(a->titulo.find("novidades") == std::string::npos);
}

TEST_CASE("um evento vira aviso especifico", "[aviso]") {
    sync::ResultadoDiff d;
    auto e = ev(sync::TipoEvento::AvaliacaoRemarcada, "1a Avaliacao");
    e.detalhe = "de 2026-09-29 para 2026-10-06";
    d.eventos = {e};

    const auto a = montarAviso(d);
    REQUIRE(a.has_value());
    CHECK(a->urgente);
    CHECK(a->corpo.find("1a Avaliacao") != std::string::npos);
    CHECK(a->corpo.find("EDO") != std::string::npos);
    CHECK(a->corpo.find("2026-10-06") != std::string::npos);
}

TEST_CASE("varios eventos viram UM aviso agregado", "[aviso]") {
    // A regra que protege a notificacao de virar ruido: um toast por ciclo.
    sync::ResultadoDiff d;
    d.eventos = {ev(sync::TipoEvento::AtualizacaoNova, "Novo arquivo"),
                 ev(sync::TipoEvento::AtividadeNova, "Tarefa 2"),
                 ev(sync::TipoEvento::AtividadeNova, "Tarefa 3"),
                 ev(sync::TipoEvento::AtividadeNova, "Tarefa 4"),
                 ev(sync::TipoEvento::AtividadeNova, "Tarefa 5")};

    const auto a = montarAviso(d);
    REQUIRE(a.has_value());
    CHECK(a->titulo.find("5 novidades") != std::string::npos);
    // Corpo cortado em 3 linhas + o resumo do resto.
    CHECK(a->corpo.find("e mais 2") != std::string::npos);
}

TEST_CASE("o mais urgente aparece primeiro no corpo", "[aviso]") {
    // Se o aviso for cortado, o que sobra tem que ser o que importa.
    sync::ResultadoDiff d;
    d.eventos = {ev(sync::TipoEvento::AtividadeConcluida, "Tarefa velha"),
                 ev(sync::TipoEvento::AtualizacaoNova, "Novo arquivo"),
                 ev(sync::TipoEvento::AtividadeNova, "Tarefa nova"),
                 ev(sync::TipoEvento::AvaliacaoRemarcada, "Prova 1")};

    const auto a = montarAviso(d);
    REQUIRE(a.has_value());
    const auto posProva = a->corpo.find("Prova 1");
    REQUIRE(posProva != std::string::npos);
    CHECK(posProva < a->corpo.find("Tarefa nova"));
    // "atividade concluida" e a menos urgente: nao cabe nas 3 primeiras linhas
    CHECK(a->corpo.find("Tarefa velha") == std::string::npos);
}

TEST_CASE("so o que tem data marca urgencia", "[aviso]") {
    sync::ResultadoDiff calmo;
    calmo.eventos = {ev(sync::TipoEvento::AtualizacaoNova, "Novo arquivo"),
                     ev(sync::TipoEvento::TurmaNova, "ADM")};
    const auto a = montarAviso(calmo);
    REQUIRE(a.has_value());
    CHECK_FALSE(a->urgente);

    sync::ResultadoDiff comPrazo;
    comPrazo.eventos = {ev(sync::TipoEvento::AtualizacaoNova, "Novo arquivo"),
                        ev(sync::TipoEvento::PrazoMudou, "Tarefa 1")};
    const auto b = montarAviso(comPrazo);
    REQUIRE(b.has_value());
    CHECK(b->urgente);
}

TEST_CASE("prioridade e total e sem empate entre tipos distintos", "[aviso]") {
    using T = sync::TipoEvento;
    const T todos[] = {T::TurmaNova,       T::AtividadeNova,   T::PrazoMudou,
                       T::AtividadeConcluida, T::AtualizacaoNova, T::AvaliacaoNova,
                       T::AvaliacaoRemarcada, T::ColetaSuspeita};
    // Um tipo novo sem entrada no switch cairia no default 9 e empataria —
    // este teste falha se alguem esquecer de classificar.
    for (const T a : todos) {
        CHECK(prioridade(a) < 9);
        CHECK_FALSE(rotulo(a).empty());
        for (const T b : todos) {
            if (a != b) CHECK(prioridade(a) != prioridade(b));
        }
    }
}
