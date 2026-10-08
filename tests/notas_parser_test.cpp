// "Ver Notas" da turma
//
// A fixture e HTML CRU DA REDE (tests/fixtures/notas_rede.html, capturada em
// 07/10/2026 em FUNDAMENTOS DE ELETROMAGNETISMO), passada por tools/redact.py.
//
// O que ela tem de valioso: uma unidade COM avaliacoes cadastradas (T1 peso 15,
// P1 peso 35) e outra SEM (so a nota da unidade); zeros LANCADOS ("0,0") ao
// lado de celulas EM BRANCO. E a coluna Faltas dizendo 0 no dia em que o mapa
// de frequencia da turma contava 20 — por isso ela nao serve de frequencia.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "core/parse/NotasParser.h"

using namespace sigaa;
using Catch::Matchers::WithinAbs;

namespace {

std::string lerFixture(const std::string& nome) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + nome, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

parse::ResultadoNotas doFixture() {
    html::Document doc;
    REQUIRE(doc.parse(html::toUtf8(lerFixture("notas_rede.html"))));
    return parse::parseNotas(doc, "87854", "FUNDAMENTOS DE ELETROMAGNETISMO");
}

} // namespace

TEST_CASE("notas: reconhece a pagina", "[notas]") {
    const auto r = doFixture();
    CHECK(r.pareceNotas);
    CHECK(r.notas.idTurma == "87854");
}

TEST_CASE("notas: unidades e avaliacoes vem do cabecalho", "[notas]") {
    const auto n = doFixture().notas;
    REQUIRE(n.unidades.size() == 2);

    const auto& u1 = n.unidades[0];
    CHECK(u1.numero == 1);
    CHECK(u1.metodo == 'P');
    REQUIRE(u1.avaliacoes.size() == 2);
    CHECK(u1.avaliacoes[0].abrev == "T1");
    CHECK(u1.avaliacoes[0].denominacao == "Trabalho 1");
    REQUIRE(u1.avaliacoes[0].peso);
    CHECK_THAT(*u1.avaliacoes[0].peso, WithinAbs(15, 1e-9));
    CHECK(u1.avaliacoes[1].abrev == "P1");
    CHECK_THAT(*u1.avaliacoes[1].peso, WithinAbs(35, 1e-9));
    CHECK_FALSE(u1.avaliacoes[1].notaMaxima);   // input vazio

    CHECK(n.unidades[1].numero == 2);
    CHECK(n.unidades[1].avaliacoes.empty());
}

TEST_CASE("notas: zero lancado nao e nota ausente", "[notas]") {
    const auto n = doFixture().notas;
    const auto& u1 = n.unidades[0];
    REQUIRE(u1.avaliacoes[0].nota);
    CHECK_THAT(*u1.avaliacoes[0].nota, WithinAbs(0, 1e-9));
    REQUIRE(u1.avaliacoes[1].nota);
    REQUIRE(u1.nota);
    CHECK_THAT(*u1.nota, WithinAbs(0, 1e-9));

    // Unid. 2, Reposicao e Resultado estao em branco: nao lancados.
    CHECK_FALSE(n.unidades[1].nota);
    CHECK_FALSE(n.reposicao);
    CHECK_FALSE(n.resultado);
    CHECK(n.temNota());
}

TEST_CASE("notas: colunas finais", "[notas]") {
    const auto n = doFixture().notas;
    REQUIRE(n.faltas);
    CHECK(*n.faltas == 0);   // e o mapa dizia 20 — ver Notas::faltas
    CHECK(n.situacao == "--");
}

TEST_CASE("notas: outra tela nao parece notas", "[notas]") {
    html::Document doc;
    REQUIRE(doc.parse("<html><body><fieldset><legend>Frequencia</legend></fieldset></body></html>"));
    const auto r = parse::parseNotas(doc, "1", "X");
    CHECK_FALSE(r.pareceNotas);
    CHECK(r.notas.unidades.empty());
    CHECK_FALSE(r.notas.temNota());
}

TEST_CASE("notas: numero brasileiro", "[notas]") {
    CHECK_THAT(*parse::numeroBr("7,5"), WithinAbs(7.5, 1e-9));
    CHECK_THAT(*parse::numeroBr(" 10,0 "), WithinAbs(10, 1e-9));
    CHECK_FALSE(parse::numeroBr(""));
    CHECK_FALSE(parse::numeroBr("  \n "));
    CHECK_FALSE(parse::numeroBr("--"));
}
