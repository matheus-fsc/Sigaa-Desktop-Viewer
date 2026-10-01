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

} // namespace sigaa::estudo
