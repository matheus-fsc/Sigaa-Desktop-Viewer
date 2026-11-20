// Testes do fluxo de login de SigaaSession, ponta a ponta, contra um SIGAA
// falso em 127.0.0.1 (cpp-httplib). Nenhuma requisicao sai da maquina.
//
// Existe por causa do aviso pos-login (telaAvisoLogon.jsf): o SIGAA da UNIFEI
// passou a intercalar um comunicado da CPA entre o POST de login e o portal,
// e o login inteiro quebrava com "pagina inesperada". O fixture aviso_logon.html
// e a pagina real, redigida com tools/redact.py.
//
// Lembrete: nomes de TEST_CASE em ASCII puro (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/http/SigaaSession.h"

using sigaa::http::PageKind;
using sigaa::http::SigaaSession;

namespace {

std::string fixture(const char* nome) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + nome, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A variante que o app NAO deve aceitar: so o form de "Declaro que li e estou
// ciente", com senha. Recortada do mesmo fixture (o painel modal dele).
const char* kAvisoComDeclaracao = R"(<html><body>
<form id="formConfirmacaoDeclaracao" name="formConfirmacaoDeclaracao" method="post"
      action="/sigaa/telaAvisoLogon.jsf">
<input type="hidden" name="formConfirmacaoDeclaracao" value="formConfirmacaoDeclaracao" />
<input type="password" name="formConfirmacaoDeclaracao:j_id_jsp_1_21" value="" />
<input id="formConfirmacaoDeclaracao:buttonCancelar" type="submit"
       name="formConfirmacaoDeclaracao:buttonCancelar" value="Cancelar" />
<input type="hidden" name="javax.faces.ViewState" value="j_id1" />
</form></body></html>)";

// SIGAA falso. `avisos` e quantas telas de aviso ficam entre o login e o
// portal; `aviso` e o HTML de cada uma.
struct SigaaFalso {
    httplib::Server srv;
    std::thread th;
    int porta{0};

    std::string login = fixture("login.html");
    std::string portal = fixture("portal_completo.html");
    std::string aviso = fixture("aviso_logon.html");
    std::string senhaCerta = "certa";

    std::atomic<int> avisosRestantes{0};
    std::atomic<int> postsAviso{0};
    std::atomic<bool> avisoDesembocaNoMenu{false};   // Continuar nao cai no portal
    std::vector<std::string> corposAviso;   // so a thread do servidor escreve
    std::mutex mu;

    explicit SigaaFalso(int avisos) : avisosRestantes(avisos) {
        srv.Get("/sigaa/logar.do", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(login, "text/html");
        });
        srv.Post("/sigaa/logar.do", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.get_param_value("user.senha") != senhaCerta) {
                res.set_content(login, "text/html");
            } else if (avisosRestantes > 0) {
                res.set_content(aviso, "text/html");
            } else {
                res.set_content(portal, "text/html");
            }
        });
        srv.Post("/sigaa/telaAvisoLogon.jsf", [this](const httplib::Request& req,
                                                    httplib::Response& res) {
            ++postsAviso;
            {
                std::lock_guard<std::mutex> lk(mu);
                corposAviso.push_back(req.body);
            }
            --avisosRestantes;
            if (avisosRestantes > 0) res.set_content(aviso, "text/html");
            else if (avisoDesembocaNoMenu) res.set_content("<html><body>menu</body></html>", "text/html");
            else res.set_content(portal, "text/html");
        });
        srv.Get("/sigaa/verPortalDiscente.do", [this](const httplib::Request&,
                                                      httplib::Response& res) {
            res.set_content(portal, "text/html");
        });
        porta = srv.bind_to_any_port("127.0.0.1");
        REQUIRE(porta > 0);
        th = std::thread([this] { srv.listen_after_bind(); });
        srv.wait_until_ready();
    }
    ~SigaaFalso() {
        srv.stop();
        th.join();
    }

    std::string base() const { return "http://127.0.0.1:" + std::to_string(porta); }
};

std::unique_ptr<SigaaSession> sessao(const SigaaFalso& s) {
    auto ss = std::make_unique<SigaaSession>(s.base());
    ss->setIntervaloMinimo(std::chrono::milliseconds(0));
    return ss;
}

} // namespace

// --- partes puras ----------------------------------------------------------

TEST_CASE("classify reconhece o aviso pos-login real", "[login]") {
    CHECK(SigaaSession::classify(fixture("aviso_logon.html")) == PageKind::AvisoLogon);
}

