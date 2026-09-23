// SHA-256 conferido contra os vetores de referencia.
//
// POR QUE ESTES TESTES SAO INEGOCIAVEIS: este hash e o que decide se o app
// executa ou descarta um binario baixado da internet. Uma implementacao quase
// certa aceita o arquivo errado — e o modo de falha nao e um digest esquisito
// na tela, e o usuario rodando um executavel que nao e o nosso.
//
// Os tres primeiros sao vetores publicados (FIPS 180-4 / NIST CAVP). Os
// seguintes cobrem o PREENCHIMENTO, que e onde implementacoes caseiras erram:
// a fronteira de 55, 56 e 64 bytes decide se o bloco final cabe ou se e
// preciso mais um.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "core/atualizacao/Sha256.h"

using namespace sigaa;

TEST_CASE("vetor NIST: string vazia", "[sha256]") {
    CHECK(hash::sha256Hex(std::string{}) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("vetor NIST: abc", "[sha256]") {
    CHECK(hash::sha256Hex(std::string{"abc"}) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("vetor NIST: duas blocadas", "[sha256]") {
    // 56 bytes — obriga um segundo bloco so para o preenchimento.
    CHECK(hash::sha256Hex(std::string{
              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"}) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("as fronteiras do preenchimento", "[sha256]") {
    // 55 bytes: o ultimo bloco ainda cabe com o 0x80 e o comprimento.
    // 56: nao cabe mais, precisa de um bloco extra. 64: bloco cheio.
    // E aqui que uma implementacao caseira erra, e o erro nao aparece nos
    // vetores curtos.
    CHECK(hash::sha256Hex(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hash::sha256Hex(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(hash::sha256Hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("consumir em pedacos da o mesmo digest", "[sha256]") {
    // O download chega em blocos de tamanho que a rede decide. Se o resultado
    // dependesse desse recorte, o hash falharia de forma intermitente — o pior
    // tipo de bug para depurar.
    const std::string entrada(1000, 'x');

    hash::Sha256 inteiro;
    inteiro.consumir(entrada.data(), entrada.size());

    hash::Sha256 picado;
    std::size_t i = 0;
    for (const std::size_t passo : {1u, 7u, 64u, 3u, 100u, 63u, 65u}) {
        const std::size_t n = std::min(passo, entrada.size() - i);
        picado.consumir(entrada.data() + i, n);
        i += n;
        if (i >= entrada.size()) break;
    }
    picado.consumir(entrada.data() + i, entrada.size() - i);

    CHECK(inteiro.hex() == picado.hex());
}

TEST_CASE("um bit diferente muda o digest inteiro", "[sha256]") {
    // A propriedade da qual depende a verificacao: um binario adulterado nao
    // pode passar por parecido.
    const std::string a(100, 'a');
    std::string b = a;
    b[50] = 'b';
    CHECK(hash::sha256Hex(a) != hash::sha256Hex(b));
}
