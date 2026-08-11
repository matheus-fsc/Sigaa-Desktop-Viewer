// Escolha de instituicao. O que importa aqui nao e "guardar uma string": e que
// o HOST derivado dela vira a chave do cofre de credenciais, e uma chave errada
// faz o app tentar a senha de uma universidade na outra — algumas tentativas
// erradas bloqueiam a conta do aluno no SIGAA.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/config/Instituicao.h"

using namespace sigaa;

TEST_CASE("instituicao: o catalogo comeca na UNIFEI, que e a verificada",
          "[instituicao]") {
    const auto& c = config::catalogo();
    REQUIRE_FALSE(c.empty());
    CHECK(c.front().id == "unifei");
    CHECK(c.front().verificada);
    CHECK(c.front().host() == "sigaa.unifei.edu.br");
}

TEST_CASE("instituicao: host sai da URL, sem esquema e sem caminho",
          "[instituicao]") {
    config::Instituicao i;
    i.baseUrl = "https://sigaa.exemplo.edu.br";
    CHECK(i.host() == "sigaa.exemplo.edu.br");

    i.baseUrl = "http://sigaa.exemplo.edu.br/sigaa/";
    CHECK(i.host() == "sigaa.exemplo.edu.br");

    i.baseUrl = "sigaa.exemplo.edu.br";
    CHECK(i.host() == "sigaa.exemplo.edu.br");
}

TEST_CASE("instituicao: URL digitada vira https", "[instituicao]") {
    // O que trafega no login e CPF e senha. Aceitar o http:// que a pessoa
    // digitou por habito seria mandar isso em claro.
    CHECK(config::personalizada("sigaa.exemplo.edu.br").baseUrl ==
          "https://sigaa.exemplo.edu.br");
    CHECK(config::personalizada("http://sigaa.exemplo.edu.br").baseUrl ==
          "https://sigaa.exemplo.edu.br");
    CHECK(config::personalizada("https://sigaa.exemplo.edu.br/").baseUrl ==
          "https://sigaa.exemplo.edu.br");
    CHECK(config::personalizada("  SIGAA.Exemplo.Edu.BR  ").baseUrl ==
          "https://sigaa.exemplo.edu.br");
}

TEST_CASE("instituicao: nome de universidade nao e endereco", "[instituicao]") {
    // Sem ponto nao e host. Tentar "https://unifei" daria erro de rede, e o
    // usuario culparia a internet em vez do campo que ele preencheu errado.
    CHECK_FALSE(config::personalizada("unifei").valida());
    CHECK_FALSE(config::personalizada("").valida());
    CHECK_FALSE(config::personalizada("   ").valida());
}

TEST_CASE("instituicao: URL manual que aponta para o catalogo e a do catalogo",
          "[instituicao]") {
    // Senao a mesma universidade teria duas identidades — e duas chaves de
    // cofre, o que faria o app pedir a senha de novo sem explicar por que.
    const auto i = config::personalizada("https://sigaa.unifei.edu.br");
    CHECK(i.id == "unifei");
    CHECK(i.verificada);
}

TEST_CASE("instituicao: selecionar troca o alvo, invalido nao troca",
          "[instituicao]") {
    const std::string antes = config::selecionada().baseUrl;

    config::selecionar(config::personalizada("sigaa.exemplo.edu.br"));
    CHECK(config::selecionada().host() == "sigaa.exemplo.edu.br");

    config::selecionar(config::Instituicao{});   // invalida: ignorada
    CHECK(config::selecionada().host() == "sigaa.exemplo.edu.br");

    config::selecionar(config::catalogo().front());
    CHECK(config::selecionada().baseUrl == antes);
}

TEST_CASE("instituicao: porId acha o que existe e recusa o que nao existe",
          "[instituicao]") {
    REQUIRE(config::porId("unifei").has_value());
    CHECK_FALSE(config::porId("nao-existe").has_value());
    CHECK_FALSE(config::porId("").has_value());
}
