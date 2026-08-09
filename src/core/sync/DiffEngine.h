#pragma once
// Compara o snapshot anterior com o novo e produz eventos.
//
// É daqui que saem as notificações. Duas decisões de projeto merecem destaque:
//
// 1. PRIMEIRA EXECUÇÃO NÃO É NOVIDADE. Sem isso, o primeiro sync dispararia
//    uma notificação para cada atividade e cada atualização já existentes —
//    dezenas de alertas inúteis que ensinam o usuário a ignorá-las.
//
// 2. COLETA VAZIA É SUSPEITA, NÃO "NADA MUDOU". Se antes havia turmas e agora
//    vieram zero, o cenário provável é o parser ter quebrado (o SIGAA mudou o
//    HTML), não o aluno ter se desmatriculado de tudo. Silenciar isso é o pior
//    modo de falha possível: o usuário confia que não há prazos e perde a
//    entrega. Por isso emitimos ColetaSuspeita.

#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::sync {

enum class TipoEvento {
    TurmaNova,
    AtividadeNova,
    PrazoMudou,
    AtividadeConcluida,
    AtualizacaoNova,
    AvaliacaoNova,
    AvaliacaoRemarcada, // prova mudou de data — dos avisos mais importantes
    ColetaSuspeita,     // a coleta parece incompleta — avisar, não silenciar
};

std::string_view toString(TipoEvento t);

struct Evento {
    TipoEvento tipo{};
    std::string chave;       // identificador estável, para dedupe
    std::string titulo;
    std::string detalhe;
    std::string idTurma;
    std::string turmaNome;
};

struct ResultadoDiff {
    std::vector<Evento> eventos;
    bool primeiraExecucao{false};
    bool suspeito{false};    // se true, NÃO trate "sem eventos" como "sem novidade"
};

ResultadoDiff diff(const Snapshot& anterior, const Snapshot& novo,
                   bool primeiraExecucao);

} // namespace sigaa::sync
