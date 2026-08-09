// Aba "Arquivos" da Turma Virtual: listagem e nome do arquivo baixado.
//
// As fixtures sao HTML CRU DA REDE (capturado com `sigaa-cli explorar`, depois
// redigido por tools/redact.py) — nunca Ctrl+S do browser, que salva o DOM
// pos-JavaScript e mente sobre o que o servidor manda (docs/RECON.md §1.7).
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "core/http/SigaaSession.h"
#include "core/parse/ArquivoParser.h"
#include "core/parse/Html.h"

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
    REQUIRE(d.parse(lerFixture(nome)));
    return d;
}

} // namespace

TEST_CASE("arquivos: a lista da rede traz os materiais com id estavel", "[arquivos]") {
    const auto d = doc("arquivos_rede.html");
    const auto lista = parse::parseArquivos(d, "89165", "PROJETO E ANALISE DE ALGORITMOS");

    CHECK(lista.pareceAbaArquivos);
    CHECK_FALSE(lista.vazioConfirmado);
    REQUIRE(lista.arquivos.size() == 6);

    const auto& a = lista.arquivos.front();
    // O `id` avulso do jsfcljs, NAO o id do componente JSF. Se algum dia isto
    // virar "formAva:j_id_jsp_1360160502_299", o download passa a baixar o
    // arquivo errado depois que a instituicao recompilar a pagina.
    CHECK(a.idArquivo == "2686969");
    CHECK(a.titulo.find("Teoria dos Grafos") != std::string::npos);
    CHECK(a.topico == "Materiais auxiliares para Estudo");
    CHECK(a.idTurma == "89165");

    // Ids diferentes entre si: um bug de seletor que pegasse sempre o primeiro
    // <a> da tabela daria seis linhas com o mesmo id, e a lista pareceria certa.
    for (size_t i = 1; i < lista.arquivos.size(); ++i) {
        CHECK(lista.arquivos[i].idArquivo != a.idArquivo);
    }

    // O ultimo arquivo esta em outro topico — a coluna de topico e o que
    // permite agrupar material por aula.
    CHECK(lista.arquivos.back().topico.find("Introdu") != std::string::npos);
}

TEST_CASE("arquivos: lista vazia e resposta do SIGAA, nao falha de parse", "[arquivos]") {
    // Turma sem material publicado. Confundir isto com erro encheria a tela de
    // alarme sobre o estado normal de metade das turmas.
    const auto d = doc("arquivos_vazio.html");
    const auto lista = parse::parseArquivos(d, "88094", "ADMINISTRACAO");

    CHECK(lista.pareceAbaArquivos);
    CHECK(lista.vazioConfirmado);
    CHECK(lista.arquivos.empty());
}

TEST_CASE("arquivos: outra pagina nao passa por aba de arquivos vazia", "[arquivos]") {
    // A pagina principal da turma tem tabelas e links, mas nao e a aba. Sem
    // esta distincao, uma sessao expirada (que devolve 200 com outra tela)
    // apareceria para o aluno como "esta turma nao tem material".
    const auto d = doc("turma_rede.html");
    const auto lista = parse::parseArquivos(d, "88094", "ADMINISTRACAO");

    CHECK_FALSE(lista.pareceAbaArquivos);
    CHECK(lista.arquivos.empty());
}

// --- nome do arquivo baixado ----------------------------------------------

namespace {
http::Response comDisposition(const std::string& v) {
    http::Response r;
    r.cabecalhos["content-disposition"] = v;
    return r;
}
} // namespace

TEST_CASE("download: o nome sai do Content-Disposition", "[arquivos][http]") {
    // Foi assim que o SIGAA respondeu de verdade no primeiro download.
    CHECK(comDisposition("attachment; filename=\"2026_ECOI2207_cronograma.pdf\"")
              .nomeSugerido() == "2026_ECOI2207_cronograma.pdf");
    CHECK(comDisposition("attachment;filename=aula.pdf").nomeSugerido() == "aula.pdf");
    CHECK(http::Response{}.nomeSugerido().empty());
}

TEST_CASE("download: filename estrela vence e vem percent-decoded", "[arquivos][http]") {
    // Quando os dois aparecem, o `filename=` puro e a versao degradada.
    const auto r = comDisposition(
        "attachment; filename=\"aula.pdf\"; filename*=UTF-8''aula%20de%20hoje.pdf");
    CHECK(r.nomeSugerido() == "aula de hoje.pdf");
}

TEST_CASE("download: nome do servidor nao escapa do diretorio", "[arquivos][http]") {
    // O nome vem do servidor. Um caminho relativo aqui escreveria fora da pasta
    // que o usuario escolheu — fica so o ultimo componente.
    CHECK(comDisposition("attachment; filename=\"..\\\\..\\\\evil.exe\"").nomeSugerido() ==
          "evil.exe");
    CHECK(comDisposition("attachment; filename=\"/etc/passwd\"").nomeSugerido() == "passwd");
    CHECK(comDisposition("attachment; filename=\"a:b?c.pdf\"").nomeSugerido() == "a_b_c.pdf");
}

TEST_CASE("download: html nao conta como arquivo", "[arquivos][http]") {
    // Sessao expirada devolve 200 com a tela de login. Salvar isso daria um
    // "apostila.pdf" que so revela o erro na hora de abrir.
    http::Response html;
    html.cabecalhos["content-type"] = "text/html;charset=windows-1252";
    CHECK_FALSE(html.ehDownload());

    http::Response pdf;
    pdf.cabecalhos["content-type"] = "application/pdf";
    CHECK(pdf.ehDownload());
}

TEST_CASE("download: nome com acento sai em utf-8", "[arquivos][http]") {
    // O SIGAA serve windows-1252 (RECON §1.10) e o Content-Disposition segue a
    // mesma sorte. Sem normalizar, "Introducao" com cedilha chega ao disco com
    // o acento quebrado — e no Windows ainda cria uma pasta/arquivo paralelo.
    const auto r = comDisposition("attachment; filename=\"Introdu\xE7\xE3o.pdf\"");
    CHECK(r.nomeSugerido() == "Introdu\xC3\xA7\xC3\xA3o.pdf");

    // Se ja vier UTF-8, nao mexe.
    const auto j = comDisposition("attachment; filename=\"Introdu\xC3\xA7\xC3\xA3o.pdf\"");
    CHECK(j.nomeSugerido() == "Introdu\xC3\xA7\xC3\xA3o.pdf");
}
