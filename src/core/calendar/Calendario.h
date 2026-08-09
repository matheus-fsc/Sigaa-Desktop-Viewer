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
