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

// ---------------------------------------------------------------------------
// Arquivos e topicos seguem a MESMA regra das avaliacoes: coleta parcial nao
// apaga o que ela nao trouxe. Aqui o custo de errar e maior que o das provas,
// porque a tela inicial mostra "a aula de hoje" a cada abertura e coletar
// topico exige entrar em cada turma (~25 s) — sem acumular, a tela ficaria
// vazia ate o proximo ciclo completo terminar.
// ---------------------------------------------------------------------------

namespace {

ArquivoTurma umArquivo(const std::string& id, const std::string& titulo) {
    ArquivoTurma a;
    a.idArquivo = id;
    a.idTurma = "88094";
    a.turmaNome = "ADMINISTRACAO";
    a.titulo = titulo;
    a.topico = "Materiais auxiliares";
    return a;
}

TopicoAula umTopico(const std::string& titulo, int dia) {
    TopicoAula t;
    t.idTurma = "88094";
    t.titulo = titulo;
    t.inicio = DateTime{2026, 8, dia, 0, 0, false};
    t.fim = t.inicio;
    t.conteudo = "conteudo da aula";
    return t;
}

} // namespace

TEST_CASE("arquivo e topico sobrevivem ao ciclo de gravar e ler", "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.aberto());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas = {umaTurma()};
    s.arquivos = {umArquivo("526919", "LIVRO: Algoritmos")};
    s.topicos = {umTopico("Apresentacao da disciplina", 4)};
    REQUIRE(db.gravar(s, 1000));

    const Snapshot lido = db.carregarUltimo();
    REQUIRE(lido.arquivos.size() == 1);
    CHECK(lido.arquivos[0].idArquivo == "526919");
    CHECK(lido.arquivos[0].titulo == "LIVRO: Algoritmos");
    CHECK(lido.arquivos[0].topico == "Materiais auxiliares");

    REQUIRE(lido.topicos.size() == 1);
    CHECK(lido.topicos[0].titulo == "Apresentacao da disciplina");
    CHECK(lido.topicos[0].inicio.day == 4);
    CHECK(lido.topicos[0].conteudo == "conteudo da aula");
}

TEST_CASE("sync so-portal nao apaga arquivo nem topico ja conhecido",
          "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot completo;
    completo.turmas = {umaTurma()};
    completo.arquivos = {umArquivo("526919", "Apostila")};
    completo.topicos = {umTopico("Aula 1", 4)};
    REQUIRE(db.gravar(completo, 1000));

    // A coleta so-portal nao entra em turma nenhuma: vem sem arquivo e sem
    // topico. Se isso apagasse o que ja havia, a janela abriria vazia depois
    // de todo sync rapido — e o app roda um a cada 20 min.
    Snapshot soPortal;
    soPortal.turmas = {umaTurma()};
    REQUIRE(db.gravar(soPortal, 2000));

    const Snapshot lido = db.carregarUltimo();
    CHECK(lido.arquivos.size() == 1);
    CHECK(lido.topicos.size() == 1);
}

TEST_CASE("topico com o mesmo titulo em datas diferentes nao colide",
          "[database]") {
    // "Revisao" aparece varias vezes no semestre. Chave so por titulo faria a
    // aula de setembro sobrescrever a de agosto, e a linha do tempo perderia
    // metade das aulas sem nenhum erro visivel.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas = {umaTurma()};
    s.topicos = {umTopico("Revisao", 10), umTopico("Revisao", 20)};
    REQUIRE(db.gravar(s, 1000));

    CHECK(db.carregarUltimo().topicos.size() == 2);
}

TEST_CASE("topico volta do banco em ordem de data", "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas = {umaTurma()};
    s.topicos = {umTopico("Terceira", 20), umTopico("Primeira", 4),
                 umTopico("Segunda", 11)};
    REQUIRE(db.gravar(s, 1000));

    const auto t = db.carregarUltimo().topicos;
    REQUIRE(t.size() == 3);
    CHECK(t[0].titulo == "Primeira");
    CHECK(t[1].titulo == "Segunda");
    CHECK(t[2].titulo == "Terceira");
}
