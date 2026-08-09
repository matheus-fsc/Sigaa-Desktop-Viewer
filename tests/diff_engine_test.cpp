// Testes do motor de diff. Logica pura, sem rede e sem banco.
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include "core/sync/DiffEngine.h"

using namespace sigaa;
using namespace sigaa::sync;

namespace {

Atividade ativ(const std::string& id, const std::string& turma,
               const std::string& titulo, int dia,
               StatusAtividade st = StatusAtividade::Pendente) {
    Atividade a;
    a.atividadeId = id;
    a.idTurma = turma;
    a.titulo = titulo;
    a.prazo = DateTime{2026, 8, dia, 23, 59, true};
    a.status = st;
    return a;
}

Turma turma(const std::string& id, const std::string& nome) {
    Turma t;
    t.idTurma = id;
    t.nome = nome;
    return t;
}

int contar(const ResultadoDiff& r, TipoEvento t) {
    int n = 0;
    for (const auto& e : r.eventos) {
        if (e.tipo == t) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("primeira execucao nao gera alarme", "[diff]") {
    // Sem isso, o primeiro sync dispararia uma notificacao por item existente.
    Snapshot novo;
    novo.turmas = {turma("1", "ADM"), turma("2", "EDO")};
    novo.atividades = {ativ("10", "1", "Tarefa 1", 11), ativ("11", "2", "Lista", 14)};

    const auto r = diff(Snapshot{}, novo, /*primeiraExecucao=*/true);
    CHECK(r.primeiraExecucao);
    CHECK(r.eventos.empty());
    CHECK_FALSE(r.suspeito);
}

TEST_CASE("atividade nova gera evento", "[diff]") {
    Snapshot antes;
    antes.atividades = {ativ("10", "1", "Tarefa 1", 11)};
    Snapshot depois = antes;
    depois.atividades.push_back(ativ("11", "1", "Tarefa 2", 14));

    const auto r = diff(antes, depois, false);
    REQUIRE(contar(r, TipoEvento::AtividadeNova) == 1);
    CHECK(r.eventos[0].titulo == "Tarefa 2");
}

TEST_CASE("prazo alterado gera evento", "[diff]") {
    // Importante: o aluno pode ter se planejado pela data antiga.
    Snapshot antes;
    antes.atividades = {ativ("10", "1", "Tarefa 1", 11)};
    Snapshot depois;
    depois.atividades = {ativ("10", "1", "Tarefa 1", 18)};

    const auto r = diff(antes, depois, false);
    REQUIRE(contar(r, TipoEvento::PrazoMudou) == 1);
    CHECK(r.eventos[0].detalhe.find("2026-08-11") != std::string::npos);
    CHECK(r.eventos[0].detalhe.find("2026-08-18") != std::string::npos);
}

TEST_CASE("conclusao gera evento so na transicao", "[diff]") {
    Snapshot antes;
    antes.atividades = {ativ("10", "1", "Tarefa 1", 11, StatusAtividade::Pendente)};
    Snapshot depois;
    depois.atividades = {ativ("10", "1", "Tarefa 1", 11, StatusAtividade::Concluida)};

    CHECK(contar(diff(antes, depois, false), TipoEvento::AtividadeConcluida) == 1);
    // ja concluida antes -> nao repete
    CHECK(contar(diff(depois, depois, false), TipoEvento::AtividadeConcluida) == 0);
}

TEST_CASE("mesma chave em turmas diferentes nao se confunde", "[diff]") {
    // A chave e (atividade_id, id_turma); so o id nao basta.
    Snapshot antes;
    antes.atividades = {ativ("10", "1", "Tarefa da ADM", 11)};
    Snapshot depois = antes;
    depois.atividades.push_back(ativ("10", "2", "Tarefa da EDO", 11));

    CHECK(contar(diff(antes, depois, false), TipoEvento::AtividadeNova) == 1);
}

TEST_CASE("nada mudou nao gera evento", "[diff]") {
    Snapshot s;
    s.turmas = {turma("1", "ADM")};
    s.atividades = {ativ("10", "1", "Tarefa 1", 11)};

    const auto r = diff(s, s, false);
    CHECK(r.eventos.empty());
    CHECK_FALSE(r.suspeito);
}

TEST_CASE("coleta vazia e suspeita, nao silencio", "[diff]") {
    // O pior modo de falha do app: o parser quebra, devolve zero, e o usuario
    // conclui que nao tem prazo nenhum.
    Snapshot antes;
    antes.turmas = {turma("1", "ADM"), turma("2", "EDO")};
    antes.atividades = {ativ("10", "1", "Tarefa 1", 11)};

    const auto r = diff(antes, Snapshot{}, false);
    CHECK(r.suspeito);
    REQUIRE(contar(r, TipoEvento::ColetaSuspeita) == 1);
    CHECK(r.eventos[0].detalhe.find("parser") != std::string::npos);
}

TEST_CASE("vazio na primeira execucao nao e suspeito", "[diff]") {
    // Conta nova, ou periodo sem matricula: legitimamente vazio.
    const auto r = diff(Snapshot{}, Snapshot{}, true);
    CHECK_FALSE(r.suspeito);
    CHECK(r.eventos.empty());
}

TEST_CASE("prova remarcada gera evento", "[diff]") {
    // Das informacoes mais importantes que o app pode dar.
    Avaliacao antesA;
    antesA.idTurma = "1";
    antesA.descricao = "1a Avaliacao";
    antesA.quando = DateTime{2026, 9, 29, 0, 0, false};

    Avaliacao depoisA = antesA;
    depoisA.quando = DateTime{2026, 10, 6, 0, 0, false};

    Snapshot antes, depois;
    antes.avaliacoes = {antesA};
    depois.avaliacoes = {depoisA};

    const auto r = diff(antes, depois, false);
    REQUIRE(contar(r, TipoEvento::AvaliacaoRemarcada) == 1);
    CHECK(r.eventos[0].detalhe.find("2026-09-29") != std::string::npos);
    CHECK(r.eventos[0].detalhe.find("2026-10-06") != std::string::npos);
}

TEST_CASE("sync sem --turmas nao alarma sobre provas", "[diff]") {
    // Uma coleta so-portal traz zero avaliacoes; isso NAO significa que as
    // provas sumiram. Sem esta guarda, todo sync rapido geraria alarme falso.
    Avaliacao a;
    a.idTurma = "1";
    a.descricao = "1a Avaliacao";
    a.quando = DateTime{2026, 9, 29, 0, 0, false};

    Snapshot antes;
    antes.avaliacoes = {a};
    antes.turmas = {turma("1", "EDO")};
    antes.atividades = {ativ("10", "1", "Tarefa", 11)};

    Snapshot depois = antes;
    depois.avaliacoes.clear();      // sync sem --turmas

    const auto r = diff(antes, depois, false);
    CHECK(r.eventos.empty());
    CHECK_FALSE(r.suspeito);
}

TEST_CASE("atualizacao nova detectada pelo hash", "[diff]") {
    Snapshot antes;
    Atualizacao u1;
    u1.hash = "aaa";
    u1.texto = "Nova tarefa: X";
    antes.atualizacoes = {u1};

    Snapshot depois = antes;
    Atualizacao u2;
    u2.hash = "bbb";
    u2.texto = "Novo Arquivo: Y";
    depois.atualizacoes.push_back(u2);

    const auto r = diff(antes, depois, false);
    REQUIRE(contar(r, TipoEvento::AtualizacaoNova) == 1);
    CHECK(r.eventos[0].chave == "bbb");
}
