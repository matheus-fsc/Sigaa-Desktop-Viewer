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

// Dias da semana em que a turma tem aula, lidos do código de horário do SIGAA:
// "24M23" = segunda e quarta, 2º e 3º horários da manhã; "6M2345" = só sexta.
// Vários blocos separados por espaço também valem ("2M12 4T34").
//
// Devolve no padrão ISO — 1 = segunda ... 7 = domingo — e VAZIO quando não dá
// para ler o código. Vazio significa "não sei", nunca "não tem aula": quem
// chama trata os dois casos de forma diferente.
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
