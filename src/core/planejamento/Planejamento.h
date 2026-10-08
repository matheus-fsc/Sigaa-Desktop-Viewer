#pragma once
// Planejamento de estudo: distribui as horas livres do aluno entre as provas
// que vêm, e diz onde o semestre aperta.
//
// POR QUE NO CORE, E NÃO NA INTERFACE: o plano é dado, não desenho. A janela
// de Planejamento o mostra, a Agenda mostra as sessões do dia, e um agente de
// IA (via MCP, no futuro) vai querer ler a carga e gravar sessões. Os três
// precisam da MESMA conta — e ela tem que ser testável sem Qt.
//
// O QUE O PLANO É: sessões de estudo por dia, cada uma presa a uma prova. As
// sessões feitas ficam guardadas para sempre (são o histórico do aluno); as
// pendentes são recalculadas a cada `planejar`, porque dependem de datas que
// mudam — uma prova confirmada, uma entrega nova, horas livres alteradas.
//
// COMO DISTRIBUI (`planejar`), prova a prova, da mais próxima à mais distante:
//
//   1. Quanto estudar: `minutosNecessarios` — uma base, mais um tanto por
//      tópico da matéria (os tópicos entre esta prova e a anterior), vezes o
//      peso da dificuldade. Menos o que já foi feito.
//   2. Onde: os dias das 3 semanas antes da prova, cada um com um peso que
//      cresce perto dela (véspera > semana da prova > duas semanas antes).
//      Revisão espaçada: a partir da segunda hora da mesma prova no mesmo dia
//      o peso cai, então o plano espalha sessões de 1h em vez de empilhar.
//   3. Quanto cabe: o tempo disponível do dia menos as aulas da grade, menos
//      o que outras provas já pegaram. Dia com OUTRA prova vale metade; dia de entrega, 3/4.
//
//   Bloco a bloco (30 min), o próximo vai para o dia de maior peso que ainda
//   tem espaço. O que não cabe vira déficit — e uma dica dizendo quanto falta.
//
// Nada aqui é aleatório: as mesmas entradas dão o mesmo plano. É isso que
// deixa recalcular a cada abertura sem o plano "pular" na frente do aluno.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/estudo/Registros.h"
#include "core/model/Models.h"

namespace sigaa::planejamento {

enum class Dificuldade : int { Facil = 1, Media = 2, Dificil = 3 };

struct Preferencias {
    // Minutos que o aluno tem para a faculdade em cada dia da semana — aulas
    // E estudo —, [0] = segunda. O plano desconta as aulas da grade
    // (`minutosDeAula`) e estuda no que sobra: assim um dia com cinco horas
    // de aula não recebe o mesmo estudo que um dia sem nenhuma.
    // Padrão: 9h nos dias úteis (um dia de 6 horários, 5h30 de aula, deixa
    // 3h30 de estudo), 5h no sábado e 3h no domingo.
    std::array<int, 7> minutosPorDia{540, 540, 540, 540, 540, 300, 180};
    // Por idTurma. Turma ausente = Media.
    std::map<std::string, Dificuldade> dificuldade;

    Dificuldade dificuldadeDe(const std::string& idTurma) const;
};

// Uma prova que o plano mira.
struct ProvaAlvo {
    std::string idTurma;
    std::string turmaNome;
    std::string descricao;   // "2ª Avaliação"
    DateTime data;           // só a data importa
    bool inferida{false};    // data deduzida, não confirmada
    int topicos{-1};         // tópicos da matéria; -1 = não coletados
};

// Uma entrega pendente: não recebe sessões, mas tira tempo do dia.
struct EntregaAlvo {
    std::string idTurma;
    std::string turmaNome;
    std::string titulo;
    DateTime prazo;
};

struct Sessao {
    std::string idTurma;
    std::string turmaNome;
    std::string prova;       // descrição da prova
    DateTime dataProva;
    DateTime dia;
    int minutos{0};
    bool feita{false};

    // "idTurma|prova|aaaa-mm-dd" — uma sessão por prova por dia. Estável:
    // é como o check do aluno sobrevive a um replanejamento.
    std::string chave() const;
    // "idTurma|prova": a prova a que a sessão pertence.
    std::string chaveProva() const;
};

// Uma semana do mapa de pressão.
struct SemanaPlano {
    DateTime inicio;          // segunda-feira
    int minutosPlanejados{0}; // sessões (feitas e pendentes)
    int minutosLivres{0};     // capacidade, já descontados dias de prova/entrega
    int provas{0};
    int entregas{0};

