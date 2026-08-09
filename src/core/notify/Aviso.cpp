#include "core/notify/Aviso.h"

#include <algorithm>
#include <vector>

namespace sigaa::notify {
namespace {

// Quantos itens cabem no corpo antes de virar parede de texto. Três é o que
// o balão do Windows mostra sem cortar; o resto vira "e mais N".
constexpr size_t kMaxLinhas = 3;

bool merecePressa(sync::TipoEvento t) {
    // Só o que tem data e pode fazer o aluno perder algo.
    switch (t) {
        case sync::TipoEvento::PrazoMudou:
        case sync::TipoEvento::AvaliacaoNova:
        case sync::TipoEvento::AvaliacaoRemarcada:
        case sync::TipoEvento::ColetaSuspeita:
            return true;
        default:
            return false;
    }
}

} // namespace

std::string_view rotulo(sync::TipoEvento t) {
    switch (t) {
        case sync::TipoEvento::TurmaNova:          return "turma nova";
        case sync::TipoEvento::AtividadeNova:      return "atividade nova";
        case sync::TipoEvento::PrazoMudou:         return "prazo alterado";
        case sync::TipoEvento::AtividadeConcluida: return "atividade concluída";
        case sync::TipoEvento::AtualizacaoNova:    return "atualização na turma";
        case sync::TipoEvento::AvaliacaoNova:      return "prova marcada";
        case sync::TipoEvento::AvaliacaoRemarcada: return "PROVA REMARCADA";
        case sync::TipoEvento::ColetaSuspeita:     return "coleta falhou";
    }
    return "novidade";
}

int prioridade(sync::TipoEvento t) {
    switch (t) {
        case sync::TipoEvento::ColetaSuspeita:     return 0;
        case sync::TipoEvento::AvaliacaoRemarcada: return 1;
        case sync::TipoEvento::PrazoMudou:         return 2;
        case sync::TipoEvento::AvaliacaoNova:      return 3;
        case sync::TipoEvento::AtividadeNova:      return 4;
        case sync::TipoEvento::AtualizacaoNova:    return 5;
        case sync::TipoEvento::TurmaNova:          return 6;
        case sync::TipoEvento::AtividadeConcluida: return 7;
    }
    return 9;
}

std::optional<Aviso> montarAviso(const sync::ResultadoDiff& d) {
    // Coleta suspeita tem precedência sobre tudo: o diff nem chegou a comparar,
    // então contar eventos aqui não faria sentido.
    if (d.suspeito) {
        return Aviso{
            "SIGAA: não consegui ler seus prazos",
            "A coleta veio vazia — provavelmente o site mudou, não os seus "
            "prazos. Não confie na ausência de avisos; confira no SIGAA.",
            true};
    }

    // Linha de base não é novidade (ver DiffEngine.h).
    if (d.primeiraExecucao || d.eventos.empty()) return std::nullopt;

    auto ev = d.eventos;
    std::stable_sort(ev.begin(), ev.end(),
                     [](const sync::Evento& a, const sync::Evento& b) {
                         return prioridade(a.tipo) < prioridade(b.tipo);
                     });

    Aviso a;
    a.urgente = std::any_of(ev.begin(), ev.end(), [](const sync::Evento& e) {
        return merecePressa(e.tipo);
    });

    if (ev.size() == 1) {
        const auto& e = ev.front();
        a.titulo = "SIGAA: " + std::string(rotulo(e.tipo));
        a.corpo = e.titulo;
        if (!e.turmaNome.empty()) a.corpo += " (" + e.turmaNome + ")";
        if (!e.detalhe.empty()) a.corpo += "\n" + e.detalhe;
        return a;
    }

    a.titulo = "SIGAA: " + std::to_string(ev.size()) + " novidades";
    for (size_t i = 0; i < ev.size() && i < kMaxLinhas; ++i) {
        const auto& e = ev[i];
        if (i) a.corpo += "\n";
        a.corpo += std::string(rotulo(e.tipo)) + ": " + e.titulo;
        if (!e.turmaNome.empty()) a.corpo += " (" + e.turmaNome + ")";
    }
    if (ev.size() > kMaxLinhas) {
        a.corpo += "\ne mais " + std::to_string(ev.size() - kMaxLinhas) +
                   " — abra o relatório";
    }
    return a;
}

} // namespace sigaa::notify
