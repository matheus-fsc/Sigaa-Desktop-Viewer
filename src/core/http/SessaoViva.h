#pragma once
// Uma sessão do SIGAA mantida VIVA e reaproveitada, em vez de uma por tarefa.
//
// O PROBLEMA QUE ISTO RESOLVE. Cada consumidor abria a própria sessão e fazia
// o próprio login: o diálogo de senha para conferir a credencial, a
// sincronização logo em seguida, a janela de turma depois. Login é a operação
// mais cara do app — 6 a 8 segundos observados — e nenhuma dessas sessões era
// encerrada, então elas se acumulavam vivas no servidor.
//
// O sintoma foi o app parecer travado: confirmada a senha (login 1), a
// sincronização abria a segunda sessão e o SIGAA não respondia ao POST de
// login — três timeouts de 45 s antes de desistir. Dois logins para uma ação
// só, e o segundo não passava.
//
// A saída não é deslogar entre as tarefas: `logOff` → `logOn` → ação é ainda
// mais lento, e uma conta que faz isso o dia todo parece um robô. A saída é
// LOGAR UMA VEZ e reusar enquanto a sessão valer.
//
// O QUE FAZ A SESSÃO VALER. O SIGAA expira em 30 minutos de inatividade, e
// informa quanto falta no cabeçalho de toda página ("Tempo de Sessão: 00:25"),
// que `SigaaSession::minutosSessaoRestantes` já lê. Então não há adivinhação:
//
//   - toda requisição renova o relógio do servidor, então uso contínuo nunca
//     expira;
//   - parada longa é detectada aqui, pelo relógio local, ANTES de gastar uma
//     requisição para descobrir que a sessão morreu;
//   - e se ainda assim a resposta vier como tela de login, `invalidar()` marca
//     a sessão como morta e o próximo `obter` faz login de novo.
//
// UM CONSUMIDOR POR VEZ. Isto não é um pool: a sessão é UMA, e o SIGAA guarda
// o ViewState no servidor — duas navegações simultâneas na mesma sessão
// invalidam-no (RECON §2.2). Quem precisa de paralelismo de verdade (o
// Baixador) continua abrindo sessões próprias, e ESSAS devem ser encerradas
// com `logout()` ao terminar, porque são descartáveis por natureza.

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "core/http/SigaaSession.h"

namespace sigaa::http {

class SessaoViva {
public:
    // Margem antes dos 30 minutos do SIGAA. Reconectar por conta própria com
    // 5 minutos de folga é mais barato que descobrir a expiração no meio de
    // uma coleta, quando já se gastou a requisição e o usuário já esperou.
    static constexpr std::chrono::minutes kValidade{25};

    // A sessão autenticada, logando só se preciso. `nullptr` com `erro`
    // preenchido quando o login falhou.
    //
    // O ponteiro devolvido é da sessão COMPARTILHADA: quem chama usa e
    // devolve, não guarda para usar em paralelo com outro consumidor.
    SigaaSession* obter(const std::string& login, const std::string& senha,
                        std::string* erro = nullptr);

    // Marca a sessão como morta. Chamar quando uma resposta vier como tela de
    // login: o servidor já descartou a sessão e insistir nela gasta
    // requisições para receber a mesma tela.
    void invalidar();

    // Encerra no servidor e esquece. Para o fim da vida do app — não entre
    // tarefas, que é justamente o que se quer evitar.
    void encerrar();

    bool viva() const;

    // Quanto falta para considerarmos a sessão velha. Negativo = já venceu.
    std::chrono::seconds restante() const;

private:
    mutable std::mutex mtx_;
    std::unique_ptr<SigaaSession> sessao_;
    std::chrono::steady_clock::time_point ultimoUso_{};
};

} // namespace sigaa::http
