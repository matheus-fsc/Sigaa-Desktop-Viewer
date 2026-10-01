// A matéria da prova e o texto dobrado que ela usa para comparar.
//
// `materiaDaProva` responde "o que cai na P2" na janela e, pelo MCP, para o
// agente de IA. Os casos abaixo são os que os comentários da regra descrevem —
// cada um já deu resposta errada antes de virar regra.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/avaliacao/Materia.h"
#include "core/util/Texto.h"

using namespace sigaa;

namespace {

DateTime dia(int y, int m, int d) {
    DateTime t;
    t.year = y;
    t.month = m;
    t.day = d;
    return t;
}

avaliacao::Efetiva prova(const std::string& desc, DateTime quando, const std::string& turma = "1") {
    avaliacao::Efetiva e;
    e.av.idTurma = turma;
    e.av.descricao = desc;
    e.av.quando = quando;
    return e;
}

TopicoAula topico(const std::string& titulo, DateTime quando, const std::string& turma = "1") {
    TopicoAula t;
    t.idTurma = turma;
    t.titulo = titulo;
    t.inicio = quando;
    t.fim = quando;
    return t;
}

} // namespace

TEST_CASE("texto: dobrar tira acento, caixa e espaco repetido", "[texto]") {
    CHECK(util::dobrar("EQUAÇÕES DIFERENCIAIS ORDINÁRIAS") == "equacoes diferenciais ordinarias");
    CHECK(util::dobrar("  Análise   e  Revisão ") == "analise e revisao");
    CHECK(util::dobrar("Ñandú") == "nandu");
    CHECK(util::dobrar("2 × 3") == "2 × 3");   // sinal, não letra: fica
    CHECK(util::contemDobrado("PROJETO E ANÁLISE DE ALGORITMOS", "analise"));
    CHECK_FALSE(util::contemDobrado("COMPILADORES", "algoritmos"));
}

TEST_CASE("texto: sigla como o aluno abrevia a materia", "[texto]") {
    CHECK(util::sigla("EQUAÇÕES DIFERENCIAIS ORDINÁRIAS") == "edo");
    CHECK(util::sigla("INTELIGÊNCIA ARTIFICIAL") == "ia");
    CHECK(util::sigla("PROJETO E ANÁLISE DE ALGORITMOS") == "paa");
    CHECK(util::sigla("ANÁLISE E DESENVOLVIMENTO DE SOFTWARE IV") == "ads");
    CHECK(util::sigla("COMPILADORES") == "c");
}

TEST_CASE("texto: casaProva entende P2, N1 e o nome inteiro", "[texto]") {
    CHECK(util::casaProva("P2", "Prova 2"));
    CHECK(util::casaProva("n2", "Avaliação 2 (N2)"));
    CHECK(util::casaProva("prova 2", "Prova 2"));
    CHECK(util::casaProva("Avaliação 2", "AVALIACAO 2 (N2)"));
    CHECK_FALSE(util::casaProva("P2", "Prova 12"));
    CHECK_FALSE(util::casaProva("P1", "Prova 2"));
    CHECK_FALSE(util::casaProva("", "Prova 2"));
}

TEST_CASE("materia: so os topicos entre a prova anterior e esta", "[materia]") {
    Snapshot s;
    s.topicos = {topico("Introdução", dia(2026, 8, 10)),
                 topico("Autômatos", dia(2026, 8, 20)),
                 topico("Análise léxica", dia(2026, 9, 10)),
                 topico("Análise sintática", dia(2026, 9, 20))};
    const auto p1 = prova("Prova 1", dia(2026, 9, 1));
    const auto p2 = prova("Prova 2", dia(2026, 10, 1));
    const std::vector<avaliacao::Efetiva> todas{p1, p2};

    const auto m1 = avaliacao::materiaDaProva(s, p1, todas);
    CHECK(m1.coletada);
    CHECK_FALSE(m1.desde.valid());
    CHECK(m1.topicos == std::vector<std::string>{"Introdução", "Autômatos"});

    const auto m2 = avaliacao::materiaDaProva(s, p2, todas);
    CHECK(m2.provaAnterior == "Prova 1");
    CHECK(m2.desde.toIso() == "2026-09-01");
    CHECK(m2.topicos == std::vector<std::string>{"Análise léxica", "Análise sintática"});
}

