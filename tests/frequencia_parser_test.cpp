// Mapa de frequencia — /sigaa/ava/FrequenciaAluno/mapa.jsf
//
// A fixture e HTML CRU DA REDE (tests/fixtures/frequencia_rede.html), passada
// por tools/redact.py. O valor dela esta no rodape: o SIGAA publica os totais
// que ele mesmo calculou, e e contra esses numeros que a leitura das linhas
// pode ser conferida.
//
// A CONTA QUE FECHA, e que justifica o desenho do parser: 13 linhas, das quais
// 12 tem registro; 12 x 2 aulas = 24 = "Aulas com Registro de Frequencia".
// 9 dessas 12 sao "Presente"; 9 x 2 = 18 = "Presencas Registradas". A 13a e
// "Nao Registrada" e nao entra em nenhum dos dois. E por isso que os totais
// vem do rodape e nao da soma das linhas: a linha "Presente" NAO diz que vale
// 2 aulas, e nao ha como descobrir isso linha a linha.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "core/parse/FrequenciaParser.h"

using namespace sigaa;

namespace {

std::string lerFixture(const std::string& nome) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + nome, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

parse::ResultadoFrequencia doFixture() {
    html::Document doc;
    REQUIRE(doc.parse(lerFixture("frequencia_rede.html")));
    return parse::parseFrequencia(doc, "T1", "COMPILADORES");
}

parse::ResultadoFrequencia doHtml(const std::string& html) {
    html::Document doc;
    REQUIRE(doc.parse(html));
    return parse::parseFrequencia(doc, "T1", "COMPILADORES");
}

}  // namespace

TEST_CASE("le os totais que o SIGAA publica no rodape", "[frequencia]") {
    const auto r = doFixture();
    REQUIRE(r.pareceMapaDeFrequencia);
    CHECK(r.frequencia.presencas == 18);
    CHECK(r.frequencia.aulasComRegistro == 24);
    CHECK(r.frequencia.aulasPelaCH == 64);
    CHECK(r.frequencia.temDados);
}

TEST_CASE("as faltas saem da subtracao do proprio SIGAA", "[frequencia]") {
    const auto r = doFixture();
    // 24 aulas registradas - 18 presencas = 6 faltas.
    CHECK(r.frequencia.faltas() == 6);
}

TEST_CASE("o limite e 25% da carga horaria do componente", "[frequencia]") {
    // A regra esta escrita na propria pagina: aprovado com presenca >= 75% da
    // carga horaria. 64 aulas -> 16 faltas.
    const auto r = doFixture();
    CHECK(r.frequencia.limiteFaltas() == 16);
    CHECK_FALSE(r.frequencia.reprovado());
}

TEST_CASE("uma disciplina de 32 aulas tem limite 8", "[frequencia]") {
    Frequencia f;
    f.aulasPelaCH = 32;
    CHECK(f.limiteFaltas() == 8);
}

TEST_CASE("o limite nao encolhe porque o diario esta atrasado", "[frequencia]") {
    // O professor so lancou 24 das 64 aulas. O teto de faltas continua sendo o
    // do semestre inteiro — calcula-lo sobre o que ja foi lancado daria 6 e
    // faria o app anunciar reprovacao no meio do semestre.
    const auto r = doFixture();
    CHECK(r.frequencia.aulasComRegistro == 24);
    CHECK(r.frequencia.limiteFaltas() == 16);
}

TEST_CASE("exatamente no limite ainda nao reprova", "[frequencia]") {
    // 16 de 64 deixa a presenca em 75%, e a regra exige presenca IGUAL ou
    // superior a 75%. E a falta seguinte que reprova.
    Frequencia f;
    f.aulasPelaCH = 64;
    f.aulasComRegistro = 64;
    f.presencas = 48;          // 16 faltas
    REQUIRE(f.faltas() == 16);
    CHECK_FALSE(f.reprovado());

    f.presencas = 47;          // 17 faltas
    CHECK(f.reprovado());
}

TEST_CASE("le cada dia com a situacao certa", "[frequencia]") {
    const auto r = doFixture();
    const auto& dias = r.frequencia.dias;
    REQUIRE(dias.size() == 13);

    CHECK(dias[0].data.toIso() == "2026-08-03");
    CHECK(dias[0].situacao == SituacaoDia::Falta);
    // "2 Falta(s)": a unidade e a HORA-AULA, nao o dia. Um encontro de dois
    // horarios seguidos lanca duas faltas.
    CHECK(dias[0].faltas == 2);

    CHECK(dias[1].situacao == SituacaoDia::Presente);
    CHECK(dias[1].faltas == 0);
}

TEST_CASE("Nao Registrada nao vira presenca nem falta", "[frequencia]") {
    // O professor deixou o dia em branco. Contar como presenca esconderia uma
    // falta; contar como falta assustaria sem motivo.
    const auto r = doFixture();
    int naoRegistradas = 0;
    for (const auto& d : r.frequencia.dias) {
        if (d.situacao == SituacaoDia::NaoRegistrada) {
            ++naoRegistradas;
            CHECK(d.faltas == 0);
        }
    }
    CHECK(naoRegistradas == 1);
}

TEST_CASE("a conta das linhas fecha com o rodape", "[frequencia]") {
    // O teste que valida o desenho inteiro: se um dia a soma das linhas
    // divergir dos totais do SIGAA, e sinal de que a pagina mudou de formato.
    const auto r = doFixture();

    int comRegistro = 0, presentes = 0, faltas = 0;
    for (const auto& d : r.frequencia.dias) {
        if (d.situacao == SituacaoDia::NaoRegistrada) continue;
        ++comRegistro;
        if (d.situacao == SituacaoDia::Presente) ++presentes;
        faltas += d.faltas;
    }
    CHECK(comRegistro == 12);
    CHECK(presentes == 9);
    CHECK(faltas == r.frequencia.faltas());      // 6
    CHECK(presentes * 2 == r.frequencia.presencas);
    CHECK(comRegistro * 2 == r.frequencia.aulasComRegistro);
}

// ---------------------------------------------------------------------------
// O professor que nunca abriu o diario
// ---------------------------------------------------------------------------

TEST_CASE("mapa sem nenhum lancamento nao vira zero faltas", "[frequencia]") {
    // "Voce nao faltou" e "ninguem registrou nada" levam a decisoes opostas.
    const auto r = doHtml(
        "<html><body><fieldset><legend> Mapa de Frequ&ecirc;ncias </legend>"
        "<div class='botoes-show'></div></fieldset></body></html>");
    CHECK(r.pareceMapaDeFrequencia);
    CHECK_FALSE(r.frequencia.temDados);
    CHECK(r.frequencia.faltas() == 0);
}

TEST_CASE("resposta que nao e o mapa nao passa por turma sem faltas", "[frequencia]") {
    // Sessao expirada devolve a tela de login. Sem esta guarda, o app diria
    // "parabens, zero faltas" para uma pagina que nem olhou a turma.
    const auto r = doHtml("<html><body><form id='form-login'></form></body></html>");
    CHECK_FALSE(r.pareceMapaDeFrequencia);
    CHECK_FALSE(r.frequencia.temDados);
}
