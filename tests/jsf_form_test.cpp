// Testa o motor JSF contra HTML sintético, modelado byte-a-byte no que o
// SIGAA realmente emite (ver docs/RECON.md §1.3).
//
// Usa-se HTML sintético de propósito: fixtures reais do portal contêm nome
// completo, CPF e idusuario do aluno (docs/RECON.md §4), e não podem ser
// commitados sem redação.

#include <catch2/catch_test_macros.hpp>

#include "core/jsf/JsfForm.h"
#include "core/parse/Html.h"

using namespace sigaa;

namespace {

// Reprodução fiel de uma linha da tabela "Turmas do Semestre".
constexpr const char* kPortalTrecho = R"HTML(
<html><body>
<form id="form_acessarTurmaVirtual" name="form_acessarTurmaVirtual" method="post"
      action="/sigaa/portais/discente/discente.jsf"
      enctype="application/x-www-form-urlencoded">
<input type="hidden" name="form_acessarTurmaVirtual" value="form_acessarTurmaVirtual" />
<a href="#" onclick="var a=function(){return prevenirDuploClique();};var b=function(){if(typeof jsfcljs == 'function'){jsfcljs(document.getElementById('form_acessarTurmaVirtual'),{'form_acessarTurmaVirtual:j_id_jsp_512348736_365':'form_acessarTurmaVirtual:j_id_jsp_512348736_365','frontEndIdTurma':'6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C'},'');}return false};return (a()==false) ? false : b();">ADMINISTRA&Ccedil;&Atilde;O</a>
<input type="hidden" name="javax.faces.ViewState" id="javax.faces.ViewState" value="j_id20" />
</form>
</body></html>
)HTML";

html::Document parse(const char* s) {
    html::Document d;
    REQUIRE(d.parse(s));
    return d;
}

} // namespace

TEST_CASE("parseForm extrai id, action, viewState e campos", "[jsf]") {
    auto doc = parse(kPortalTrecho);
    auto f = jsf::parseForm(doc, "form_acessarTurmaVirtual");

    REQUIRE(f.has_value());
    CHECK(f->id == "form_acessarTurmaVirtual");
    CHECK(f->action == "/sigaa/portais/discente/discente.jsf");
    CHECK(f->viewState == "j_id20");
    // o form do portal tem exatamente 2 inputs — é o que torna o POST enxuto
    CHECK(f->fields.size() == 2);
}

TEST_CASE("parseJsfcljs extrai formId e params do onclick aninhado", "[jsf]") {
    auto doc = parse(kPortalTrecho);
    auto cmds = jsf::findCommands(doc);

    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].formId == "form_acessarTurmaVirtual");
    REQUIRE(cmds[0].params.size() == 2);
    CHECK(cmds[0].params[0].first == "form_acessarTurmaVirtual:j_id_jsp_512348736_365");
    CHECK(cmds[0].params[1].first == "frontEndIdTurma");
    CHECK(cmds[0].params[1].second == "6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C");
}

TEST_CASE("buildPostBody reproduz o corpo observado no browser", "[jsf]") {
    // Corpo capturado de verdade (docs/RECON.md §1.3): 4 campos, nesta ordem.
    auto doc = parse(kPortalTrecho);
    auto cmd = jsf::findCommandByLabel(doc, "ADMINISTRA");
    REQUIRE(cmd.has_value());
    auto f = jsf::parseForm(doc, cmd->formId);
    REQUIRE(f.has_value());

    const std::string body = f->buildPostBody(cmd->params);

    CHECK(body.find("form_acessarTurmaVirtual=form_acessarTurmaVirtual") != std::string::npos);
    CHECK(body.find("javax.faces.ViewState=j_id20") != std::string::npos);
    CHECK(body.find("frontEndIdTurma=6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C") !=
          std::string::npos);
    // command link presente (os ':' são escapados)
    CHECK(body.find("j_id_jsp_512348736_365") != std::string::npos);
}

// NOTA: nomes de TEST_CASE devem ser ASCII puro. O CTest repassa o nome como
// filtro para o binario do Catch2, e no Windows o codepage do console corrompe
// acentos no meio do caminho — o teste "some" e falha com "No tests ran".
TEST_CASE("urlEncode escapa ':' e espaco como o browser", "[jsf]") {
    CHECK(jsf::urlEncode("a:b") == "a%3Ab");
    CHECK(jsf::urlEncode("um dois") == "um+dois");
    CHECK(jsf::urlEncode("ok-_.~") == "ok-_.~");
}

TEST_CASE("onclick sem jsfcljs eh ignorado", "[jsf]") {
    auto doc = parse(R"HTML(
      <html><body><a href="#" onclick="alert('oi')">nada</a></body></html>)HTML");
    CHECK(jsf::findCommands(doc).empty());
}
