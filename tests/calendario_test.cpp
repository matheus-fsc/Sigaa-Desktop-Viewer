// Testes do TurmaParser e do gerador de iCalendar.
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <set>

#include "core/calendar/Calendario.h"
#include "core/parse/TurmaParser.h"

using namespace sigaa;
using namespace sigaa::parse;
using namespace sigaa::calendario;

// --- TurmaParser -----------------------------------------------------------

TEST_CASE("titulo de topico traz nome e intervalo de datas", "[turma]") {
    std::string nome;
    DateTime i, f;
    REQUIRE(parseTituloTopico("Primeira avaliacao (29/09/2026 - 29/09/2026)", nome, i, f));
    CHECK(nome == "Primeira avaliacao");
    CHECK(i.year == 2026);
    CHECK(i.month == 9);
    CHECK(i.day == 29);
    CHECK(f.day == 29);
}

TEST_CASE("titulo sem datas ainda devolve o nome", "[turma]") {
    std::string nome;
    DateTime i, f;
    CHECK_FALSE(parseTituloTopico("Materiais auxiliares", nome, i, f));
    CHECK(nome == "Materiais auxiliares");
}

TEST_CASE("heuristica de avaliacao cobre as formas usadas pelos professores",
          "[turma]") {
    CHECK(pareceAvaliacao("Primeira avaliacao"));
    CHECK(pareceAvaliacao("1a Avaliacao"));
    CHECK(pareceAvaliacao("Avaliacao substitutiva"));
    CHECK(pareceAvaliacao("Prova 2"));
    CHECK(pareceAvaliacao("Exame final"));
    CHECK(pareceAvaliacao("Seminario de encerramento"));
    // negativos: nao pode marcar aula normal como prova
    CHECK_FALSE(pareceAvaliacao("Equacoes separaveis"));
    CHECK_FALSE(pareceAvaliacao("Aula de duvidas"));
    CHECK_FALSE(pareceAvaliacao("Transformada de Laplace"));
}

TEST_CASE("falsos positivos vistos em producao sao rejeitados", "[turma]") {
    // Inventar prova e pior que perder: o aluno estuda para o dia errado.
    // Todos estes apareceram no calendario real antes da lista de excecao.
    CHECK_FALSE(pareceAvaliacao("Revisao pre-prova"));
    CHECK_FALSE(pareceAvaliacao("Revisão pré-prova"));
    CHECK_FALSE(pareceAvaliacao(
        "Auto-avaliacao - competencias gerenciais e Apresentacao do Plano de Ensino"));
    CHECK_FALSE(pareceAvaliacao("Vista de prova"));
    CHECK_FALSE(pareceAvaliacao("Correcao da prova"));
    // e os verdadeiros continuam passando
    CHECK(pareceAvaliacao("Primeira avaliacao"));
    CHECK(pareceAvaliacao("Prova N1"));
}

TEST_CASE("painel de avaliacao aceita os dois formatos de horario", "[turma]") {
    // O formato varia por turma (docs/RECON.md §1.6).
    DateTime q;
    std::string h;

    REQUIRE(parseDataPainel("29/09 15:45", 2026, q, h));
    CHECK(q.day == 29);
    CHECK(q.month == 9);
    CHECK(q.year == 2026);
    CHECK(q.hasTime);
    CHECK(q.hour == 15);
    CHECK(h == "15:45");

    REQUIRE(parseDataPainel("04/09 6T34", 2026, q, h));
    CHECK(q.day == 4);
    CHECK_FALSE(q.hasTime);      // codigo SIGAA nao vira hora sem a tabela de turnos
    CHECK(h == "6T34");
}

TEST_CASE("ano vem do periodo letivo, nao do painel", "[turma]") {
    CHECK(anoDoPeriodo("2026.2") == 2026);
    CHECK(anoDoPeriodo("lixo") == 0);
}

