#pragma once
// Transforma o resultado do diff em UM aviso para o usuário.
//
// Política deliberada, e a parte difícil não é técnica:
//
// 1. NO MÁXIMO UM AVISO POR CICLO. Um toast por evento parece "mais
//    informativo", mas oito toasts numa manhã ensinam o usuário a fechar tudo
//    sem ler — e aí o aviso que importava se perde junto. Agregamos.
//
// 2. SILÊNCIO É UMA RESPOSTA VÁLIDA. Sem novidade, nada aparece. O app só
//    interrompe quando tem algo a dizer.
//
// 3. COLETA SUSPEITA AVISA. É o inverso dos outros casos: aqui o perigo é o
//    usuário concluir que não há prazos porque o parser quebrou. Ver
//    core/sync/DiffEngine.h.
//
// Isto é lógica pura e testável — a chamada ao SO fica em platform/Notify.h.

#include <optional>
#include <string>

#include "core/sync/DiffEngine.h"

namespace sigaa::notify {

struct Aviso {
    std::string titulo;
    std::string corpo;
    bool urgente{false};   // muda o ícone/som; reservado ao que tem data
};

// Rótulo curto e em português para um tipo de evento.
std::string_view rotulo(sync::TipoEvento t);

// Menor número = mais merece o topo do aviso.
int prioridade(sync::TipoEvento t);

// nullopt = não interrompa o usuário.
std::optional<Aviso> montarAviso(const sync::ResultadoDiff& d);

} // namespace sigaa::notify
