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

// ---------------------------------------------------------------------------
// Material publicado na turma. Este e o evento mais frequente do semestre:
// professor sobe o PDF da aula toda semana.
// ---------------------------------------------------------------------------

namespace {

ArquivoTurma arq(const std::string& id, const std::string& turma,
                 const std::string& titulo, const std::string& topico = "") {
    ArquivoTurma a;
    a.idArquivo = id;
    a.idTurma = turma;
    a.turmaNome = "ADM";
    a.titulo = titulo;
    a.topico = topico;
    return a;
}

} // namespace

TEST_CASE("material novo em turma ja conhecida vira evento", "[diff]") {
    Snapshot antes;
    antes.turmas = {turma("1", "ADM")};
    antes.arquivos = {arq("100", "1", "Aula 01")};

    Snapshot novo;
    novo.turmas = {turma("1", "ADM")};
    novo.arquivos = {arq("100", "1", "Aula 01"), arq("101", "1", "Aula 02", "Aula 2")};

    const auto r = diff(antes, novo, /*primeiraExecucao=*/false);
    REQUIRE(contar(r, TipoEvento::MaterialNovo) == 1);
    for (const auto& e : r.eventos) {
        if (e.tipo != TipoEvento::MaterialNovo) continue;
        CHECK(e.chave == "101");
        CHECK(e.titulo == "Aula 02");
        CHECK(e.turmaNome == "ADM");
    }
}

TEST_CASE("material que ja existia nao vira evento", "[diff]") {
    Snapshot antes;
    antes.turmas = {turma("1", "ADM")};
    antes.arquivos = {arq("100", "1", "Aula 01"), arq("101", "1", "Aula 02")};

    Snapshot novo = antes;
    CHECK(contar(diff(antes, novo, false), TipoEvento::MaterialNovo) == 0);
}

TEST_CASE("renomear o arquivo no SIGAA nao e material novo", "[diff]") {
    // O titulo e texto livre do professor; a chave e o id (RECON 1.6.1).
    // Comparar por titulo faria cada renomeacao virar notificacao, e alarme
    // falso recorrente e o jeito mais rapido de ensinar alguem a ignorar tudo.
    Snapshot antes;
    antes.turmas = {turma("1", "ADM")};
    antes.arquivos = {arq("100", "1", "Aula 3")};

    Snapshot novo;
    novo.turmas = {turma("1", "ADM")};
    novo.arquivos = {arq("100", "1", "Aula 03 - revisada")};

    CHECK(contar(diff(antes, novo, false), TipoEvento::MaterialNovo) == 0);
}

TEST_CASE("a primeira coleta de uma turma nao anuncia o acervo inteiro", "[diff]") {
    // A turma 2 nunca teve arquivo coletado: o que veio agora e a linha de
    // base dela, nao seis publicacoes de hoje.
    Snapshot antes;
    antes.turmas = {turma("1", "ADM"), turma("2", "EDO")};
    antes.arquivos = {arq("100", "1", "Aula 01")};

    Snapshot novo;
    novo.turmas = antes.turmas;
    novo.arquivos = {arq("100", "1", "Aula 01"), arq("200", "2", "Apostila"),
                     arq("201", "2", "Lista 1"), arq("202", "2", "Lista 2")};

    CHECK(contar(diff(antes, novo, false), TipoEvento::MaterialNovo) == 0);
}

TEST_CASE("sync sem turmas nao apaga nem acusa material", "[diff]") {
    // Sem --turmas a coleta nao entra em turma nenhuma e traz zero arquivos.
    // Tratar isso como "os arquivos sumiram" seria alarme falso garantido.
    Snapshot antes;
    antes.turmas = {turma("1", "ADM")};
    antes.arquivos = {arq("100", "1", "Aula 01")};

    Snapshot novo;
    novo.turmas = {turma("1", "ADM")};
    novo.arquivos = {};

    const auto r = diff(antes, novo, false);
    CHECK(contar(r, TipoEvento::MaterialNovo) == 0);
    CHECK_FALSE(r.suspeito);
}
