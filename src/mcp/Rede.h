#pragma once
// As ferramentas que falam com o SIGAA (docs/MCP.md, fase 4): baixar um
// material que falta e atualizar uma turma, a pedido do agente.
//
// É a única porta do servidor que gera tráfego na conta do aluno, e por isso
// a mais cercada:
//   - exige a permissão `rede`, desligada por padrão;
//   - um ORÇAMENTO por sessão do agente (o processo do servidor): cada
//     operação custa o que custa em requisições, e acabou, acabou — o SIGAA
//     bloqueia conta que consulta rápido demais, e um agente em laço não
//     pode ser o motivo;
//   - um intervalo mínimo entre duas operações, e uma de cada vez;
//   - a senha vem do cofre do sistema por um provedor que o executável
//     registra; o agente nunca a vê, nem por erro.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mcp/Leitura.h"

namespace sigaa::mcp {

struct Credenciais {
    std::string login;
    std::string senha;
};

// Quem roda o servidor (sigaa-cli, sigaa-ui) registra de onde vem a senha.
// Sem provedor, as ferramentas de rede respondem que não há credenciais.
using ProvedorCredenciais = std::function<std::optional<Credenciais>()>;
void definirProvedorDeCredenciais(ProvedorCredenciais p);

const std::vector<Ferramenta>& ferramentasDeRede();

// Requisições que uma sessão pode gastar, e quanto custa cada operação.
inline constexpr int kOrcamentoRede = 24;
inline constexpr int kCustoBaixar = 6;      // login, portal, turma, arquivos, download
inline constexpr int kCustoAtualizar = 8;   // login, portal e as abas da turma
inline constexpr int kIntervaloRede = 20;   // segundos entre duas operações

// Devolve o orçamento e o intervalo ao estado inicial. Só para testes.
void zerarOrcamentoDeRede();

} // namespace sigaa::mcp
