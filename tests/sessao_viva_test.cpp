// Reuso de sessao do SIGAA.
//
// O QUE ESTE ARQUIVO PROTEGE. Login e a operacao mais cara do app (6 a 8 s
// observados) e a unica que o SIGAA as vezes recusa quando ja ha outra sessao
// aberta na conta — o sintoma foi o app parecer travado, com tres timeouts de
// 45 s no segundo login. A SessaoViva existe para que o login aconteca UMA vez
// e seja reaproveitado enquanto valer.
//
// O que da para testar sem rede e a MAQUINA DE ESTADOS: quando a sessao conta
// como viva, quando vence, e o que `invalidar` faz. O login em si precisa do
// SIGAA e nao entra aqui.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/http/SessaoViva.h"

using namespace sigaa;

TEST_CASE("cofre novo nao tem sessao viva", "[sessao]") {
    // Importa porque `viva()` decide se o ciclo diz "reaproveitando" ou
    // "autenticando". Um verdadeiro aqui faria o Servico usar um ponteiro nulo.
    http::SessaoViva c;
    CHECK_FALSE(c.viva());
    CHECK(c.restante().count() == 0);
}

TEST_CASE("invalidar zera o estado sem ir a rede", "[sessao]") {
    // `invalidar` e chamado quando uma resposta volta como tela de login: o
    // servidor JA descartou a sessao, e mandar logOff seria gastar uma
    // requisicao para receber a mesma tela.
    http::SessaoViva c;
    c.invalidar();
    CHECK_FALSE(c.viva());
    CHECK(c.restante().count() == 0);
}

TEST_CASE("encerrar num cofre vazio nao faz nada", "[sessao]") {
    // Roda no destrutor da janela principal. Se tropecasse num cofre vazio,
    // fechar o app sem nunca ter logado quebraria.
    http::SessaoViva c;
    c.encerrar();
    CHECK_FALSE(c.viva());
}

TEST_CASE("a margem de validade fica abaixo do timeout do SIGAA", "[sessao]") {
    // O SIGAA expira em 30 minutos. Reconectar por conta propria com folga e
    // mais barato que descobrir a expiracao no meio de uma coleta, quando a
    // requisicao ja foi gasta e o usuario ja esperou.
    CHECK(http::SessaoViva::kValidade.count() < 30);
    CHECK(http::SessaoViva::kValidade.count() >= 20);
}

// NAO ha teste de `obter` aqui, e e de proposito: ele faz login, login vai a
// rede, e a suite deste projeto e offline (ver o cabecalho de
// baixador_test.cpp). Um teste que so passa com internet e com o SIGAA no ar
// vira ruido vermelho no dia em que qualquer um dos dois faltar — e ensina a
// ignorar falha de teste, que e o pior habito que uma suite pode criar.