TEST_CASE("materia: topico do dia da prova e da proxima; do dia da anterior entra",
          "[materia]") {
    Snapshot s;
    s.topicos = {topico("Séries de potências", dia(2026, 9, 1)),       // dia da P1
                 topico("Noções de sequências e séries", dia(2026, 10, 1))};  // dia da P2
    const auto p1 = prova("Avaliação 1", dia(2026, 9, 1));
    const auto p2 = prova("Avaliação 2", dia(2026, 10, 1));
    const std::vector<avaliacao::Efetiva> todas{p1, p2};

    const auto m2 = avaliacao::materiaDaProva(s, p2, todas);
    CHECK(m2.topicos == std::vector<std::string>{"Séries de potências"});
}

TEST_CASE("materia: anuncio de prova, revisao e feriado nao sao materia", "[materia]") {
    Snapshot s;
    s.topicos = {topico("Gramáticas livres de contexto", dia(2026, 9, 5)),
                 topico("REVISÃO para a prova", dia(2026, 9, 6)),
                 topico("Não haverá aula", dia(2026, 9, 7)),
                 topico("Aula de Exercícios", dia(2026, 9, 8)),
                 topico("Primeira Avaliação", dia(2026, 9, 9)),
                 topico("Quiz 2", dia(2026, 9, 9))};
    const auto p = prova("P1", dia(2026, 9, 10));
    const auto m = avaliacao::materiaDaProva(s, p, {p});
    CHECK(m.topicos == std::vector<std::string>{"Gramáticas livres de contexto"});
}

TEST_CASE("materia: o mesmo titulo registrado duas vezes conta uma", "[materia]") {
    Snapshot s;
    s.topicos = {topico("Árvores AVL", dia(2026, 9, 2)),
                 topico("árvores avl.", dia(2026, 9, 4)),
                 topico("Heaps", dia(2026, 9, 6))};
    const auto p = prova("P1", dia(2026, 9, 10));
    const auto m = avaliacao::materiaDaProva(s, p, {p});
    CHECK(m.topicos == std::vector<std::string>{"Árvores AVL", "Heaps"});
}

TEST_CASE("materia: arquivos do topico e da aba Arquivos, so da turma", "[materia]") {
    Snapshot s;
    auto t = topico("Aula de Exercícios", dia(2026, 9, 5));   // não é matéria, mas tem material
    MaterialTopico lista;
    lista.id = "111";
    t.materiais.push_back(lista);
    s.topicos = {t, topico("Grafos", dia(2026, 9, 6)), topico("Grafos", dia(2026, 9, 6), "2")};

    ArquivoTurma a;
    a.idTurma = "1";
    a.idArquivo = "222";
    a.topico = "Grafos";
    ArquivoTurma deOutra = a;
    deOutra.idTurma = "2";
    deOutra.idArquivo = "333";
    s.arquivos = {a, deOutra};

    const auto p = prova("P1", dia(2026, 9, 10));
    const auto m = avaliacao::materiaDaProva(s, p, {p});
    CHECK(m.idsArquivos == std::vector<std::string>{"111", "222"});
}

TEST_CASE("materia: turma sem topicos coletados", "[materia]") {
    Snapshot s;
    s.topicos = {topico("Grafos", dia(2026, 9, 6), "outra")};
    const auto p = prova("P1", dia(2026, 9, 10));
    const auto m = avaliacao::materiaDaProva(s, p, {p});
    CHECK_FALSE(m.coletada);
    CHECK(m.topicos.empty());
}
