// Regressao: o SIGAA serve windows-1252, nao UTF-8.
//
// Este bug passou despercebido por muito tempo porque o browser decodifica
// cp1252 sozinho — no DevTools os acentos apareciam certos. So aparece quando
// se le os BYTES: 0xC7 0xD5 ("CO" com cedilha e til) e UTF-8 invalido, o
// lexbor troca por U+FFFD e o acento se perde de forma irrecuperavel.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/parse/Html.h"

using namespace sigaa::html;

// Os literais sao quebrados em pedacos adjacentes de proposito: em C++ o
// escape \xNN e guloso e consome todo digito hex seguinte — "\xD5ES" vira o
// escape invalido \xD5E (erro C7744). Concatenar literais encerra o escape.

TEST_CASE("isValidUtf8 rejeita bytes cp1252", "[html][encoding]") {
    // "EQUA<C7><D5>ES" — como o SIGAA manda na rede
    const std::string cp1252 = "EQUA\xC7\xD5" "ES";
    CHECK_FALSE(isValidUtf8(cp1252));

    // o mesmo texto em UTF-8
    const std::string utf8 = "EQUA\xC3\x87\xC3\x95" "ES";
    CHECK(isValidUtf8(utf8));
}

TEST_CASE("toUtf8 converte cp1252 preservando o acento", "[html][encoding]") {
    const std::string cp1252 = "EQUA\xC7\xD5" "ES";
    CHECK(toUtf8(cp1252) == "EQUA\xC3\x87\xC3\x95" "ES");
}

TEST_CASE("toUtf8 nao mexe em conteudo que ja e UTF-8", "[html][encoding]") {
    // Se o SIGAA migrar para UTF-8 num upgrade, nada pode quebrar.
    const std::string utf8 = "Pr\xC3\xA9""dio 2 - Sala 2309";
    CHECK(toUtf8(utf8) == utf8);
}

TEST_CASE("toUtf8 trata a faixa 0x80-0x9F propria do cp1252", "[html][encoding]") {
    // 0x93/0x94 sao aspas curvas no cp1252 (nao existem em latin-1).
    const std::string cp1252 = "\x93oi\x94";
    CHECK(toUtf8(cp1252) == "\xE2\x80\x9C" "oi" "\xE2\x80\x9D");
}

TEST_CASE("parse de documento cp1252 devolve texto acentuado correto",
          "[html][encoding]") {
    // Fim a fim: bytes crus como chegam do servidor -> texto do DOM.
    const std::string pagina =
        "<html><head><meta charset=windows-1252></head><body>"
        "<span id=\"t\">EQUA\xC7\xD5" "ES DIFERENCIAIS ORDIN\xC1" "RIAS</span>"
        "</body></html>";

    Document doc;
    REQUIRE(doc.parse(pagina));

    const auto n = doc.selectFirst("#t");
    REQUIRE(n);
    CHECK(n.text() ==
          "EQUA\xC3\x87\xC3\x95" "ES DIFERENCIAIS ORDIN\xC3\x81" "RIAS");
    // e, principalmente, nenhum caractere de substituicao
    CHECK(n.text().find("\xEF\xBF\xBD") == std::string::npos);
}
