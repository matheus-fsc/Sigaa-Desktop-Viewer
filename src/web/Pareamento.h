#pragma once
// O que separa "estar na mesma rede" de "poder ler as turmas" (docs/WEB.md §3).
//
// POR QUE PAREAR, se o acesso já passa por uma VPN: a VPN diz quem está na
// rede, não quem é você. Uma tailnet compartilhada, um notebook antigo ainda
// logado na VPN ou alguém que escolheu "rede local" em vez da interface da VPN
// alcançam a porta. O pareamento é a segunda chave e vale igual em qualquer VPN.
//
// DUAS CREDENCIAIS DE PAREAMENTO, um token por aparelho:
//
//   - o código do QR (43 caracteres, no link `#t=`): o jeito rápido, com a
//     câmera;
//   - o PIN que o aluno escolhe (6 a 12 dígitos): para digitar o endereço
//     curto no celular sem depender do link gigante.
//
//   As duas servem só para PAREAR. O celular as troca, uma vez, por um token
//   próprio dele (`/api/parear`), e é esse token que vai nos pedidos. É o que
//   deixa a aba listar os aparelhos e desconectar um só.
//
// O QUE FICA NO BANCO: o código do QR em `meta` ("web.token"), porque a aba
// precisa mostrá-lo de novo; o PIN e os tokens dos aparelhos, só como hash.
// Quem lê o banco já tem os dados que isso protege, então o hash não é o que
// segura o PIN; o que segura é o limite de tentativas do servidor.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/store/Database.h"

namespace sigaa::web {

// 32 bytes aleatórios em base64url, sem preenchimento: 43 caracteres, que
// cabem num QR code pequeno e numa URL sem escape.
std::string gerarToken();

// --- código do QR ------------------------------------------------------------

// O código em vigor. Vazio se nunca foi gerado.
std::string tokenAtual(store::Database& db);

// Gera, grava e devolve um código novo. O anterior deixa de parear na hora
// (os aparelhos já pareados continuam: eles têm token próprio).
std::string novoToken(store::Database& db);

// O código em vigor, criando um se ainda não houver.
std::string tokenOuNovo(store::Database& db);

// --- PIN -----------------------------------------------------------------------

constexpr int kPinMinimo = 6;
constexpr int kPinMaximo = 12;

// Só dígitos, entre o mínimo e o máximo. Dígitos porque é o teclado numérico
// que o celular abre; o mínimo de 6 é o que torna o limite de tentativas do
// servidor suficiente (um milhão de combinações a cinco por quarto de hora).
bool pinValido(std::string_view pin);

bool temPin(store::Database& db);
// Falso se o PIN não é válido ou não deu para gravar.
bool definirPin(store::Database& db, std::string_view pin);
bool removerPin(store::Database& db);
bool pinConfere(store::Database& db, std::string_view pin);

// --- aparelhos ---------------------------------------------------------------

// SHA-256 do token do aparelho: o que o banco guarda.
std::string hashDoToken(std::string_view token);

// Confere o código do QR ou o PIN e, se um deles bater, cria o aparelho e
// devolve o token dele. Vazio = recusado. `nome` vem do celular e é cortado.
struct Pedido {
    std::string codigo;   // o do QR, ou vazio
    std::string pin;      // ou vazio
    std::string nome;
    std::string ip;
};
std::string parear(store::Database& db, const Pedido& p, std::int64_t agora);

// O aparelho dono do cabeçalho `Authorization: Bearer <token>`, se houver.
std::optional<store::Database::DispositivoWeb> aparelhoDoCabecalho(store::Database& db,
                                                                   std::string_view cabecalho);

// Comparação em tempo constante, para o tempo de resposta não contar quantos
// caracteres do palpite estavam certos.
bool tokensIguais(std::string_view a, std::string_view b);

// --- endereço ------------------------------------------------------------------

// Endereços que o servidor recusa sem pergunta: os "todas as interfaces".
// Escutar em 0.0.0.0 é o erro que expõe o app no Wi-Fi da faculdade inteiro,
// e a VPN não protege o que não passa por ela.
bool enderecoAberto(std::string_view host);

// A URL que o celular abre. Com `token`, ele vai no FRAGMENTO (#), que o
// navegador nunca manda ao servidor, então não aparece em log nem em proxy.
std::string urlDePareamento(const std::string& host, int porta, const std::string& token);

} // namespace sigaa::web
