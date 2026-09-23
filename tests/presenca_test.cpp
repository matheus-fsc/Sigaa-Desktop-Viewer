// Presenca que o ALUNO registra nos dias que o professor deixou em branco.
//
// O QUE ESTE ARQUIVO PROTEGE, e por que quase todo teste fala de conflito: a
// marcacao do aluno e prova pessoal para uma conversa futura — com o professor,
// com a secretaria. Ela so vale se o app souber distinguir "o professor ainda
// nao lancou" de "o professor lancou e discordou de mim". As duas tem a mesma
// aparencia se olharmos so para "a marcacao difere do SIGAA".
//
// E a regra que NAO pode ser quebrada: marcar presenca aqui nunca muda a
// contagem de faltas do SIGAA. Ver o cabecalho de core/frequencia/Presenca.h.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/frequencia/Presenca.h"

using namespace sigaa;
using namespace sigaa::frequencia;

namespace {

DateTime dia(int d) {
    DateTime t;
    t.year = 2026; t.month = 8; t.day = d;
    return t;
}

DiaFrequencia doSigaa(int d, SituacaoDia s, int faltas = 0) {
    DiaFrequencia x;
    x.data = dia(d);
    x.situacao = s;
    x.faltas = faltas;
    return x;
}

Frequencia mapa(std::vector<DiaFrequencia> dias) {
    Frequencia f;
    f.idTurma = "T1";
    f.turmaNome = "COMPILADORES";
    f.aulasPelaCH = 64;
    f.aulasComRegistro = 24;
    f.presencas = 18;
    f.temDados = true;
    f.dias = std::move(dias);
    return f;
}

Marcacao marcou(int d, SituacaoDia s = SituacaoDia::Presente, int faltas = 0) {
    Marcacao m;
    m.idTurma = "T1";
    m.data = dia(d);
    m.situacao = s;
    m.faltas = faltas;
    m.situacaoSigaaNaEpoca = SituacaoDia::NaoRegistrada;
    return m;
}

}  // namespace

TEST_CASE("dia nao registrado aparece como nao registrado", "[presenca]") {
    // O estado de partida: o professor nao abriu o diario, e o app nao inventa
    // presenca nem falta por conta propria.
    const auto v = efetivos(mapa({doSigaa(17, SituacaoDia::NaoRegistrada)}), {});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == EstadoDia::NaoRegistrada);
    CHECK(v[0].situacao == SituacaoDia::NaoRegistrada);
}

TEST_CASE("o aluno marca presenca no dia em branco", "[presenca]") {
    const auto v = efetivos(mapa({doSigaa(17, SituacaoDia::NaoRegistrada)}),
                            {marcou(17)});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == EstadoDia::MarcadaPeloAluno);
    CHECK(v[0].situacao == SituacaoDia::Presente);
    // O que o SIGAA diz continua a vista: a marcacao tem de poder ser
    // conferida, nao so acreditada.
    CHECK(v[0].situacaoSigaa == SituacaoDia::NaoRegistrada);
}

TEST_CASE("o aluno pode registrar a propria falta", "[presenca]") {
    // O caso mais honesto: ele sabe que faltou e quer o controle antes de o
    // professor lancar. Recusar isso faria a aba servir so para melhorar
    // numero, que e o oposto do proposito.
    const auto v = efetivos(mapa({doSigaa(17, SituacaoDia::NaoRegistrada)}),
                            {marcou(17, SituacaoDia::Falta, 2)});
    CHECK(v[0].situacao == SituacaoDia::Falta);
    CHECK(v[0].faltas == 2);
}

TEST_CASE("marcacao nao mexe no que o professor ja registrou", "[presenca]") {
    // Um dia lancado e dado do professor. Se a marcacao pudesse sobrescreve-lo,
    // a aba viraria uma folha de rascunho que mente sobre o diario oficial.
    const auto v = efetivos(mapa({doSigaa(17, SituacaoDia::Falta, 2)}),
                            {marcou(17, SituacaoDia::Presente)});
    REQUIRE(v.size() == 1);
    CHECK(v[0].situacao == SituacaoDia::Falta);
    CHECK(v[0].faltas == 2);
    // Mas o conflito fica visivel — e justamente aqui que a marcacao do aluno
    // pode ser necessaria.
    CHECK(v[0].estado == EstadoDia::Conflitada);
}

TEST_CASE("o professor lancar igual ao aluno nao e conflito", "[presenca]") {
    // Os dois concordam. Alarmar seria o alarme falso que ensina a ignorar os
    // verdadeiros.
    auto r = reconciliar(mapa({doSigaa(17, SituacaoDia::Presente)}), {marcou(17)});
    CHECK(r.conflitos.empty());
    // A marcacao se aposenta: cumpriu o papel.
    CHECK(r.mudou);
    REQUIRE(r.marcacoes.size() == 1);
    CHECK_FALSE(r.marcacoes[0].ativo);
}

