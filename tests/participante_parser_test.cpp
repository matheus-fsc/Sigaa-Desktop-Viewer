// Aba "Participantes" da Turma Virtual.
//
// A fixture e HTML CRU DA REDE — a resposta do POST em /sigaa/ava/index.jsf
// extraida de uma captura HAR, depois redigida por tools/redact.py. Nunca
// Ctrl+S do browser, que salva o DOM pos-JavaScript (docs/RECON.md §1.7).
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "core/parse/Html.h"
#include "core/parse/ParticipanteParser.h"

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

size_t contar(const std::vector<Participante>& v, PapelParticipante papel) {
    return static_cast<size_t>(
        std::count_if(v.begin(), v.end(),
                      [papel](const Participante& p) { return p.papel == papel; }));
}

} // namespace

TEST_CASE("participantes: a lista da rede traz docente e discentes", "[participantes]") {
    const auto d = doc("participantes_rede.html");
    const auto lista = parse::parseParticipantes(d, "89165", "COMPILADORES");

    CHECK(lista.pareceAbaParticipantes);

    // O <legend> declara quantos sao. Conferir contra o que extraimos e o que
    // transforma "o seletor quebrou" em falha de teste em vez de lista curta.
    CHECK(lista.docentesDeclarados == 1);
    CHECK(lista.discentesDeclarados == 31);
    REQUIRE(lista.participantes.size() == 32);
    CHECK(contar(lista.participantes, PapelParticipante::Docente) == 1);
    CHECK(contar(lista.participantes, PapelParticipante::Discente) == 31);

    for (const auto& p : lista.participantes) {
        CHECK(p.idTurma == "89165");
        CHECK(p.turmaNome == "COMPILADORES");
        CHECK_FALSE(p.nome.empty());
    }
}

TEST_CASE("participantes: o docente traz departamento, nao matricula", "[participantes]") {
    const auto d = doc("participantes_rede.html");
    const auto lista = parse::parseParticipantes(d, "89165", "COMPILADORES");

    const auto it = std::find_if(
        lista.participantes.begin(), lista.participantes.end(),
        [](const Participante& p) { return p.papel == PapelParticipante::Docente; });
    REQUIRE(it != lista.participantes.end());

    // O nome do docente mora DENTRO do link para a pagina publica dele; o do
    // discente e texto solto. O parser le os dois pelo <strong> que os envolve.
    CHECK(it->nome == "DOCENTE TESTE");
    CHECK(it->departamento.find("INSTITUTO DE CI") != std::string::npos);
    CHECK(it->formacao == "DOUTORADO");
    // "E-Mail:" com M maiusculo so no bloco do docente — a busca e case-insensitive.
    CHECK(it->email == "aluno@example.edu");
    // Campos do outro papel ficam vazios, e nao preenchidos com lixo do vizinho.
    CHECK(it->matricula.empty());
    CHECK(it->curso.empty());
}

TEST_CASE("participantes: o discente traz curso e matricula", "[participantes]") {
    const auto d = doc("participantes_rede.html");
    const auto lista = parse::parseParticipantes(d, "89165", "COMPILADORES");

    size_t comCurso = 0, comMatricula = 0, comEmail = 0;
    for (const auto& p : lista.participantes) {
        if (p.papel != PapelParticipante::Discente) continue;
        CHECK(p.departamento.empty());
        if (!p.curso.empty()) ++comCurso;
        if (!p.matricula.empty()) ++comMatricula;
        if (!p.email.empty()) ++comEmail;
    }
    // Os 31 tinham os tres campos na captura. Se um seletor passar a colher o
    // <em> errado, estes numeros caem antes de a tela mostrar coluna vazia.
    CHECK(comCurso == 31);
    CHECK(comMatricula == 31);
    CHECK(comEmail == 31);

    const auto it = std::find_if(
        lista.participantes.begin(), lista.participantes.end(),
        [](const Participante& p) { return p.papel == PapelParticipante::Discente; });
    REQUIRE(it != lista.participantes.end());
    CHECK(it->curso.find("ENGENHARIA DE COMPUTA") != std::string::npos);
    // A fixture e redigida: matricula e e-mail sao os placeholders do redact.py.
    CHECK(it->matricula == "2000000000");
    CHECK(it->email == "aluno@example.edu");
    // O nome NAO pode arrastar o rotulo seguinte junto.
    CHECK(it->nome.find("Curso") == std::string::npos);
}

