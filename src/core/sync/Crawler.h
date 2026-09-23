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
#include <set>
#include <string>

#include "core/http/SigaaSession.h"
#include "core/model/Models.h"

namespace sigaa::sync {

struct OpcoesColeta {
    bool incluirTurmas{false};

    // Abre também a aba Arquivos de cada turma visitada.
    //
    // É o que faz o app perceber que o professor publicou o PDF da aula de
    // hoje — a novidade mais frequente do semestre, e a única que o portal
    // NUNCA mostra: "Atualizações das Turmas" traz um texto solto ("Novo
    // Arquivo: ..."), sem id, sem título confiável e sem o material.
    //
    // Custa uma requisição a mais por turma (~1,5 s cada, pelo intervalo
    // mínimo). Só tem efeito com `incluirTurmas`.
    bool incluirArquivos{true};

    // Abre também a aba Participantes de cada turma visitada.
    //
    // PADRÃO DESLIGADO, ao contrário de `incluirArquivos`: a lista de colegas
    // muda no começo do semestre e depois fica parada, então pagar uma
    // requisição por turma a cada ciclo seria comprar sempre a mesma resposta.
    // Quem chama liga isto quando o banco ainda não tem ninguém guardado —
    // é a primeira coleta completa que popula a aba, e daí em diante a janela
    // da turma reatualiza sob demanda, só na turma que o aluno abriu.
    //
    // NÃO HÁ COLETA DE FOTO, aqui nem em lugar nenhum. O app já baixava o
    // retrato de cada participante para uma pasta em disco; saiu em
    // 18/09/2026, porque a imagem de alguém é dado dessa pessoa e os 31
    // colegas de turma não escolheram tê-la copiada para a máquina de outro.
    // A lista mostra as iniciais, que resolvem o mesmo problema.
    bool incluirParticipantes{false};

    // Abre o mapa de frequência de cada turma visitada.
    //
    // PADRÃO LIGADO, ao contrário de `incluirParticipantes`: custa a mesma uma
    // requisição por turma, mas o dado MUDA toda semana e é o que responde
    // "ainda posso faltar?" — enquanto a lista de colegas é a mesma o semestre
    // inteiro e por isso só vale a pena buscar uma vez.
    bool incluirFrequencia{true};

    // Visita SÓ estas turmas (por `idTurma`). Vazio = todas, que é o padrão.
    //
    // O QUE ISTO COMPRA: o gargalo do app não é processar HTML, é esperar o
    // SIGAA responder — cada turma custa de 2 a 4 requisições de ~1,5 s. Quem
    // só quer saber se faltou na aula de hoje não tem por que pagar a visita
    // às outras cinco turmas, e é a diferença entre 40 segundos e 6.
    //
    // O portal continua sendo lido por inteiro (é UMA requisição, e é de onde
    // saem os prazos), então filtrar aqui nunca esconde uma atividade nova.
    std::set<std::string> apenasTurmas;

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
