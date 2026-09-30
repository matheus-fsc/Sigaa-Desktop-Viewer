// Comparacao de versoes do atualizador.
//
// POR QUE ISTO TEM TESTE PROPRIO: e a funcao que decide se o app baixa e
// EXECUTA um binario novo. Errar para mais oferece uma atualizacao que nao
// existe; errar para menos deixa o usuario preso numa versao com bug, achando
// que esta em dia. Os dois erros sao silenciosos.
//
// O caso que motiva metade dos testes e o sufixo de pre-lancamento: este
// projeto lanca "0.1.2-alpha", e no dia em que sair a "0.1.2" final o app
// precisa saber que ela vem DEPOIS — e nao oferecer uma atualizacao para tras.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/atualizacao/Atualizador.h"

using sigaa::atualizacao::levaToken;
using sigaa::atualizacao::maisNova;

TEST_CASE("versao maior em qualquer campo e mais nova", "[atualizador]") {
    CHECK(maisNova("0.2.0", "0.1.9"));
    CHECK(maisNova("1.0.0", "0.9.9"));
    CHECK(maisNova("0.1.3", "0.1.2"));
    CHECK_FALSE(maisNova("0.1.2", "0.1.3"));
    CHECK_FALSE(maisNova("0.1.2", "0.1.2"));
}

TEST_CASE("compara numero, nao texto", "[atualizador]") {
    // "0.1.10" < "0.1.9" em ordem alfabetica, e e exatamente o erro que faria
    // o app parar de oferecer atualizacoes a partir da decima correcao.
    CHECK(maisNova("0.1.10", "0.1.9"));
    CHECK_FALSE(maisNova("0.1.9", "0.1.10"));
}

TEST_CASE("o v da tag nao atrapalha", "[atualizador]") {
    // A API do GitHub devolve "v0.1.3-alpha" em `tag_name`; a versao do CMake
    // vem sem o v. Comparar os dois formatos crus daria sempre falso.
    CHECK(maisNova("v0.1.3", "0.1.2"));
    CHECK(maisNova("v0.1.3-alpha", "v0.1.2-alpha"));
}

TEST_CASE("lancamento final vem depois do pre-lancamento", "[atualizador]") {
    // A regra do semver, e o caso real deste projeto: a 0.1.2 final sai depois
    // da 0.1.2-alpha, e o app tem de enxergar isso como atualizacao.
    CHECK(maisNova("0.1.2", "0.1.2-alpha"));
    CHECK_FALSE(maisNova("0.1.2-alpha", "0.1.2"));
}

TEST_CASE("quem esta num alpha recebe a 1.0 como atualizacao", "[atualizador]") {
    // O caminho de upgrade que existe de verdade hoje: todo mundo que instalou
    // este app esta numa 0.x-alpha. Se `maisNova` errasse aqui, a 1.0 sairia e
    // ninguem seria avisado — e o silencio seria indistinguivel de "nao ha
    // versao nova", que e o que o app diz quando esta em dia.
    CHECK(maisNova("1.0.0", "0.2.0-alpha"));
    CHECK(maisNova("v1.0.0", "v0.2.0-alpha"));
    CHECK(maisNova("1.0.0", "0.2.0"));

    // E a volta: ja na 1.0.0, um alpha antigo ainda listado na release nao
    // pode ser oferecido como novidade.
    CHECK_FALSE(maisNova("0.2.0-alpha", "1.0.0"));
    CHECK_FALSE(maisNova("1.0.0", "1.0.0"));
}

TEST_CASE("entre pre-lancamentos, alpha vem antes de beta", "[atualizador]") {
    CHECK(maisNova("0.2.0-beta", "0.2.0-alpha"));
    CHECK(maisNova("0.2.0-rc1", "0.2.0-beta"));
    CHECK_FALSE(maisNova("0.2.0-alpha", "0.2.0-beta"));
}

TEST_CASE("pre-lancamento de versao maior ainda e mais novo", "[atualizador]") {
    // O numero manda sobre o sufixo: a 0.3.0-alpha vem depois da 0.2.0 final.
    CHECK(maisNova("0.3.0-alpha", "0.2.0"));
    CHECK_FALSE(maisNova("0.2.0", "0.3.0-alpha"));
}

TEST_CASE("versao incompleta nao quebra a comparacao", "[atualizador]") {
    // Uma tag "v1" ou "v1.2" nao deveria existir, mas existir uma nao pode
    // travar o app nem fazer ele anunciar bobagem.
    CHECK(maisNova("1", "0.9.9"));
    CHECK(maisNova("1.2", "1.1.9"));
    CHECK_FALSE(maisNova("", "0.1.0"));
}

TEST_CASE("a versao atual vem do CMake", "[atualizador]") {
    // Se `SIGAA_VERSAO` nao chegar ao compilador, isto cai em "0.0.0" e o app
    // anunciaria atualizacao para sempre.
    CHECK(sigaa::atualizacao::versaoAtual() != "0.0.0");
}

TEST_CASE("GITHUB_TOKEN so vai para a API do GitHub por HTTPS", "[atualizador]") {
    // O caso legitimo: a lista de releases.
    CHECK(levaToken("https://api.github.com/repos/matheus-fsc/Sigaa-Desktop-Viewer/releases"));

    // O que um teste por substring deixava passar. As URLs de download vem da
    // resposta da API, entao nao sao todas nossas.
    CHECK_FALSE(levaToken("https://api.github.com.exemplo.net/x"));
    CHECK_FALSE(levaToken("https://exemplo.net/api.github.com/x"));
    CHECK_FALSE(levaToken("https://exemplo.net/?h=https://api.github.com/"));
    CHECK_FALSE(levaToken("https://evil@exemplo.net/api.github.com"));

    // Texto puro nao leva token.
    CHECK_FALSE(levaToken("http://api.github.com/repos/x"));

    // O download do pacote e em outro host do GitHub, e nao precisa de token.
    CHECK_FALSE(levaToken("https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/releases/download/v1.0.1/x.AppImage"));
    CHECK_FALSE(levaToken("https://objects.githubusercontent.com/x"));
}
