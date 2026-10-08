#pragma once
// O que um agente de IA devolve ao app pelo MCP (docs/MCP.md §5): tempo de
// estudo, desempenho em exercícios e os pontos em que o aluno tem dificuldade.
//
// Dado do ALUNO, à parte do snapshot como os ajustes de prova: nenhum sync
// toca nestas tabelas. Toda linha diz QUEM escreveu (`origem`, o nome que o
// agente deu no initialize) e QUANDO, e o aluno apaga qualquer uma na UI.

#include <cstdint>
#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::estudo {

struct RegistroEstudo {
    std::int64_t id{0};
    std::string origem;
    std::int64_t criadoEm{0};
    std::string idTurma;
    std::string prova;                  // opcional: "Prova 2"
    DateTime quando;                    // o dia em que estudou
    int minutos{0};
    std::vector<std::string> topicos;
    std::string observacao;
};

struct Desempenho {
    std::int64_t id{0};
    std::string origem;
    std::int64_t criadoEm{0};
    std::string idTurma;
    std::string topico;
    std::string tipo;                   // simulado | exercicio | revisao
    int acertos{0};
    int total{0};
    DateTime quando;
};

struct PontoFoco {
    std::int64_t id{0};
    std::string origem;
    std::int64_t criadoEm{0};
    std::int64_t atualizadoEm{0};
    std::string idTurma;
    std::string topico;
    int nivel{1};                       // 1 atenção, 2 dificuldade, 3 crítico
    std::string motivo;
    bool resolvido{false};
};

// Uma mudança no plano que o agente quer fazer: mais horas num dia, outra
// dificuldade para uma matéria, uma sessão extra, um ponto de foco. O aluno
// escolhe, por tipo, se o agente não pode, propõe (e o aluno aceita, ajusta
// ou recusa) ou aplica e avisa (e o aluno pode desfazer) — mcp/Propostas.h.
//
// A proposta fica guardada depois de respondida: é o histórico do que mudou
// no plano e por quê, e é o que deixa desfazer.
enum class TipoProposta { Horas, Dificuldade, Sessao, Foco };
enum class EstadoProposta { Pendente, Aceita, Aplicada, Recusada, Desfeita };

const char* nomeTipo(TipoProposta t);        // "horas", "dificuldade"...
const char* nomeEstado(EstadoProposta e);    // "pendente", "aceita"...
TipoProposta tipoPorNome(const std::string& s);
EstadoProposta estadoPorNome(const std::string& s);

struct Proposta {
    std::int64_t id{0};
    std::string origem;
    std::int64_t criadoEm{0};
    std::int64_t respondidaEm{0};
    TipoProposta tipo{TipoProposta::Horas};
    EstadoProposta estado{EstadoProposta::Pendente};
    std::string idTurma;     // dificuldade, sessão, foco
    int diaSemana{-1};       // horas: 0 = segunda
    DateTime dia;            // sessão: o dia dela
    std::string topico;      // sessão, foco
    // O valor antes e o proposto. Horas: minutos disponíveis no dia;
    // dificuldade: 1..3; sessão: minutos (de = 0); foco: nível 1..3 (de = 0).
    int de{0};
    int para{0};
    std::string motivo;
    std::int64_t ref{0};     // foco: o ponto criado ao aceitar, para desfazer

    // Valendo no plano: aceita pelo aluno ou aplicada pelo agente.
    bool valendo() const {
        return estado == EstadoProposta::Aceita || estado == EstadoProposta::Aplicada;
    }
};

} // namespace sigaa::estudo