TEST_CASE("participantes: duas pessoas por linha nao viram uma", "[participantes]") {
    // O bloco de discentes vem em DUAS COLUNAS: cada <tr> guarda duas pessoas.
    // Iterar por linha dava 16 participantes em vez de 31, e — pior que a
    // contagem — colava o nome do segundo no fim do e-mail do primeiro.
    //
    // Este teste tranca as duas coisas: ninguem some, e nenhum campo carrega
    // resto do vizinho.
    const auto d = doc("participantes_rede.html");
    const auto lista = parse::parseParticipantes(d, "89165", "COMPILADORES");

    REQUIRE(lista.participantes.size() == 32);
    for (const auto& p : lista.participantes) {
        CHECK(p.nome.find('@') == std::string::npos);
        CHECK(p.email.find("ALUNO") == std::string::npos);
        CHECK(p.email.find(' ') == std::string::npos);
        CHECK(p.matricula.find(' ') == std::string::npos);
    }
}

TEST_CASE("participantes: campoRotulado corta no proximo rotulo", "[participantes]") {
    const std::string t = "FULANO Curso: ENGENHARIA/ICT Matricula: 2000000000 "
                          "E-mail: a@b.edu";
    CHECK(parse::campoRotulado(t, "Curso") == "ENGENHARIA/ICT");
    CHECK(parse::campoRotulado(t, "Matr") == "2000000000");
    CHECK(parse::campoRotulado(t, "E-mail") == "a@b.edu");
    CHECK(parse::campoRotulado(t, "Departamento").empty());

    // Campo ausente no meio: o valor anterior nao pode engolir o resto da linha.
    const std::string semCurso = "FULANO Matricula: 2000000000 E-mail: a@b.edu";
    CHECK(parse::campoRotulado(semCurso, "Matr") == "2000000000");
    CHECK(parse::campoRotulado(semCurso, "Curso").empty());

    // Caixa do rotulo varia entre os blocos ("E-Mail:" no docente).
    CHECK(parse::campoRotulado("X E-Mail: z@w.edu", "E-mail") == "z@w.edu");
}

TEST_CASE("participantes: contagem do legend", "[participantes]") {
    CHECK(parse::contagemDoLegend(" Discentes (31)") == 31);
    CHECK(parse::contagemDoLegend(" Docentes (1)") == 1);
    CHECK(parse::contagemDoLegend("Discentes") == 0);
    CHECK(parse::contagemDoLegend("Discentes (varios)") == 0);
}

TEST_CASE("participantes: outra pagina nao passa por aba de participantes",
          "[participantes]") {
    // Sessao expirada devolve 200 com outra tela. Sem esta distincao, a janela
    // diria ao aluno que a turma nao tem colegas.
    const auto d = doc("turma_rede.html");
    const auto lista = parse::parseParticipantes(d, "88094", "ADMINISTRACAO");

    CHECK_FALSE(lista.pareceAbaParticipantes);
    CHECK(lista.participantes.empty());
}

TEST_CASE("participantes: a celula da foto nao vira gente", "[participantes]") {
    // O parser NAO le mais a URL do retrato (removido em 18/09/2026), mas o
    // <td> da foto continua no HTML, intercalado entre as pessoas. Ele nao
    // pode ser confundido com um participante nem deslocar os campos do
    // vizinho — era a armadilha que a leitura da foto ajudava a contornar, e
    // que agora precisa se sustentar sozinha.
    const auto d = doc("participantes_rede.html");
    const auto lista = parse::parseParticipantes(d, "88094", "COMPILADORES");

    // 32 pessoas, nem uma a mais: cada celula de foto que virasse participante
    // apareceria aqui como uma linha extra sem nome.
    REQUIRE(lista.participantes.size() == 32);
    for (const auto& p : lista.participantes) {
        CHECK_FALSE(p.nome.empty());
    }

    // O docente e o primeiro da pagina e tem retrato. Se a celula da foto
    // escorregasse os campos em um, ele perderia o papel ou o departamento.
    CHECK(lista.participantes.front().papel == PapelParticipante::Docente);
    CHECK_FALSE(lista.participantes.front().departamento.empty());
}
