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

#include <sqlite3.h>

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

// --- participantes ---------------------------------------------------------

namespace {

Participante umParticipante(const std::string& nome, PapelParticipante papel) {
    Participante p;
    p.idTurma = "88094";
    p.turmaNome = "ADMINISTRACAO";
    p.papel = papel;
    p.nome = nome;
    p.email = "aluno@example.edu";
    if (papel == PapelParticipante::Discente) {
        p.matricula = "2000000000";
        p.curso = "ENGENHARIA DE COMPUTACAO/ICT";
    } else {
        p.departamento = "INSTITUTO DE CIENCIAS TECNOLOGICAS";
        p.formacao = "DOUTORADO";
    }
    return p;
}

} // namespace

TEST_CASE("banco sem participante e o gatilho da coleta completa",
          "[database][participantes]") {
    // `participantesGuardados` e o que decide, ANTES de coletar, se o ciclo
    // paga uma requisicao por turma para abrir a aba Participantes. Se ele
    // mentisse "ja tem" num banco novo, a aba nunca seria populada sozinha; se
    // mentisse "nao tem" num banco cheio, todo ciclo pagaria a conta de novo.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    CHECK(db.participantesGuardados() == 0);

    Snapshot s;
    s.turmas = {umaTurma()};
    s.participantes = {umParticipante("ALUNO TESTE", PapelParticipante::Discente),
                       umParticipante("DOCENTE TESTE", PapelParticipante::Docente)};
    REQUIRE(db.gravar(s, 1000));

    CHECK(db.participantesGuardados() == 2);

    // Regravar os mesmos nao inventa gente nova — a chave e (id_turma, nome).
    REQUIRE(db.gravar(s, 2000));
    CHECK(db.participantesGuardados() == 2);
}

TEST_CASE("o banco nao tem onde guardar foto de participante",
          "[database][participantes]") {
    // O app ja baixou e guardou o retrato de cada colega de turma. A coleta
    // saiu em 18/09/2026: rosto e dado pessoal de quem aparece nele, e os 31
    // colegas nao escolheram ter a propria imagem copiada para a maquina de
    // ninguem.
    //
    // O teste olha o ESQUEMA, e nao um campo do struct, porque e o esquema que
    // decide o que sobrevive ao fechar o app. Se alguem reintroduzir a coleta,
    // a coluna aparece aqui antes de o primeiro retrato chegar ao disco.
    BancoTemp b;
    {
        store::Database db(b.str());
        REQUIRE(db.migrar());
    }

    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(b.str().c_str(), &raw) == SQLITE_OK);

    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw,
                               "SELECT name FROM pragma_table_info('participante')",
                               -1, &st, nullptr) == SQLITE_OK);
    int colunas = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        ++colunas;
        const std::string nome =
            reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        CHECK(nome.find("foto") == std::string::npos);
        CHECK(nome.find("imagem") == std::string::npos);
        CHECK(nome.find("retrato") == std::string::npos);
    }
    sqlite3_finalize(st);
    sqlite3_close(raw);

    // Se a consulta nao devolveu coluna nenhuma, a tabela nao existe e o teste
    // acima nao provou nada.
    CHECK(colunas > 0);
}

TEST_CASE("participante faz ida e volta com os campos dos dois papeis",
          "[database][participantes]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas = {umaTurma()};
    s.participantes = {umParticipante("ALUNO TESTE", PapelParticipante::Discente),
                       umParticipante("DOCENTE TESTE", PapelParticipante::Docente)};
    REQUIRE(db.gravar(s, 1000));

    const auto p = db.carregarUltimo().participantes;
    REQUIRE(p.size() == 2);

    // Docente primeiro: e ele que a tela mostra no topo, e a ordem sai do
    // banco para que o .ics, o relatorio e a janela nao a reinventem cada um.
    CHECK(p[0].papel == PapelParticipante::Docente);
    CHECK(p[0].nome == "DOCENTE TESTE");
    CHECK(p[0].formacao == "DOUTORADO");
    CHECK(p[0].matricula.empty());

    CHECK(p[1].papel == PapelParticipante::Discente);
    CHECK(p[1].matricula == "2000000000");
    CHECK(p[1].curso == "ENGENHARIA DE COMPUTACAO/ICT");
    CHECK(p[1].departamento.empty());
}