    // Fração da capacidade já tomada pelo plano: 0 = folgada, 1 = cheia.
    double ocupacao() const;
    // Zona de maior carga: plano tomando 80% do tempo livre, ou 3+ provas.
    bool critica() const;
};

enum class TipoDica {
    Deficit,           // a prova não cabe no tempo livre antes dela
    Aglomerado,        // 3+ provas em 7 dias
    MesmoDia,          // 2+ provas no mesmo dia
    Deduzida,          // o plano mira uma data não confirmada
    SemanaLeve,        // semana folgada antes de uma crítica
    Atrasadas,         // sessões passadas que não foram feitas
    EntregaNaVespera,  // entrega na véspera ou no dia de uma prova
    Estrategia,        // como estudar numa semana crítica
    Foco,              // ponto de dificuldade marcado por um agente de IA
};

struct Dica {
    TipoDica tipo;
    int prioridade{0};   // maior = mais acima
    std::string texto;
};

// O que os agentes de IA registraram pelo MCP (docs/MCP.md §5), para o plano
// levar em conta:
//   - o estudo registrado conta como feito para a prova citada nele (ou, sem
//     prova, para a próxima da turma depois do dia em que estudou);
//   - cada ponto de foco ABERTO numa turma acrescenta 30 min × nível à prova
//     mais próxima dela, e vira dica quando essa prova está a até 21 dias;
//   - cada sessão extra valendo (aceita ou aplicada, de hoje em diante) soma
//     os minutos dela à próxima prova da turma depois do dia, e os reserva
//     naquele dia — mesmo que o dia já esteja cheio: foi o aluno que aceitou.
//     As outras propostas (horas, dificuldade) já chegam pelas Preferencias.
struct DoAgente {
    std::vector<estudo::RegistroEstudo> estudos;
    std::vector<estudo::PontoFoco> focos;
    std::vector<estudo::Proposta> propostas;
};

struct Plano {
    std::vector<Sessao> sessoes;        // feitas + pendentes, por dia
    std::vector<SemanaPlano> semanas;   // de hoje à última prova
    std::vector<Dica> dicas;            // por prioridade
    // Minutos que não couberam, por chaveProva.
    std::map<std::string, int> deficit;
    // Minutos que agentes registraram para cada prova (chaveProva).
    std::map<std::string, int> comAgente;
};

// Duração de um horário da grade. O código diz QUANTOS horários há ("M23" =
// dois), não quanto dura cada um; na UNIFEI cada horário tem 55 min
// (M2 07:55–08:50, M3 08:50–09:45 — "M23" vai de 07:55 a 09:45). A tela
// mostra a conta, para o aluno saber de onde veio o desconto.
inline constexpr int kMinutosPorHoraAula = 55;

// Minutos de aula em cada dia da semana ([0] = segunda), pela grade de todas
// as turmas. Código ilegível conta zero — não dá para descontar o que não se
// sabe.
std::array<int, 7> minutosDeAulaPorDia(const std::vector<Turma>& turmas);

// O tempo de estudo do dia da semana `dia` (0 = segunda): o disponível menos
// as aulas, nunca negativo.
int minutosParaEstudo(const Preferencias& p, const std::array<int, 7>& aulas, int dia);

// Quanto estudar para uma prova, em minutos, antes de descontar o já feito.
int minutosNecessarios(const ProvaAlvo& p, Dificuldade d);

// O plano a partir de hoje. `guardadas` são as sessões do banco: as feitas
// entram como estão (e contam para a prova delas); as pendentes do passado
// viram a dica de atrasadas; as pendentes futuras são descartadas e
// recalculadas.
// `aulas` é `minutosDeAulaPorDia` da grade; zeros = nada a descontar.
Plano planejar(const std::vector<ProvaAlvo>& provas, const std::vector<EntregaAlvo>& entregas,
               const Preferencias& prefs, const std::vector<Sessao>& guardadas,
               const DateTime& hoje, const std::array<int, 7>& aulas = {},
               const DoAgente& agente = {});

// "1h30", "45 min", "2h".
std::string duracao(int minutos);

} // namespace sigaa::planejamento
