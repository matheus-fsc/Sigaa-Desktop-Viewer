// O resumo da turma em Markdown.
//
// O ARQUIVO E FEITO PARA SER COLADO NUM ASSISTENTE DE IA, e e isso que decide
// o que este teste protege: a hierarquia de cabecalhos (e por ela que o modelo
// se orienta), a ordem cronologica das aulas (que e a ordem em que a materia
// se estuda) e a data das provas (que e a pergunta que leva alguem a abrir o
// arquivo). Um titulo de aula comecando com "#" bagunca os tres de uma vez.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/report/TurmaMd.h"

using namespace sigaa;

namespace {

DateTime dia(int y, int m, int d) {
    DateTime t;
    t.year = y; t.month = m; t.day = d;
    return t;
}

TopicoAula topico(const std::string& titulo, DateTime inicio) {
    TopicoAula t;
    t.titulo = titulo;
    t.inicio = inicio;
    return t;
}

report::DadosTurmaMd base() {
    report::DadosTurmaMd d;
    d.turma.nome = "COMPILADORES";
    d.turma.codigo = "ECOI2219";
    d.turma.cargaHoraria = 64;
    return d;
}

}  // namespace

TEST_CASE("o cabecalho traz a identificacao da turma", "[turmamd]") {
    const std::string md = report::gerarTurmaMd(base());
    CHECK(md.rfind("# COMPILADORES", 0) == 0);
    CHECK(md.find("ECOI2219") != std::string::npos);
    CHECK(md.find("64 h") != std::string::npos);
}

TEST_CASE("as aulas saem em ordem cronologica", "[turmamd]") {
    // O SIGAA devolve na ordem da PAGINA, que muda quando o professor edita um
    // topico antigo. A ordem que serve para estudar e a das datas.
    auto d = base();
    d.topicos.push_back(topico("Terceira", dia(2026, 9, 1)));
    d.topicos.push_back(topico("Primeira", dia(2026, 8, 3)));
    d.topicos.push_back(topico("Segunda", dia(2026, 8, 17)));

    const std::string md = report::gerarTurmaMd(d);
    const auto p1 = md.find("Primeira");
    const auto p2 = md.find("Segunda");
    const auto p3 = md.find("Terceira");
    REQUIRE(p1 != std::string::npos);
    CHECK(p1 < p2);
    CHECK(p2 < p3);
}

TEST_CASE("as avaliacoes vem antes das aulas", "[turmamd]") {
    // Quem abre este arquivo quase sempre esta perguntando "o que cai na prova
    // de quinta?", e a data da prova e o que delimita a resposta.
    auto d = base();
    d.topicos.push_back(topico("Aula 1", dia(2026, 8, 3)));
    avaliacao::Efetiva e;
    e.av.descricao = "Prova 1";
    e.av.quando = dia(2026, 9, 24);
    d.provas.push_back(e);

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("## Avaliacoes") == std::string::npos);   // com acento, abaixo
    CHECK(md.find("Prova 1") < md.find("## Aulas"));
}

TEST_CASE("a prova corrigida pelo aluno diz que foi corrigida", "[turmamd]") {
    // Sem isso o arquivo anunciaria uma data que o SIGAA nao confirma sem dizer
    // de onde ela veio — e quem estuda por ele nao teria como desconfiar.
    auto d = base();
    avaliacao::Efetiva e;
    e.av.descricao = "Prova 1";
    e.av.quando = dia(2026, 9, 24);
    e.estado = avaliacao::Estado::Editada;
    d.provas.push_back(e);

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("corrigida por voc") != std::string::npos);
}

TEST_CASE("titulo que parece marcacao nao vira cabecalho", "[turmamd]") {
    // Um topico chamado "## Revisao" viraria uma secao de nivel 2 e quebraria a
    // hierarquia do arquivo — que e justamente o que a IA usa para se orientar.
    auto d = base();
    d.topicos.push_back(topico("# Revisao geral", dia(2026, 8, 3)));

    const std::string md = report::gerarTurmaMd(d);
    // O titulo sai escapado, entao nao existe um cabecalho "# Revisao geral"
    // comecando uma linha.
    CHECK(md.find("\n# Revisao geral") == std::string::npos);
    CHECK(md.find("Revisao geral") != std::string::npos);
}

TEST_CASE("pipe no titulo nao quebra a tabela de avaliacoes", "[turmamd]") {
    // "Prova 1 | Turma A" partiria a linha da tabela em colunas a mais, e o
    // Markdown renderizaria uma tabela torta.
    auto d = base();
    avaliacao::Efetiva e;
    e.av.descricao = "Prova 1 | Turma A";
    e.av.quando = dia(2026, 9, 24);
    d.provas.push_back(e);

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("Prova 1 \\| Turma A") != std::string::npos);
}

TEST_CASE("turma sem topico diz isso em vez de ficar vazia", "[turmamd]") {
    // Secao "## Aulas" vazia parece arquivo truncado. Dizer que o professor nao
    // publicou nada e diferente de nao termos conseguido ler.
    const std::string md = report::gerarTurmaMd(base());
    CHECK(md.find("## Aulas") != std::string::npos);
    CHECK(md.find("ainda n") != std::string::npos);   // "ainda nao publicou"
}

TEST_CASE("arquivo sem topico correspondente nao some", "[turmamd]") {
    // Costuma ser a lista de exercicios e o livro da disciplina — o material
    // que mais se procura na vespera da prova.
    auto d = base();
    d.topicos.push_back(topico("Aula 1", dia(2026, 8, 3)));
    ArquivoTurma a;
    a.titulo = "Livro: Dragon Book";
    a.topico = "Materiais auxiliares";
    d.arquivos.push_back(a);

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("Outros arquivos da turma") != std::string::npos);
    CHECK(md.find("Dragon Book") != std::string::npos);
}

TEST_CASE("as faltas do aluno entram no cabecalho", "[turmamd]") {
    auto d = base();
    Frequencia f;
    f.aulasPelaCH = 64;
    f.aulasComRegistro = 24;
    f.presencas = 18;
    f.temDados = true;
    d.frequencia = &f;

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("6 de 16") != std::string::npos);
}

TEST_CASE("frequencia sem lancamento nao vira zero faltas", "[turmamd]") {
    // A mesma regra da tela: "o professor nao lancou nada" nao pode virar
    // "voce nao faltou" num arquivo que alguem vai ler sozinho depois.
    auto d = base();
    Frequencia f;
    f.aulasPelaCH = 64;
    f.temDados = false;
    d.frequencia = &f;

    const std::string md = report::gerarTurmaMd(d);
    CHECK(md.find("Faltas") == std::string::npos);
}

TEST_CASE("o mesmo dado gera sempre o mesmo texto", "[turmamd]") {
    // A pasta da turma pode estar num diretorio versionado. Regravar sem
    // mudanca nao pode produzir diferenca.
    auto d = base();
    d.topicos.push_back(topico("Aula 1", dia(2026, 8, 3)));
    CHECK(report::gerarTurmaMd(d) == report::gerarTurmaMd(d));
}