TEST_CASE("portal e login nao viram aviso", "[login]") {
    CHECK(SigaaSession::classify(fixture("portal_completo.html")) == PageKind::Portal);
    CHECK(SigaaSession::classify(fixture("login.html")) == PageKind::Login);
}

TEST_CASE("continuar do aviso real posta o form do Continuar", "[login]") {
    const auto c = SigaaSession::continuarAviso(fixture("aviso_logon.html"));
    REQUIRE(c.has_value());
    CHECK(c->action == "/sigaa/telaAvisoLogon.jsf");
    // O form certo (nao o da declaracao), com o ViewState e o botao clicado.
    CHECK(c->corpo.find("j_id_jsp_402922989_1=j_id_jsp_402922989_1") != std::string::npos);
    CHECK(c->corpo.find("j_id_jsp_402922989_3=Continuar") != std::string::npos);
    CHECK(c->corpo.find("javax.faces.ViewState=j_id1") != std::string::npos);
    // Nada do form da declaracao vai junto — nem senha, nem o Cancelar.
    CHECK(c->corpo.find("formConfirmacaoDeclaracao") == std::string::npos);
}

TEST_CASE("aviso que pede senha nao e aceito sozinho", "[login]") {
    CHECK(SigaaSession::classify(kAvisoComDeclaracao) == PageKind::AvisoLogon);
    CHECK_FALSE(SigaaSession::continuarAviso(kAvisoComDeclaracao).has_value());
}

// --- ponta a ponta ---------------------------------------------------------

TEST_CASE("login direto no portal", "[login][rede]") {
    SigaaFalso s(0);
    auto ss = sessao(s);
    std::string erro;
    CHECK(ss->login("00000000000", "certa", &erro));
    CHECK(erro.empty());
    CHECK(ss->autenticado());
    CHECK(s.postsAviso == 0);
}

TEST_CASE("login passa pelo aviso pos-login e chega ao portal", "[login][rede]") {
    SigaaFalso s(1);
    auto ss = sessao(s);
    std::string erro;
    CHECK(ss->login("00000000000", "certa", &erro));
    INFO(erro);
    CHECK(ss->autenticado());
    REQUIRE(s.postsAviso == 1);
    CHECK(s.corposAviso[0].find("j_id_jsp_402922989_3=Continuar") != std::string::npos);
}

TEST_CASE("login passa por varios avisos em fila", "[login][rede]") {
    SigaaFalso s(3);
    auto ss = sessao(s);
    std::string erro;
    CHECK(ss->login("00000000000", "certa", &erro));
    CHECK(ss->autenticado());
    CHECK(s.postsAviso == 3);
}

TEST_CASE("aviso que nao sai do lugar nao vira loop infinito", "[login][rede]") {
    SigaaFalso s(1000);
    auto ss = sessao(s);
    std::string erro;
    CHECK_FALSE(ss->login("00000000000", "certa", &erro));
    CHECK_FALSE(ss->autenticado());
    CHECK(s.postsAviso == 5);
    CHECK(erro.find("avisos") != std::string::npos);
}

TEST_CASE("aviso com declaracao falha sem postar a senha", "[login][rede]") {
    SigaaFalso s(1);
    s.aviso = kAvisoComDeclaracao;
    auto ss = sessao(s);
    std::string erro;
    CHECK_FALSE(ss->login("00000000000", "certa", &erro));
    CHECK_FALSE(ss->autenticado());
    CHECK(s.postsAviso == 0);
    CHECK(erro.find("navegador") != std::string::npos);
}

TEST_CASE("aviso que devolve pagina estranha cai no portal discente", "[login][rede]") {
    // Depois do Continuar, um vinculo de servidor cai no menu principal, nao
    // no portal do aluno. O login pede o portal explicitamente.
    SigaaFalso s(1);
    s.avisoDesembocaNoMenu = true;
    auto ss = sessao(s);
    std::string erro;
    CHECK(ss->login("00000000000", "certa", &erro));
    INFO(erro);
    CHECK(ss->autenticado());
    CHECK(s.postsAviso == 1);
}

TEST_CASE("senha errada continua sendo senha errada", "[login][rede]") {
    SigaaFalso s(1);
    auto ss = sessao(s);
    std::string erro;
    CHECK_FALSE(ss->login("00000000000", "errada", &erro));
    CHECK(erro.find("credenciais") != std::string::npos);
    CHECK(s.postsAviso == 0);
}
