// A pagina local que faz o navegador entrar no SIGAA (core/http/EntradaNoNavegador.h).
//
// O QUE ESTE ARQUIVO PROTEGE: a senha chega ao SIGAA EXATAMENTE como o aluno a
// digitaria. Um caractere mal escapado muda a senha enviada, o SIGAA recusa, e
// o aluno gasta uma das poucas tentativas antes do bloqueio da conta sem nunca
// ter errado nada.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/http/EntradaNoNavegador.h"

using namespace sigaa::http;

namespace {

bool contem(const std::string& s, const std::string& trecho) {
    return s.find(trecho) != std::string::npos;
}

} // namespace

TEST_CASE("senha com caracteres de HTML nao fecha o atributo", "[entrada]") {
    CHECK(escaparAtributo(R"(a"b)") == "a&quot;b");
    CHECK(escaparAtributo("a<b>c") == "a&lt;b&gt;c");
    CHECK(escaparAtributo("a&b") == "a&amp;b");
    CHECK(escaparAtributo("it's") == "it&#39;s");
    CHECK(escaparAtributo("") == "");

    // O caso que importaria na pratica: uma senha que, sem escape, encerraria o
    // input e injetaria outro campo com o mesmo nome.
    const auto p = paginaDeEntrada("https://sigaa.x.br", "12345678901",
                                   R"(x"><input name="user.senha" value="y)", "n");
    CHECK(contem(p, R"(value="x&quot;&gt;&lt;input name=&quot;user.senha&quot; value=&quot;y")"));
    // Um so campo de senha na pagina, e nao dois.
    size_t n = 0;
    for (size_t i = p.find("name=\"user.senha\""); i != std::string::npos;
         i = p.find("name=\"user.senha\"", i + 1)) ++n;
    CHECK(n == 1);
}

TEST_CASE("pagina posta os oito campos do login no SIGAA", "[entrada]") {
    const auto p = paginaDeEntrada("https://sigaa.unifei.edu.br", "12345678901",
                                   "segredo", "abc");

    CHECK(contem(p, R"(action="https://sigaa.unifei.edu.br/sigaa/logar.do?dispatch=logOn")"));
    CHECK(contem(p, R"(method="post")"));
    for (const char* campo : {"width", "height", "urlRedirect", "subsistemaRedirect",
                              "acao", "acessibilidade", "user.login", "user.senha"}) {
        INFO(campo);
        CHECK(contem(p, std::string("name=\"") + campo + "\""));
    }
    CHECK(contem(p, R"(name="user.login" value="12345678901")"));
    CHECK(contem(p, R"(name="user.senha" value="segredo")"));

    // Todos ocultos: o gerenciador de senhas do navegador nao oferece salvar
    // a senha para 127.0.0.1.
    CHECK_FALSE(contem(p, R"(type="password")"));
    CHECK_FALSE(contem(p, R"(type="text")"));

    // O mesmo charset da tela de login do SIGAA (RECON §1.10).
    CHECK(contem(p, R"(accept-charset="windows-1252")"));
    CHECK(contem(p, R"(<script nonce="abc">)"));
}

TEST_CASE("barra no fim da URL da instituicao nao duplica", "[entrada]") {
    const auto p = paginaDeEntrada("https://sigaa.x.br/", "1", "2", "n");
    CHECK(contem(p, R"(action="https://sigaa.x.br/sigaa/logar.do?dispatch=logOn")"));
    CHECK_FALSE(contem(p, "br//sigaa"));
}

TEST_CASE("cabecalhos nao deixam a pagina da senha ir para cache",
          "[entrada]") {
    const auto h = cabecalhosDaEntrada("abc", 1234);
    CHECK(contem(h, "Cache-Control: no-store\r\n"));
    CHECK(contem(h, "Referrer-Policy: no-referrer\r\n"));
    CHECK(contem(h, "Content-Length: 1234\r\n"));
    CHECK(contem(h, "script-src 'nonce-abc'"));
    // O icone vazio da pagina e servido como data:, e nada alem disso.
    CHECK(contem(h, "img-src data:;"));
    // SEM form-action: o Chrome a aplica tambem ao redirecionamento depois do
    // login, e um 302 do SIGAA para fora da lista deixava a aba parada em
    // 127.0.0.1 sem erro nenhum. Foi o primeiro teste real do botao.
    CHECK_FALSE(contem(h, "form-action"));
    CHECK(contem(h, "default-src 'none'"));
    CHECK(h.ends_with("\r\n\r\n"));
}

TEST_CASE("pagina explica quando o SIGAA demora ou o navegador bloqueia", "[entrada]") {
    // Parada em 127.0.0.1 parece defeito do app mesmo quando quem nao responde
    // e o SIGAA. A pagina tem de dizer qual dos dois, e oferecer a saida.
    const auto p = paginaDeEntrada("https://sigaa.x.br/", "1", "2", "n");
    CHECK(contem(p, R"(id="demora" class="aviso demora" hidden)"));
    CHECK(contem(p, R"(id="bloqueio" class="aviso bloqueio" hidden)"));

    // O que o aluno ve no segundo em que tudo da certo: o recado e a barra.
    CHECK(contem(p, "Aguarde enquanto seu navegador"));
    CHECK(contem(p, R"(role="progressbar")"));
    // Tudo dentro da pagina: a politica recusa qualquer recurso buscado fora.
    CHECK_FALSE(contem(p, "<img"));
    CHECK_FALSE(contem(p, "fonts.googleapis"));
    CHECK(contem(p, "securitypolicyviolation"));
    CHECK(contem(p, R"(href="https://sigaa.x.br/sigaa/verTelaLogin.do")"));
}
