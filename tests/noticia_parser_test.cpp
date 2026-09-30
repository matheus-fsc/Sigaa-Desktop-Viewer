// Noticias da Turma Virtual: a ultima (pagina da turma), a lista e o detalhe.
//
// Fixtures: HTML cru da rede (tests/fixtures/noticias_*.html, extraidos de um
// HAR do site real e redigidos por tools/redact.py).
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "core/parse/Html.h"
#include "core/parse/NoticiaParser.h"

using namespace sigaa;

namespace {

std::string lerFixture(const std::string& nome) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + nome, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

html::Document doc(const std::string& nome) {
    html::Document d;
    REQUIRE(d.parse(lerFixture(nome)));
    return d;
}

bool contem(const std::string& s, const std::string& parte) {
    return s.find(parte) != std::string::npos;
}

} // namespace

TEST_CASE("noticias: a lista traz titulo, dia e o id de cada uma", "[noticias]") {
    const auto l = parse::parseListaNoticias(doc("noticias_lista.html"));
    CHECK(l.pareceAbaNoticias);
    CHECK_FALSE(l.vazioConfirmado);
    REQUIRE(l.itens.size() == 3);

    CHECK(l.itens[0].id == "130428096");
    CHECK(l.itens[0].titulo == "ATIVIDADES DE LABORATÓRIO");
    CHECK(l.itens[0].data.toIso() == "2026-09-30");

    CHECK(l.itens[1].id == "130227123");
    CHECK(l.itens[1].titulo == "ATIVIDADES PRÁTICAS ATÉ 31/08");
    CHECK(l.itens[2].id == "130221147");
    CHECK(l.itens[2].data.toIso() == "2026-08-19");
}

TEST_CASE("noticias: o detalhe traz a hora e o texto limpo", "[noticias]") {
    const auto d = parse::parseDetalheNoticia(doc("noticia_detalhe.html"));
    REQUIRE(d.pareceDetalhe);
    CHECK(d.titulo == "ATIVIDADES DE LABORATÓRIO");
    CHECK(d.data.toIso() == "2026-09-30T14:51");

    // O texto sobrevive, com negrito e paragrafos...
    CHECK(contem(d.conteudoHtml, "<p>Olá a todos!</p>"));
    CHECK(contem(d.conteudoHtml, "<strong>modulo4_laboratorios.pdf</strong>"));
    // ...e a fonte e o tamanho que o editor cola em cada span, nao.
    CHECK_FALSE(contem(d.conteudoHtml, "verdana"));
    CHECK_FALSE(contem(d.conteudoHtml, "<span"));
    CHECK_FALSE(contem(d.conteudoHtml, "<div"));
}

TEST_CASE("noticias: a ultima vem da pagina da turma, sem custo de rede", "[noticias]") {
    const auto n = parse::parseUltimaNoticia(doc("noticias_turma.html"), "89178", "COMPILADORES");
    REQUIRE(n.has_value());
    CHECK(n->idTurma == "89178");
    CHECK(n->idNoticia.empty());   // a pagina da turma nao da o id
    CHECK(n->titulo == "ATIVIDADES DE LABORATÓRIO");
    CHECK(n->data.toIso() == "2026-09-30T14:51");
    CHECK(n->autor == "DOCENTE TESTE");
    CHECK(contem(n->conteudoHtml, "<strong>cinco</strong>"));
}

TEST_CASE("noticias: a ultima e o item da lista caem na mesma chave", "[noticias]") {
    // E o que permite nao pagar o detalhe da noticia que a pagina da turma ja
    // trouxe: a ultima tem hora, a lista so o dia, e a chave usa so o dia.
    const auto n = parse::parseUltimaNoticia(doc("noticias_turma.html"), "89178", "COMPILADORES");
    const auto l = parse::parseListaNoticias(doc("noticias_lista.html"));
    REQUIRE(n.has_value());
    REQUIRE_FALSE(l.itens.empty());
    CHECK(parse::chaveNoticia("89178", n->titulo, n->data) ==
          parse::chaveNoticia("89178", l.itens[0].titulo, l.itens[0].data));
}

TEST_CASE("noticias: pagina que nao e a aba nao vira lista vazia", "[noticias]") {
    // A pagina da turma nao e a aba Noticias: "sem noticias" aqui seria
    // inventar uma resposta a partir de uma falha.
    const auto l = parse::parseListaNoticias(doc("noticias_turma.html"));
    CHECK_FALSE(l.pareceAbaNoticias);
    CHECK(l.itens.empty());
    CHECK_FALSE(parse::parseDetalheNoticia(doc("noticias_lista.html")).pareceDetalhe);
}

TEST_CASE("noticias: a limpeza guarda o que se le e descarta o resto", "[noticias]") {
    using parse::limparHtmlNoticia;
    CHECK(limparHtmlNoticia("<p><span style=\"font-size:small\">oi</span></p>") == "<p>oi</p>");
    CHECK(limparHtmlNoticia("<b>a</b><i>b</i>") == "<strong>a</strong><em>b</em>");
    CHECK(limparHtmlNoticia("<p>&nbsp;</p><p>x</p>") == "<p>x</p>");
    CHECK(limparHtmlNoticia("<script>alert(1)</script><p>y</p>") == "<p>y</p>");
    // Link http(s) fica, com o href e nada mais...
    CHECK(limparHtmlNoticia("<a href=\"https://ex.com/a\" onclick=\"x()\">l</a>") ==
          "<a href=\"https://ex.com/a\">l</a>");
    // ...javascript: vira texto solto, sem link.
    CHECK(limparHtmlNoticia("<a href=\"javascript:alert(1)\">l</a>") == "l");
}