TEST_CASE("o professor lancar falta onde o aluno marcou presenca e conflito",
          "[presenca]") {
    // O caso que justifica guardar a marcacao: e a evidencia do aluno para a
    // conversa que vem.
    auto r = reconciliar(mapa({doSigaa(17, SituacaoDia::Falta, 2)}), {marcou(17)});
    REQUIRE(r.conflitos.size() == 1);
    CHECK(r.conflitos[0].doAluno == SituacaoDia::Presente);
    CHECK(r.conflitos[0].doSigaa == SituacaoDia::Falta);
    CHECK(r.conflitos[0].faltasDoSigaa == 2);

    // Aposentada, nao apagada.
    REQUIRE(r.marcacoes.size() == 1);
    CHECK_FALSE(r.marcacoes[0].ativo);
    CHECK(r.marcacoes[0].situacao == SituacaoDia::Presente);
}

TEST_CASE("numero de faltas diferente tambem e conflito", "[presenca]") {
    // O professor lancou 1 onde o aluno registrou 2. Tratar como acordo
    // esconderia uma divergencia que muda a conta do semestre.
    auto r = reconciliar(mapa({doSigaa(17, SituacaoDia::Falta, 1)}),
                         {marcou(17, SituacaoDia::Falta, 2)});
    REQUIRE(r.conflitos.size() == 1);
    CHECK(r.conflitos[0].faltasDoAluno == 2);
    CHECK(r.conflitos[0].faltasDoSigaa == 1);
}

TEST_CASE("enquanto o professor nao lanca, nenhum ciclo aposenta a marcacao",
          "[presenca]") {
    // O caso comum: o professor nunca abre o diario. A marcacao tem de
    // sobreviver a sincronizacoes indefinidas.
    auto marcacoes = std::vector<Marcacao>{marcou(17)};
    const auto f = mapa({doSigaa(17, SituacaoDia::NaoRegistrada)});
    for (int i = 0; i < 10; ++i) {
        auto r = reconciliar(f, marcacoes);
        CHECK(r.conflitos.empty());
        CHECK_FALSE(r.mudou);
        marcacoes = r.marcacoes;
    }
    CHECK(marcacoes[0].ativo);
    CHECK(efetivos(f, marcacoes)[0].estado == EstadoDia::MarcadaPeloAluno);
}

TEST_CASE("mapa sem dados nao aposenta marcacao nenhuma", "[presenca]") {
    // Um sync que nao entrou na turma traz mapa vazio. Aposentar tudo aqui
    // apagaria em silencio o registro que o aluno guardou.
    Frequencia vazio;
    vazio.idTurma = "T1";
    vazio.temDados = false;
    auto r = reconciliar(vazio, {marcou(17)});
    CHECK_FALSE(r.mudou);
    CHECK(r.marcacoes[0].ativo);
}

TEST_CASE("dia que o SIGAA nem lista nao some", "[presenca]") {
    // Houve aula e o professor nunca lancou linha nenhuma para ela — o caso
    // mais desamparado de todos, e o que mais precisa do registro do aluno.
    const auto v = efetivos(mapa({doSigaa(3, SituacaoDia::Presente)}), {marcou(17)});
    REQUIRE(v.size() == 2);
    CHECK(v[1].data.toIso() == "2026-08-17");
    CHECK(v[1].estado == EstadoDia::MarcadaPeloAluno);
}

TEST_CASE("os dias saem em ordem cronologica", "[presenca]") {
    const auto v = efetivos(
        mapa({doSigaa(24, SituacaoDia::Presente), doSigaa(3, SituacaoDia::Presente)}),
        {marcou(17)});
    REQUIRE(v.size() == 3);
    CHECK(v[0].data.day == 3);
    CHECK(v[1].data.day == 17);
    CHECK(v[2].data.day == 24);
}

TEST_CASE("marcacao aposentada nao afeta a lista", "[presenca]") {
    auto m = marcou(17);
    m.ativo = false;
    const auto v = efetivos(mapa({doSigaa(17, SituacaoDia::Falta, 2)}), {m});
    REQUIRE(v.size() == 1);
    CHECK(v[0].estado == EstadoDia::DoSigaa);
    CHECK(v[0].situacao == SituacaoDia::Falta);
}

TEST_CASE("a contagem oficial de faltas nao muda com marcacao", "[presenca]") {
    // A regra que nao pode ser quebrada. Se marcar presenca mexesse no total,
    // o app viraria uma calculadora de mentira que o aluno consulta em vez do
    // diario oficial — e ele descobriria a verdade na secretaria.
    const auto f = mapa({doSigaa(17, SituacaoDia::NaoRegistrada)});
    const int antes = f.faltas();
    (void)efetivos(f, {marcou(17, SituacaoDia::Falta, 2)});
    CHECK(f.faltas() == antes);
    CHECK(f.limiteFaltas() == 16);
}