TEST_CASE("turma sem topicos e detectada e nao e erro", "[turma]") {
    html::Document doc;
    REQUIRE(doc.parse(R"(<html><body><div class="intro-aval">
        <h4>Turma Virtual!</h4></div></body></html>)"));
    const auto c = parseTurmaVirtual(doc, "88094", "ADM", "2026.2");
    CHECK(c.semTopicos);
    CHECK(c.topicos.empty());
}

TEST_CASE("painel Avaliacoes e lido da barra lateral", "[turma]") {
    html::Document doc;
    REQUIRE(doc.parse(R"(<html><body>
      <div class="rich-stglpanel">
        <div class="rich-stglpanel-header">Avaliacoes</div>
        <div class="rich-stglpanel-body"><ul class="menu-direita">
          <li><span class="data">29/09 15:45</span><span class="descricao">1a Avaliacao</span></li>
          <li><span class="data">03/12 15:45</span><span class="descricao">Substitutiva</span></li>
        </ul></div>
      </div></body></html>)"));

    const auto c = parseTurmaVirtual(doc, "88080", "EDO", "2026.2");
    REQUIRE(c.avaliacoes.size() == 2);
    CHECK(c.avaliacoes[0].descricao == "1a Avaliacao");
    CHECK(c.avaliacoes[0].fonte == FonteAvaliacao::PainelAvaliacoes);
    CHECK(c.avaliacoes[1].quando.month == 12);
}

// --- mesclagem -------------------------------------------------------------

TEST_CASE("painel vence topico quando descrevem o mesmo dia", "[calendario]") {
    // As duas fontes se sobrepoem de forma imprevisivel (docs/RECON.md §1.6).
    Avaliacao doTopico;
    doTopico.idTurma = "88080";
    doTopico.descricao = "Primeira avaliacao";
    doTopico.quando = DateTime{2026, 9, 29, 0, 0, false};
    doTopico.fonte = FonteAvaliacao::TopicoAula;

    Avaliacao doPainel;
    doPainel.idTurma = "88080";
    doPainel.descricao = "1a Avaliacao";
    doPainel.quando = DateTime{2026, 9, 29, 15, 45, true};
    doPainel.fonte = FonteAvaliacao::PainelAvaliacoes;

    const auto m = mesclarAvaliacoes({doTopico, doPainel});
    REQUIRE(m.size() == 1);
    CHECK(m[0].fonte == FonteAvaliacao::PainelAvaliacoes);
    CHECK(m[0].quando.hasTime);
}

TEST_CASE("apos mesclar, a chave do banco fica unica", "[calendario]") {
    // Regressao: o Crawler precisa mesclar ANTES de persistir. O banco chaveia
    // avaliacao por (id_turma, descricao); painel e topico trazem a MESMA
    // prova com a mesma descricao, uma com hora e outra sem. Persistindo os
    // dois, eles se sobrescrevem em ordem nao deterministica e todo ciclo
    // acusa "prova remarcada" comparando 15:45 com sem-hora.
    Avaliacao doPainel;
    doPainel.idTurma = "88080";
    doPainel.descricao = "Avaliacao substitutiva";
    doPainel.quando = DateTime{2026, 12, 3, 15, 45, true};
    doPainel.fonte = FonteAvaliacao::PainelAvaliacoes;

    Avaliacao doTopico = doPainel;
    doTopico.quando = DateTime{2026, 12, 3, 0, 0, false};
    doTopico.fonte = FonteAvaliacao::TopicoAula;

    const auto m = mesclarAvaliacoes({doTopico, doPainel});

    std::set<std::string> chaves;
    for (const auto& a : m) {
        CHECK(chaves.insert(a.idTurma + "|" + a.descricao).second);
    }
    REQUIRE(m.size() == 1);
    CHECK(m[0].quando.hasTime);      // a versao do painel sobreviveu
}

TEST_CASE("mesmo dia em turmas diferentes nao deduplica", "[calendario]") {
    Avaliacao a, b;
    a.idTurma = "1";
    a.quando = DateTime{2026, 9, 29, 0, 0, false};
    b.idTurma = "2";
    b.quando = DateTime{2026, 9, 29, 0, 0, false};
    CHECK(mesclarAvaliacoes({a, b}).size() == 2);
}

// --- iCalendar -------------------------------------------------------------

TEST_CASE("escape segue a RFC 5545", "[calendario]") {
    CHECK(escaparTexto("a,b") == "a\\,b");
    CHECK(escaparTexto("a;b") == "a\\;b");
    CHECK(escaparTexto("a\\b") == "a\\\\b");
    CHECK(escaparTexto("a\nb") == "a\\nb");
}

TEST_CASE("dobra de linha nao corta caractere UTF-8 no meio", "[calendario]") {
    // Cortar no meio de um multibyte faz o cliente de calendario mostrar lixo.
    std::string s = "SUMMARY:";
    for (int i = 0; i < 40; ++i) s += "\xC3\xA7";   // 'c cedilha', 2 bytes
    const std::string dobrado = dobrarLinha(s);

    for (size_t i = 0; i < dobrado.size(); ++i) {
        if (dobrado[i] == '\r') {
            // o byte anterior nao pode ser inicio de sequencia multibyte
            REQUIRE(i > 0);
            CHECK((static_cast<unsigned char>(dobrado[i - 1]) & 0xC0) != 0xC0);
        }
    }
}

TEST_CASE("ics tem estrutura valida e omite atividade concluida", "[calendario]") {
    Snapshot s;

    Atividade pendente;
    pendente.atividadeId = "10";
    pendente.idTurma = "1";
    pendente.turmaNome = "ADM";
    pendente.titulo = "Tarefa 1";
    pendente.prazo = DateTime{2026, 8, 11, 23, 59, true};
    pendente.status = StatusAtividade::Pendente;

    Atividade feita = pendente;
    feita.atividadeId = "11";
    feita.titulo = "Tarefa velha";
    feita.status = StatusAtividade::Concluida;

    s.atividades = {pendente, feita};

    const std::string ics = gerarIcs(s);
    CHECK(ics.rfind("BEGIN:VCALENDAR", 0) == 0);
    CHECK(ics.find("END:VCALENDAR") != std::string::npos);
    CHECK(ics.find("DTSTART:20260811T235900") != std::string::npos);
    CHECK(ics.find("Tarefa 1") != std::string::npos);
    // concluida nao polui o calendario
    CHECK(ics.find("Tarefa velha") == std::string::npos);
    // linhas terminam em CRLF
    CHECK(ics.find("\r\n") != std::string::npos);
}

TEST_CASE("avaliacao inferida avisa que e heuristica", "[calendario]") {
    Snapshot s;
    Avaliacao a;
    a.idTurma = "1";
    a.turmaNome = "EDO";
    a.descricao = "Primeira avaliacao";
    a.quando = DateTime{2026, 9, 29, 0, 0, false};
    a.fonte = FonteAvaliacao::TopicoAula;
    s.avaliacoes = {a};

    const std::string ics = gerarIcs(s);
    CHECK(ics.find("DTSTART;VALUE=DATE:20260929") != std::string::npos);
    CHECK(ics.find("inferido") != std::string::npos);
}

// ---------------------------------------------------------------------------
// "Esta aula acontece hoje?" — a regra por tras da tela inicial.
//
// Vive em core/ e nao na UI justamente para poder ser testada headless: quando
// erra, a tela nao quebra, so fica mais vazia do que deveria — e ninguem nota.
// ---------------------------------------------------------------------------

namespace {

TopicoAula aula(const std::string& titulo, int diaInicio, int diaFim = 0) {
    TopicoAula t;
    t.idTurma = "88094";
    t.titulo = titulo;
    t.inicio = DateTime{2026, 8, diaInicio, 0, 0, false};
    if (diaFim > 0) t.fim = DateTime{2026, 8, diaFim, 0, 0, false};
    return t;
}

DateTime dia(int d) { return DateTime{2026, 8, d, 0, 0, false}; }

} // namespace

TEST_CASE("aula de um dia so aparece no proprio dia", "[calendario]") {
    const auto t = aula("Apresentacao", 12, 12);
    CHECK(calendario::aulaOcorreEm(t, dia(12)));
    CHECK_FALSE(calendario::aulaOcorreEm(t, dia(11)));
    CHECK_FALSE(calendario::aulaOcorreEm(t, dia(13)));
}

TEST_CASE("topico que cobre um bloco vale para todos os dias do bloco",
          "[calendario]") {
    // "Semana 3 (10/08 - 14/08)". Comparar so com o inicio faria a aula de
    // quarta sumir do dia em que ela acontece.
    const auto t = aula("Semana 3", 10, 14);
    CHECK(calendario::aulaOcorreEm(t, dia(10)));
    CHECK(calendario::aulaOcorreEm(t, dia(12)));
    CHECK(calendario::aulaOcorreEm(t, dia(14)));
    CHECK_FALSE(calendario::aulaOcorreEm(t, dia(9)));
    CHECK_FALSE(calendario::aulaOcorreEm(t, dia(15)));
}

TEST_CASE("fim ausente ou quebrado nao alarga o intervalo", "[calendario]") {
    // Alargar por causa de um `fim` invalido faria a aula aparecer em dias em
    // que ela nao acontece — e o aluno levaria isso a serio.
    const auto semFim = aula("Sem fim", 12);
    CHECK(calendario::aulaOcorreEm(semFim, dia(12)));
    CHECK_FALSE(calendario::aulaOcorreEm(semFim, dia(13)));

    auto invertido = aula("Invertido", 12);
    invertido.fim = DateTime{2026, 8, 5, 0, 0, false};   // fim antes do inicio
    CHECK(calendario::aulaOcorreEm(invertido, dia(12)));
    CHECK_FALSE(calendario::aulaOcorreEm(invertido, dia(5)));
}

TEST_CASE("topico sem data nunca cai num dia", "[calendario]") {
    // Professor que nao datou o topico: ele existe na turma, mas nao pode
    // aparecer como "aula de hoje" — inventar um dia e pior que omitir.
    TopicoAula t;
    t.titulo = "Sem data";
    CHECK_FALSE(calendario::aulaOcorreEm(t, dia(12)));
    CHECK_FALSE(calendario::aulaOcorreEm(aula("x", 12), DateTime{}));
}

TEST_CASE("aulasDoDia filtra e preserva a ordem", "[calendario]") {
    const std::vector<TopicoAula> todos = {aula("Primeira", 12, 12),
                                           aula("Outra", 13, 13),
                                           aula("Segunda", 12, 12)};
    const auto hoje = calendario::aulasDoDia(todos, dia(12));
    REQUIRE(hoje.size() == 2);
    CHECK(hoje[0].titulo == "Primeira");
    CHECK(hoje[1].titulo == "Segunda");
}
