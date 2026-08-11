#pragma once
// Calendário: mescla as fontes de avaliação e exporta iCalendar (RFC 5545).
//
// O .ics é o maior retorno por linha de código do projeto: joga provas e
// prazos no Google Calendar / Outlook / calendário do celular, onde o aluno
// já olha todo dia — sem precisar abrir o app.

#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::calendario {

// Junta avaliações vindas do painel e dos tópicos, deduplicando por
// (idTurma, dia). Quando as duas fontes descrevem o mesmo dia, o painel vence:
// é dado estruturado que o professor cadastrou de propósito, enquanto o tópico
// é inferência por regex sobre um título livre.
std::vector<Avaliacao> mesclarAvaliacoes(std::vector<Avaliacao> todas);

// A aula do tópico `t` acontece no dia `d`?
//
// Mora em core/, e não na camada de UI que desenha a tela "Hoje", porque é
// regra de domínio: o professor registra um INTERVALO, quase sempre com início
// == fim, mas às vezes um bloco ("Semana 3 (10/08 - 14/08)"). Comparar só com
// o início faria a aula de quarta dentro de um bloco nunca aparecer no dia em
// que ela acontece — e ninguém notaria, porque a tela ficaria só um pouco mais
// vazia do que deveria.
bool aulaOcorreEm(const TopicoAula& t, const DateTime& d);

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
