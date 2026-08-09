#pragma once
// Orquestra um ciclo de coleta.
//
// A navegação do SIGAA é uma máquina de estados server-side: cada view tem seu
// ViewState, e os "links" só valem na página onde foram lidos. Então o padrão é
// sempre: voltar ao portal -> ler os comandos de lá -> entrar numa turma.
// Reaproveitar um comando lido numa página anterior dá ViewExpired.
//
// Custo: 1 + 2 por turma (entrar + voltar). Com 7 turmas e o intervalo mínimo
// de 1,5 s, um ciclo completo leva ~25 s. É por isso que `incluirTurmas` é
// opcional: o portal sozinho já responde "o que vence essa semana".

#include <functional>
#include <string>

#include "core/http/SigaaSession.h"
#include "core/model/Models.h"

namespace sigaa::sync {

struct OpcoesColeta {
    bool incluirTurmas{false};
    // Chamado a cada passo, para o CLI mostrar progresso.
    std::function<void(const std::string&)> progresso;
};

struct ResultadoColeta {
    Snapshot snapshot;
    bool ok{false};
    std::string erro;
    int turmasVisitadas{0};
    int turmasComFalha{0};
    // HTML cru do portal, como veio da rede. É a fonte certa para gerar
    // fixture de parser — o Ctrl+S do browser salva o DOM pós-JavaScript, que
    // é 2,7× maior e tem markup que o servidor nunca envia (RECON §1.7).
    std::string htmlPortal;
    std::string htmlPrimeiraTurma;   // idem, para a primeira turma visitada
};

ResultadoColeta coletar(http::SigaaSession& sessao, const OpcoesColeta& op = {});

} // namespace sigaa::sync