TEST_CASE("coleta so-portal nao apaga os participantes ja conhecidos",
          "[database][participantes]") {
    // Mesma invariante que protege arquivos e provas: `sync` sem --turmas traz
    // zero participantes, e um DELETE aqui esvaziaria a aba ate a proxima
    // coleta completa — que so roda 3x ao dia (tools/agendar.ps1).
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot completo;
    completo.turmas = {umaTurma()};
    completo.participantes = {umParticipante("ALUNO TESTE", PapelParticipante::Discente)};
    REQUIRE(db.gravar(completo, 1000));

    Snapshot soPortal;
    soPortal.turmas = {umaTurma()};
    REQUIRE(db.gravar(soPortal, 2000));

    CHECK(db.carregarUltimo().participantes.size() == 1);
}

TEST_CASE("participante e atualizado no lugar, nao duplicado",
          "[database][participantes]") {
    // A chave e (id_turma, nome). O aluno troca de e-mail entre um semestre e
    // outro e a linha tem que ser a mesma — duas linhas dariam a mesma pessoa
    // duas vezes na lista da turma.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    Snapshot s;
    s.turmas = {umaTurma()};
    s.participantes = {umParticipante("ALUNO TESTE", PapelParticipante::Discente)};
    REQUIRE(db.gravar(s, 1000));

    s.participantes[0].email = "outro@example.edu";
    REQUIRE(db.gravar(s, 2000));

    const auto p = db.carregarUltimo().participantes;
    REQUIRE(p.size() == 1);
    CHECK(p[0].email == "outro@example.edu");
}

// ---------------------------------------------------------------------------
// Correcoes do aluno sobre datas de prova
//
// O invariante central: um sync REESCREVE a tabela `avaliacao` inteira, e a
// correcao tem de sobreviver a isso. Foi por causa deste teste que ela nasceu
// numa tabela separada em vez de numa coluna ao lado da data do SIGAA.
// ---------------------------------------------------------------------------

namespace {

avaliacao::Ajuste ajusteDeTeste() {
    avaliacao::Ajuste a;
    a.idTurma = "T1";
    a.descricao = "Prova 1";
    a.turmaNome = "INTELIGENCIA ARTIFICIAL";
    a.quando.year = 2026; a.quando.month = 9; a.quando.day = 27;
    a.quando.hour = 15; a.quando.minute = 45; a.quando.hasTime = true;
    a.horarioBruto = "15:45";
    a.nota = "adiada em sala";
    a.confirmada = true;
    a.quandoSigaaNaEpoca.year = 2026;
    a.quandoSigaaNaEpoca.month = 9;
    a.quandoSigaaNaEpoca.day = 20;
    a.editadoEm = 1750000000;
    return a;
}

}  // namespace

TEST_CASE("ajuste sobrevive ao fechar e reabrir o banco", "[database]") {
    BancoTemp b;
    {
        store::Database db(b.str());
        REQUIRE(db.migrar());
        REQUIRE(db.gravarAjuste(ajusteDeTeste()));
    }
    store::Database db(b.str());
    REQUIRE(db.migrar());
    const auto as = db.carregarAjustes();
    REQUIRE(as.size() == 1);
    CHECK(as[0].idTurma == "T1");
    CHECK(as[0].quando.toIso() == "2026-09-27T15:45");
    CHECK(as[0].horarioBruto == "15:45");
    CHECK(as[0].nota == "adiada em sala");
    CHECK(as[0].confirmada);
    CHECK(as[0].ativo);
    // O campo sem o qual o conflito nao e detectavel.
    CHECK(as[0].quandoSigaaNaEpoca.toIso() == "2026-09-20");
}

TEST_CASE("gravar o snapshot nao apaga a correcao do aluno", "[database]") {
    // ESTE e o teste que justifica a tabela separada. Um sync reescreve a
    // linha de `avaliacao` inteira; se a correcao morasse la, sumiria aqui —
    // em silencio, levando junto a data que o aluno digitou a mao.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());
    REQUIRE(db.gravarAjuste(ajusteDeTeste()));

    Snapshot s;
    Avaliacao av;
    av.idTurma = "T1";
    av.descricao = "Prova 1";
    av.turmaNome = "INTELIGENCIA ARTIFICIAL";
    av.quando.year = 2026; av.quando.month = 9; av.quando.day = 20;
    s.avaliacoes.push_back(av);
    REQUIRE(db.gravar(s, 1750000100));

    const auto as = db.carregarAjustes();
    REQUIRE(as.size() == 1);
    CHECK(as[0].quando.toIso() == "2026-09-27T15:45");
}

