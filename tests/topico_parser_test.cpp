// Topicos de aula da Turma Virtual: a linha do tempo que o aluno realmente le,
// e os materiais que o professor pendura em cada aula (docs/RECON.md §1.6.2).
//
// Fixture e HTML CRU DA REDE, redigido por tools/redact.py — nunca Ctrl+S do
// browser (§1.7). Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "core/parse/Html.h"
#include "core/parse/TurmaParser.h"

using namespace sigaa;

namespace {

std::string lerFixture(const std::string& nome) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + nome, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

html::Document doc(const std::string& nome) {
    html::Document d;
    REQUIRE(d.parse(html::toUtf8(lerFixture(nome))));
    return d;
}

} // namespace

TEST_CASE("topicos: a pagina da turma traz a linha do tempo das aulas", "[topicos]") {
    const auto d = doc("turma_rede.html");
    const auto c = parse::parseTurmaVirtual(d, "89151", "ADMINISTRACAO", "2026.2");

    CHECK_FALSE(c.semTopicos);
    REQUIRE(c.topicos.size() == 16);

    const auto& t = c.topicos.front();
    CHECK_FALSE(t.titulo.empty());
    CHECK(t.idTurma == "89151");
    // O titulo carrega o intervalo entre parenteses e ele tem que sair de la:
    // sem data, a aula nao tem onde cair numa linha do tempo.
    CHECK(t.inicio.valid());
    CHECK(t.inicio.year == 2026);
}

TEST_CASE("topicos: o conteudo nao vem com o javascript do RichFaces junto",
          "[topicos]") {
    const auto d = doc("turma_rede.html");
    const auto c = parse::parseTurmaVirtual(d, "89151", "ADMINISTRACAO", "2026.2");

    // Cada topico embute um <script> de drag-and-drop. textContent do DOM o
    // incluiria, e o campo iria para a tela com `var elt = $("formAva:...")`
    // grudado na descricao escrita pelo professor.
    for (const auto& t : c.topicos) {
        CHECK(t.conteudo.find("createDragIndicator") == std::string::npos);
        CHECK(t.conteudo.find("indicatorTemplates") == std::string::npos);
        CHECK(t.conteudo.find("function(") == std::string::npos);
    }
}

TEST_CASE("topicos: materiais do topico usam o id avulso do jsfcljs", "[topicos]") {
    const auto d = doc("turma_rede.html");
    const auto c = parse::parseTurmaVirtual(d, "89151", "ADMINISTRACAO", "2026.2");

    int comMaterial = 0;
    std::vector<std::string> ids;
    for (const auto& t : c.topicos) {
        if (t.materiais.empty()) continue;
        ++comMaterial;
        for (const auto& m : t.materiais) {
            CHECK_FALSE(m.titulo.empty());
            CHECK_FALSE(m.id.empty());
            // Chave estavel e o `id` avulso; o id do COMPONENTE e posicional e
            // muda quando a instituicao recompila o JSP (§1.6.1).
            CHECK(m.id.find("j_id_jsp") == std::string::npos);
            CHECK(m.id.find(':') == std::string::npos);
            ids.push_back(m.id);
        }
    }
    REQUIRE(comMaterial > 0);
    REQUIRE(ids.size() == 2);
    // Ids distintos: um seletor que pegasse sempre o primeiro <a> do topico
    // daria dois materiais com o mesmo id, e a lista pareceria certa.
    CHECK(ids[0] != ids[1]);
}

TEST_CASE("topicos: o tipo do material sai do icone", "[topicos]") {
    CHECK(parse::tipoDoIcone("/sigaa/img/porta_arquivos/icones/tarefa.png") == "tarefa");
    CHECK(parse::tipoDoIcone("/sigaa/img/porta_arquivos/icones/arquivo.png") == "arquivo");
    // Sem icone nao inventamos tipo: chutar "arquivo" faria a UI oferecer
    // download de coisa que nao e arquivo.
    CHECK(parse::tipoDoIcone("").empty());
    CHECK(parse::tipoDoIcone("/sigaa/img/indicator.gif").empty());

    const auto d = doc("turma_rede.html");
    const auto c = parse::parseTurmaVirtual(d, "89151", "ADMINISTRACAO", "2026.2");
    bool achouTarefa = false;
    for (const auto& t : c.topicos) {
        for (const auto& m : t.materiais) {
            if (m.tipo == "tarefa") achouTarefa = true;
        }
    }
    CHECK(achouTarefa);
}

TEST_CASE("topicos: a descricao do material traz o prazo por extenso", "[topicos]") {
    const auto d = doc("turma_rede.html");
    const auto c = parse::parseTurmaVirtual(d, "89151", "ADMINISTRACAO", "2026.2");

    bool achou = false;
    for (const auto& t : c.topicos) {
        for (const auto& m : t.materiais) {
            if (m.descricao.find("finaliza em") != std::string::npos) achou = true;
        }
    }
    CHECK(achou);
}

TEST_CASE("topicos: turma sem nada cadastrado nao e erro de parse", "[topicos]") {
    html::Document vazio;
    REQUIRE(vazio.parse("<html><body><div class='intro-aval'>Nenhum topico</div></body></html>"));
    const auto c = parse::parseTurmaVirtual(vazio, "1", "X", "2026.2");
    CHECK(c.semTopicos);
    CHECK(c.topicos.empty());
}
