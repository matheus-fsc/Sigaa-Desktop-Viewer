// Onde o aluno esta em cada turma (core/estudo/Situacao.h): faltas contra o
// limite e notas contra a media de 6,0 (60%, regra da UNIFEI).
//
// Os numeros de faltas sao os do banco real de 07/10/2026: quatro turmas com
// 16 de 16 e Eletromagnetismo com 20 de 16.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/estudo/Situacao.h"

using namespace sigaa;
using Catch::Matchers::WithinAbs;

namespace {

Frequencia freq(int presencas, int comRegistro, int pelaCH) {
    Frequencia f;
    f.presencas = presencas;
    f.aulasComRegistro = comRegistro;
    f.aulasPelaCH = pelaCH;
    f.temDados = true;
    return f;
}

UnidadeNota unidade(int numero, std::optional<double> nota) {
    UnidadeNota u;
    u.numero = numero;
    u.nota = nota;
    return u;
}

} // namespace

TEST_CASE("situacao: faltas", "[situacao]") {
    CHECK(estudo::situacaoFaltas(nullptr).risco == estudo::RiscoFalta::SemDados);

    Frequencia vazia;
    CHECK(estudo::situacaoFaltas(&vazia).risco == estudo::RiscoFalta::SemDados);

    const auto passou = freq(14, 34, 64);   // 20 de 16
    const auto s = estudo::situacaoFaltas(&passou);
    CHECK(s.risco == estudo::RiscoFalta::Reprovado);
    CHECK(s.restam == -4);
    CHECK(s.aulasRestantes == 30);

    const auto limite = freq(16, 32, 64);   // 16 de 16
    CHECK(estudo::situacaoFaltas(&limite).risco == estudo::RiscoFalta::NoLimite);

    const auto perto = freq(18, 30, 64);    // 12 de 16, restam 4
    CHECK(estudo::situacaoFaltas(&perto).risco == estudo::RiscoFalta::Atencao);

    const auto folga = freq(18, 24, 64);    // 6 de 16
    CHECK(estudo::situacaoFaltas(&folga).risco == estudo::RiscoFalta::Folgado);
}

TEST_CASE("situacao: notas e quanto falta para 6", "[situacao]") {
    CHECK(estudo::situacaoNotas(nullptr).risco == estudo::RiscoNota::SemNota);

    Notas n;
    n.unidades = {unidade(1, 5.0), unidade(2, std::nullopt), unidade(3, std::nullopt)};
    auto s = estudo::situacaoNotas(&n);
    CHECK(s.lancadas == 1);
    CHECK_THAT(*s.mediaParcial, WithinAbs(5.0, 1e-9));
    CHECK_THAT(*s.precisa, WithinAbs(6.5, 1e-9));   // (18 - 5) / 2
    CHECK(s.risco == estudo::RiscoNota::Atencao);

    n.unidades = {unidade(1, 0.0), unidade(2, std::nullopt)};
    s = estudo::situacaoNotas(&n);
    CHECK_THAT(*s.precisa, WithinAbs(12.0, 1e-9));
    CHECK(s.risco == estudo::RiscoNota::SoReposicao);
    REQUIRE(s.zeros.size() == 1);
    CHECK(s.zeros[0] == "Unid. 1");
    CHECK(s.soZeros);

    n.unidades = {unidade(1, 7.0), unidade(2, 6.0)};
    CHECK(estudo::situacaoNotas(&n).risco == estudo::RiscoNota::Aprovado);
    n.unidades = {unidade(1, 4.0), unidade(2, 6.0)};
    CHECK(estudo::situacaoNotas(&n).risco == estudo::RiscoNota::Abaixo);
}

TEST_CASE("situacao: zero de avaliacao vai para a lista de perguntas", "[situacao]") {
    Notas n;
    auto u = unidade(1, 0.0);
    u.avaliacoes.push_back({"T1", "Trabalho 1", 15.0, std::nullopt, 0.0});
    u.avaliacoes.push_back({"P1", "Avaliacao 1", 35.0, std::nullopt, 0.0});
    n.unidades = {u, unidade(2, std::nullopt)};
    const auto s = estudo::situacaoNotas(&n);
    REQUIRE(s.zeros.size() == 2);
    CHECK(s.zeros[0] == "T1 (Unid. 1)");
    CHECK(s.zeros[1] == "P1 (Unid. 1)");
    CHECK(s.soZeros);
}
