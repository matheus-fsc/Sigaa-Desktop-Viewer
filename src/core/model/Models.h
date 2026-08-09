#pragma once
// Modelo de domínio. Structs puras — sem lógica de rede, sem UI.
// Nomes de domínio ficam em português porque é o vocabulário do SIGAA.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace sigaa {

// Data/hora ingênua (sem timezone). O SIGAA opera em horário de Brasília e
// nunca envia offset, então guardamos o que ele diz e resolvemos no display.
struct DateTime {
    int year{}, month{}, day{};
    int hour{}, minute{};
    bool hasTime{false};

    bool valid() const { return year > 0 && month > 0 && day > 0; }

    // ISO-8601 sem timezone, ou "" se inválida.
    std::string toIso() const {
        if (!valid()) return {};
        char buf[32];
        if (hasTime) {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d", year, month, day,
                          hour, minute);
        } else {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
        }
        return buf;
    }

    // Ordenação cronológica — usada para montar a timeline.
    bool operator<(const DateTime& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        if (day != o.day) return day < o.day;
        if (hour != o.hour) return hour < o.hour;
        return minute < o.minute;
    }
};

enum class StatusAtividade {
    Pendente,    // sem ícone específico
    NaSemana,    // prova_semana.png
    Concluida,   // check.png
    Desconhecido
};

// Uma turma. Os dados vêm de DUAS telas (ver docs/RECON.md §1.5 e §1.10):
//   - catálogo inline (#formTurma): codigo, nome, cargaHoraria, periodo, frontEndId
//   - tabela do portal:             local, horario
struct Turma {
    std::string idTurma;        // "88094"  — usado por atualizações e chat
    std::string frontEndId;     // hash de 40 hex — ÚNICO jeito de entrar na turma virtual
    std::string codigo;         // "EPRIB001"
    std::string nome;           // "ADMINISTRAÇÃO"
    std::string periodo;        // "2026.2"
    int cargaHoraria{0};        // 32, 64...
    std::string local;          // "Anexo 3 - Sala 19"
    std::string horario;        // "6T34" (código SIGAA)
};

struct Atividade {
    std::string atividadeId;    // "130174755"
    std::string idTurma;        // "89151"      -> chave: (atividadeId, idTurma)
    std::string turmaNome;
    std::string periodo;
    std::string tipo;           // "Tarefa", "Prova", ...
    std::string titulo;
    DateTime prazo;
    StatusAtividade status{StatusAtividade::Desconhecido};
};

struct Atualizacao {
    std::string hash;           // sha1(data|idTurma|texto) — não há ID no HTML
    std::string idTurma;
    std::string turmaNome;
    DateTime data;              // só a data, sem hora
    std::string texto;          // "Nova tarefa: ...", "Novo Arquivo: ..."
};

struct TopicoAula {
    std::string idTurma;
    std::string titulo;
    DateTime inicio;
    DateTime fim;
    std::string conteudo;
};

// Origem da avaliação — importa porque as duas fontes divergem por professor
// e o merge precisa saber em quem confiar (ver docs/RECON.md §1.6).
enum class FonteAvaliacao {
    PainelAvaliacoes,   // div.rich-stglpanel "Avaliações" — alta confiança
    TopicoAula          // regex sobre título de tópico — fallback
};

struct Avaliacao {
    std::string idTurma;
    std::string turmaNome;
    std::string descricao;      // "1ª Avaliação"
    DateTime quando;
    // Texto cru do horário. Formato VARIA: "6T34" (código SIGAA) ou "15:45".
    std::string horarioBruto;
    FonteAvaliacao fonte{FonteAvaliacao::PainelAvaliacoes};
};

// Um material publicado pelo professor na aba "Arquivos" da Turma Virtual.
//
// A chave é `idArquivo`, o parâmetro `id` avulso do jsfcljs (RECON §1.6.1). O
// id do COMPONENTE que aparece ao lado dele é posicional e muda quando a
// instituição recompila a página — guardar aquele daria um botão de download
// que aponta para o arquivo errado depois de um upgrade do SIGAA.
struct ArquivoTurma {
    std::string idTurma;
    std::string turmaNome;
    std::string idArquivo;      // "526919"
    std::string titulo;         // "LIVRO: Algoritmos - Thomas H. Cormen"
    std::string descricao;      // quase sempre vazia na prática
    std::string topico;         // "Materiais auxiliares para Estudo"
};

// Snapshot completo de um ciclo de coleta.
struct Snapshot {
    std::vector<Turma> turmas;
    std::vector<Atividade> atividades;
    std::vector<Atualizacao> atualizacoes;
    std::vector<TopicoAula> topicos;
    std::vector<Avaliacao> avaliacoes;
    std::optional<int> minutosSessaoRestantes;  // lido do header do SIGAA
};

} // namespace sigaa