TEST_CASE("regravar o ajuste da mesma prova atualiza em vez de duplicar", "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());
    REQUIRE(db.gravarAjuste(ajusteDeTeste()));

    auto a = ajusteDeTeste();
    a.quando.day = 30;
    a.ativo = false;
    REQUIRE(db.gravarAjuste(a));

    const auto as = db.carregarAjustes();
    REQUIRE(as.size() == 1);
    CHECK(as[0].quando.toIso() == "2026-09-30T15:45");
    CHECK_FALSE(as[0].ativo);
}

TEST_CASE("desfazer remove a correcao mas nao o historico", "[database]") {
    // O historico e o que responde "por que esta data mudou tres vezes?".
    // Apaga-lo junto com a correcao deixaria a pergunta sem resposta — que e o
    // problema que este recurso existe para resolver.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());
    REQUIRE(db.gravarAjuste(ajusteDeTeste()));

    avaliacao::Mudanca m;
    m.idTurma = "T1";
    m.descricao = "Prova 1";
    m.turmaNome = "INTELIGENCIA ARTIFICIAL";
    m.tipo = avaliacao::TipoMudanca::AlunoCorrigiu;
    m.de = "2026-09-20";
    m.para = "2026-09-27";
    m.quando = 1750000000;
    REQUIRE(db.registrarMudanca(m));

    REQUIRE(db.removerAjuste("T1", "Prova 1"));
    CHECK(db.carregarAjustes().empty());
    CHECK(db.historico().size() == 1);
}

TEST_CASE("historico sai do mais recente para o mais antigo", "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    for (int i = 0; i < 3; ++i) {
        avaliacao::Mudanca m;
        m.idTurma = "T1";
        m.descricao = "Prova 1";
        m.tipo = avaliacao::TipoMudanca::AlunoCorrigiu;
        m.para = "2026-09-2" + std::to_string(i);
        m.quando = 1750000000 + i;
        REQUIRE(db.registrarMudanca(m));
    }

    const auto h = db.historico();
    REQUIRE(h.size() == 3);
    CHECK(h[0].quando == 1750000002);
    CHECK(h[2].quando == 1750000000);
}

TEST_CASE("historico filtra por prova", "[database]") {
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    for (const char* desc : {"Prova 1", "Prova 2"}) {
        avaliacao::Mudanca m;
        m.idTurma = "T1";
        m.descricao = desc;
        m.tipo = avaliacao::TipoMudanca::SigaaRemarcou;
        m.quando = 1750000000;
        REQUIRE(db.registrarMudanca(m));
    }

    CHECK(db.historico().size() == 2);
    const auto so = db.historico("T1", "Prova 2");
    REQUIRE(so.size() == 1);
    CHECK(so[0].descricao == "Prova 2");
}

TEST_CASE("o tipo da mudanca volta como texto, nao como numero", "[database]") {
    // O banco guarda o texto de proposito: um enum serializado por valor vira
    // lixo silencioso no dia em que alguem inserir um caso no meio da lista, e
    // o historico e justamente o que nao pode mentir.
    BancoTemp b;
    store::Database db(b.str());
    REQUIRE(db.migrar());

    avaliacao::Mudanca m;
    m.idTurma = "T1";
    m.descricao = "Prova 1";
    m.tipo = avaliacao::TipoMudanca::SigaaAtropelou;
    m.quando = 1750000000;
    REQUIRE(db.registrarMudanca(m));

    const auto h = db.historico();
    REQUIRE(h.size() == 1);
    CHECK(h[0].tipo == avaliacao::TipoMudanca::SigaaAtropelou);
}

TEST_CASE("banco antigo ganha as tabelas novas sem perder nada", "[database]") {
    // Quem ja usa o app tem um banco sem `ajuste_avaliacao`. A migracao e
    // aditiva (CREATE TABLE IF NOT EXISTS), entao abrir a versao nova sobre o
    // banco velho nao pode custar um dado sequer.
    BancoTemp b;
    {
        store::Database db(b.str());
        REQUIRE(db.migrar());
        Snapshot s;
        Turma t;
        t.idTurma = "T1";
        t.nome = "INTELIGENCIA ARTIFICIAL";
        s.turmas.push_back(t);
        REQUIRE(db.gravar(s, 1750000000));
    }
    store::Database db(b.str());
    REQUIRE(db.migrar());
    CHECK(db.carregarUltimo().turmas.size() == 1);
    CHECK(db.carregarAjustes().empty());
    CHECK(db.historico().empty());
    CHECK(db.gravarAjuste(ajusteDeTeste()));
}
