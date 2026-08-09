// Invariantes da persistência.
//
// O que este arquivo protege é a suposição em que se apoiam DUAS decisões de
// projeto muito distantes uma da outra:
//
//   - o agendador roda `sync` só-portal a cada 20 min e `--turmas` 3x ao dia,
//     em vez de coleta completa o tempo todo (tools/agendar.ps1);
//   - a janela recarrega do banco depois de sincronizar, em vez de pintar a
//     tela com o snapshot da coleta (src/ui/JanelaPrincipal.cpp).
//
// As duas só são seguras porque gravar uma coleta parcial NÃO apaga o que ela
// não trouxe. Se alguém trocar o upsert por "apaga e insere", nada aqui
// explode em produção: o app simplesmente para de mostrar as provas do aluno,
// em silêncio. Daí o teste.
//
// Nome de TEST_CASE em ASCII puro: o CTest passa o nome como filtro e o
// codepage do console do Windows corrompe acento — o teste some com
// "No tests ran" em vez de falhar.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "core/store/Database.h"

using namespace sigaa;

namespace {

// Banco em arquivo temporário, removido no fim do escopo.
struct BancoTemp {
    std::filesystem::path caminho;

    BancoTemp() {
        static int n = 0;
        caminho = std::filesystem::temp_directory_path() /
                  ("sigaa-teste-" + std::to_string(++n) + ".db");
        std::filesystem::remove(caminho);
    }
    ~BancoTemp() {
        std::error_code ec;
        std::filesystem::remove(caminho, ec);
    }
    std::string str() const { return caminho.string(); }
};

Turma umaTurma() {
    Turma t;
    t.idTurma = "88094";
    t.frontEndId = "abc123";
    t.codigo = "EPRIB001";
    t.nome = "ADMINISTRACAO";
    t.periodo = "2026.2";
    return t;
}

Atividade umaAtividade(const std::string& titulo) {
    Atividade a;
    a.atividadeId = "130174755";
    a.idTurma = "88094";
    a.turmaNome = "ADMINISTRACAO";
    a.tipo = "Tarefa";
    a.titulo = titulo;
    a.prazo = DateTime{2026, 8, 20, 23, 59, true};
    return a;
}

Avaliacao umaAvaliacao() {
    Avaliacao av;
    av.idTurma = "88094";
    av.turmaNome = "ADMINISTRACAO";
    av.descricao = "1a Avaliacao";
    av.quando = DateTime{2026, 12, 3, 0, 0, false};
    av.fonte = FonteAvaliacao::PainelAvaliacoes;
    return av;
}

} // namespace

TEST_CASE("coleta so-portal nao apaga as provas ja conhecidas") {
    BancoTemp tmp;
    store::Database db(tmp.str());
    REQUIRE(db.aberto());
    REQUIRE(db.migrar());

    // Ciclo completo: veio com turma, atividade e prova.
    Snapshot completo;
    completo.turmas.push_back(umaTurma());
    completo.atividades.push_back(umaAtividade("Tarefa 1"));
    completo.avaliacoes.push_back(umaAvaliacao());
    REQUIRE(db.gravar(completo, 1000));

    REQUIRE(db.carregarUltimo().avaliacoes.size() == 1);

    // Ciclo só-portal: o crawler nao entra nas turmas, entao volta SEM
    // avaliacao nenhuma. Isso nao significa que as provas foram canceladas.
    Snapshot soPortal;
    soPortal.turmas.push_back(umaTurma());
    soPortal.atividades.push_back(umaAtividade("Tarefa 1"));
    REQUIRE(soPortal.avaliacoes.empty());
    REQUIRE(db.gravar(soPortal, 2000));

    const auto depois = db.carregarUltimo();
    CHECK(depois.avaliacoes.size() == 1);
    CHECK(depois.avaliacoes.front().descricao == "1a Avaliacao");
}

TEST_CASE("gravar atualiza a atividade em vez de duplicar") {
    BancoTemp tmp;
    store::Database db(tmp.str());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas.push_back(umaTurma());
    s.atividades.push_back(umaAtividade("Tarefa 1"));
    REQUIRE(db.gravar(s, 1000));

    // Mesma atividade (mesmo id), titulo corrigido pelo professor.
    Snapshot s2;
    s2.turmas.push_back(umaTurma());
    s2.atividades.push_back(umaAtividade("Tarefa 1 - revisada"));
    REQUIRE(db.gravar(s2, 2000));

    const auto depois = db.carregarUltimo();
    REQUIRE(depois.atividades.size() == 1);
    CHECK(depois.atividades.front().titulo == "Tarefa 1 - revisada");
}

TEST_CASE("ciclos comeca em zero e conta cada registro") {
    BancoTemp tmp;
    store::Database db(tmp.str());
    REQUIRE(db.migrar());

    // Zero e o que faz o DiffEngine tratar a primeira coleta como linha de
    // base, em vez de disparar uma notificacao por item ja existente.
    CHECK(db.ciclos() == 0);
    db.registrarCiclo(1000);
    CHECK(db.ciclos() == 1);
}
