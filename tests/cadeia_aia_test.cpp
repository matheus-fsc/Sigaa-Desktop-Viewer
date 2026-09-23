// AIA chasing — as partes que nao tocam a rede.
//
// O QUE ESTE ARQUIVO PROTEGE: o AIA so serve se o certificado que ele busca
// chegar ao OpenSSL num formato que o OpenSSL aceite. Um base64 com padding
// errado, ou uma URL extraida com uma virgula grudada no fim, produzem
// exatamente o MESMO sintoma do problema original — "unable to get local
// issuer certificate" — e mandam quem for depurar procurar no servidor um
// defeito que esta aqui.
//
// A propriedade de seguranca (intermediario buscado NAO vira ancora de
// confianca) nao da para testar sem rede; foi conferida contra badssl.com, e
// o raciocinio esta no cabecalho de core/http/CadeiaAia.h.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/http/CadeiaAia.h"

using namespace sigaa;

TEST_CASE("PEM de entrada volta intacto", "[aia]") {
    // O AIA pode devolver PEM em vez de DER — a RFC permite as duas, e re-
    // codificar um PEM produziria um arquivo que o OpenSSL recusa.
    const std::string pem =
        "-----BEGIN CERTIFICATE-----\nQUJD\n-----END CERTIFICATE-----\n";
    CHECK(http::derParaPem(pem) == pem);
}

TEST_CASE("DER vira PEM com moldura e quebras de linha", "[aia]") {
    const std::string pem = http::derParaPem("ABC");
    CHECK(pem.rfind("-----BEGIN CERTIFICATE-----\n", 0) == 0);
    CHECK(pem.find("QUJD") != std::string::npos);            // base64("ABC")
    CHECK(pem.find("-----END CERTIFICATE-----") != std::string::npos);
    CHECK(pem.back() == '\n');
}

TEST_CASE("o padding do base64 fecha nos tres restos possiveis", "[aia]") {
    // 3 bytes por bloco: sobra 0, 1 ou 2. Sem o '=' o OpenSSL recusa o PEM
    // inteiro, e o sintoma seria identico ao do erro que o AIA veio corrigir.
    CHECK(http::derParaPem("ABC").find("QUJD") != std::string::npos);    // resto 0
    CHECK(http::derParaPem("AB").find("QUI=") != std::string::npos);     // resto 2
    CHECK(http::derParaPem("A").find("QQ==") != std::string::npos);      // resto 1
}

TEST_CASE("linhas do PEM tem no maximo 64 caracteres", "[aia]") {
    const std::string pem = http::derParaPem(std::string(200, 'x'));
    size_t ini = pem.find('\n') + 1;
    while (true) {
        const size_t fim = pem.find('\n', ini);
        if (fim == std::string::npos) break;
        const std::string linha = pem.substr(ini, fim - ini);
        if (linha.rfind("-----", 0) == 0) break;
        CHECK(linha.size() <= 64);
        ini = fim + 1;
    }
}

TEST_CASE("extrai a URL do emissor e ignora o OCSP", "[aia]") {
    // Pegar qualquer URL do bloco traria o respondedor OCSP, que nao e um
    // certificado — o download daria 200 com lixo e o bundle sairia invalido.
    const std::string certinfo =
        "Authority Information Access:CA Issuers - "
        "URI:http://secure.globalsign.com/cacert/rnpicpedugr46ovtlsca2025.crt\n"
        "OCSP - URI:http://ocsp.globalsign.com/rnpicpedugr46ovtlsca2025\n";

    const auto urls = http::urlsDeEmissor(certinfo);
    REQUIRE(urls.size() == 1);
    CHECK(urls[0] ==
          "http://secure.globalsign.com/cacert/rnpicpedugr46ovtlsca2025.crt");
}

TEST_CASE("a URL nao leva virgula nem espaco grudados", "[aia]") {
    // O certinfo junta campos na mesma linha. Uma virgula no fim da URL vira
    // um 404 no download, e o AIA falha em silencio.
    const auto urls = http::urlsDeEmissor(
        "CA Issuers - URI:http://ca.exemplo/x.crt, OCSP - URI:http://o.exemplo\n");
    REQUIRE(urls.size() == 1);
    CHECK(urls[0] == "http://ca.exemplo/x.crt");
}

TEST_CASE("varias URLs de emissor, sem repetir", "[aia]") {
    // A mesma URL aparece uma vez por certificado da cadeia. Baixar duas vezes
    // custaria uma requisicao a toa num caminho que ja e o caminho lento.
    const auto urls = http::urlsDeEmissor(
        "CA Issuers - URI:http://a.exemplo/1.crt\n"
        "CA Issuers - URI:http://b.exemplo/2.crt\n"
        "CA Issuers - URI:http://a.exemplo/1.crt\n");
    REQUIRE(urls.size() == 2);
    CHECK(urls[0] == "http://a.exemplo/1.crt");
    CHECK(urls[1] == "http://b.exemplo/2.crt");
}

TEST_CASE("esquema que nao e http nao e seguido", "[aia]") {
    // O AIA aceita LDAP. Nao vamos falar LDAP por causa de um servidor mal
    // configurado — e uma URL ldap:// passada ao curl seria uma requisicao
    // para um protocolo que este app nunca quis usar.
    CHECK(http::urlsDeEmissor("CA Issuers - URI:ldap://ca.exemplo/cn=x\n").empty());
    CHECK(http::urlsDeEmissor("CA Issuers - URI:file:///etc/passwd\n").empty());
}

TEST_CASE("certinfo sem AIA devolve vazio", "[aia]") {
    // Servidor com cadeia completa nao tem por que ter AIA lido. Vazio aqui e
    // o que faz o app desistir e reportar o erro original, em vez de insistir.
    CHECK(http::urlsDeEmissor("Subject:CN=exemplo\nIssuer:CN=raiz\n").empty());
    CHECK(http::urlsDeEmissor("").empty());
}
