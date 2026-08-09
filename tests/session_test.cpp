// Testes das partes puras de SigaaSession — classificacao de pagina e leitura
// do contador de sessao. Nao tocam a rede.
//
// Lembrete: nomes de TEST_CASE em ASCII puro (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/http/SigaaSession.h"

using sigaa::http::PageKind;
using sigaa::http::SigaaSession;

TEST_CASE("classify reconhece o portal", "[session]") {
    CHECK(SigaaSession::classify(
              R"(<form id="formAtividades"></form>)") == PageKind::Portal);
    CHECK(SigaaSession::classify(
              R"(<form id="formAtualizacoesTurmas"></form>)") == PageKind::Portal);
}

TEST_CASE("classify reconhece a turma virtual", "[session]") {
    CHECK(SigaaSession::classify(
              R"(<form id="formAva" action="/sigaa/ava/index.jsf"></form>)") ==
          PageKind::TurmaVirtual);
}

TEST_CASE("classify reconhece a tela de login", "[session]") {
    CHECK(SigaaSession::classify(
              R"(<input name="user.login"><input name="user.senha" type="password">)") ==
          PageKind::Login);
}

TEST_CASE("sessao expirada tem prioridade sobre o resto", "[session]") {
    // Uma pagina de expiracao pode vir com o menu do portal junto; se a ordem
    // dos testes estiver errada, ela seria classificada como Portal e o
    // cliente seguiria navegando numa sessao morta.
    const char* html =
        R"(<div>Sua sessao expirada</div><form id="formAtividades"></form>)";
    CHECK(SigaaSession::classify(html) == PageKind::SessaoExpirada);
}

TEST_CASE("classify devolve Desconhecida quando nao ha marca", "[session]") {
    CHECK(SigaaSession::classify("<html><body>oi</body></html>") ==
          PageKind::Desconhecida);
}

TEST_CASE("le o contador Tempo de Sessao", "[session]") {
    // UTF-8 real, com o 'a' com til em dois bytes, como o servidor manda.
    const char* header = "Tempo de Sess\xC3\xA3o: 00:25";
    auto m = SigaaSession::minutosSessaoRestantes(header);
    REQUIRE(m.has_value());
    CHECK(*m == 25);
}

TEST_CASE("contador em horas vira minutos", "[session]") {
    auto m = SigaaSession::minutosSessaoRestantes("Tempo de Sessao: 01:05");
    REQUIRE(m.has_value());
    CHECK(*m == 65);
}

TEST_CASE("sem contador devolve nullopt", "[session]") {
    CHECK_FALSE(SigaaSession::minutosSessaoRestantes("<html></html>").has_value());
}
