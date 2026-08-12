#pragma once
// Calendário: mescla as fontes de avaliação e exporta iCalendar (RFC 5545).
//
// O .ics é o maior retorno por linha de código do projeto: joga provas e
// prazos no Google Calendar / Outlook / calendário do celular, onde o aluno
// já olha todo dia — sem precisar abrir o app.

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::calendario {

// Junta avaliações vindas do painel e dos tópicos, deduplicando por
// (idTurma, dia). Quando as duas fontes descrevem o mesmo dia, o painel vence:
// é dado estruturado que o professor cadastrou de propósito, enquanto o tópico
// é inferência por regex sobre um título livre.
std::vector<Avaliacao> mesclarAvaliacoes(std::vector<Avaliacao> todas);

// Um bloco do código de horário do SIGAA: "24M23" = segunda e quarta, 2º e 3º
// horários da manhã. Um código pode ter vários blocos ("2M12 4T34").
//
// POR QUE O TURNO E OS HORÁRIOS IMPORTAM, e não só os dias: é o que ordena as
// aulas dentro do dia. Sem isso a agenda lista a aula da noite antes da aula da
// manhã sempre que o crawler devolver as turmas nessa ordem — e uma agenda fora
// de ordem cronológica não é agenda.
//
// O que este módulo NÃO faz: converter horário em hora do relógio. "T34" é
// 15h30 na UNIFEI e pode ser outra coisa na universidade do lado; inventar
// "15:30" aqui seria afirmar o que não sabemos. O código cru vai para a tela,
// que é o mesmo que o aluno lê no SIGAA.
struct BlocoHorario {
    std::set<int> dias;        // ISO: 1 = segunda ... 7 = domingo
    char turno{'M'};           // 'M', 'T' ou 'N'
    std::vector<int> horarios; // 1..6, na ordem em que vieram

    // Chave de ordenação dentro do dia: manhã < tarde < noite e, dentro do
    // turno, o primeiro horário.
    int ordem() const;

    // "M23", "T34" — o pedaço do código que se refere ao turno, sem os dias.
    std::string codigo() const;
};

// Vazio quando não dá para ler — e "vazio" significa "não sei", nunca "não tem
// aula". Quem chama trata os dois casos de forma diferente.
std::vector<BlocoHorario> lerHorario(std::string_view horario);

// Só os dias, de todos os blocos. Atalho para quem não precisa do turno.
std::set<int> diasDeAula(std::string_view horario);

// A aula do tópico `t` acontece no dia `d`?
//
// Mora em core/, e não na camada de UI que desenha a agenda, porque é regra de
// domínio: o professor registra um INTERVALO, quase sempre com início == fim,
// mas às vezes um bloco ("Semana 3 (10/08 - 14/08)"). Comparar só com o início
// faria a aula de quarta dentro de um bloco nunca aparecer no dia em que ela
// acontece — e ninguém notaria, porque a tela ficaria só um pouco mais vazia
// do que deveria.
//
// `horarioTurma` é o código de horário da turma dona do tópico, e é o que
// impede o erro simétrico: um bloco de três semanas ("Desenvolvimento Móvel,
// 07/08 a 28/08") cobrindo TODOS os dias faria a mesma aula aparecer no sábado,
// no domingo e em toda quarta-feira de uma turma que só encontra às sextas. O
// intervalo diz em que período a unidade corre; quem diz em que dias há aula é
// a grade horária. Vazio ou ilegível = todos os dias do bloco, que é o
// comportamento antigo: sem a grade, alargar é menos errado do que esconder.
//
// Se a grade existe mas nenhum dia de aula cai dentro do bloco, o tópico
// aparece no dia de INÍCIO. Sumir seria pior: é uma aula registrada.
bool aulaOcorreEm(const TopicoAula& t, const DateTime& d,
                  std::string_view horarioTurma = {});

// As aulas de um dia, na ordem em que estão no snapshot.
std::vector<TopicoAula> aulasDoDia(const std::vector<TopicoAula>& topicos,
                                   const DateTime& d);

struct OpcoesIcs {
    // Lembrete antes do evento, em minutos. 0 desliga.
    int lembreteMinutos{24 * 60};
    std::string prefixoUid{"sigaa-viewer"};
};

// Gera um .ics com as atividades (prazos) e as avaliações (provas).
std::string gerarIcs(const Snapshot& s, const OpcoesIcs& op = {});

// --- expostos para teste ---------------------------------------------------
std::string escaparTexto(std::string_view s);
std::string dobrarLinha(std::string_view linha);

} // namespace sigaa::calendario
