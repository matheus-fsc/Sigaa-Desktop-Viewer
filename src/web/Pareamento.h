#pragma once
// O que separa "estar na mesma rede" de "poder ler as turmas": o token que o
// celular recebe no pareamento (docs/WEB.md §3).
//
// POR QUE UM TOKEN, se o acesso já passa por uma VPN: a VPN diz quem está na
// rede, não quem é você. Uma tailnet compartilhada, um notebook antigo ainda
// logado na VPN ou alguém que escolheu "rede local" em vez da interface da VPN
// alcançam a porta. O token é a segunda chave e vale igual em qualquer VPN.
//
// O token fica no banco (`meta`, chave "web.token"), e não no cofre do
// sistema: quem consegue ler o banco já tem todos os dados que o token
// protege. O cofre guardaria o cadeado ao lado do que ele tranca.

#include <string>
#include <string_view>

namespace sigaa::store {
class Database;
}

namespace sigaa::web {

// 32 bytes aleatórios em base64url, sem preenchimento: 43 caracteres, que
// cabem num QR code pequeno e numa URL sem escape.
std::string gerarToken();

// O token em vigor. Vazio se nenhum celular foi pareado ainda.
std::string tokenAtual(store::Database& db);

// Gera, grava e devolve um token novo. O anterior para de valer na hora:
// é assim que se "revoga" um celular perdido.
std::string novoToken(store::Database& db);

// O token em vigor, criando um se ainda não houver.
std::string tokenOuNovo(store::Database& db);

// Comparação em tempo constante, para o tempo de resposta não contar quantos
// caracteres do palpite estavam certos.
bool tokensIguais(std::string_view a, std::string_view b);

// O valor do cabeçalho `Authorization` traz este token?
bool autorizado(std::string_view cabecalho, std::string_view token);

// Endereços que o servidor recusa sem pergunta: os "todas as interfaces".
// Escutar em 0.0.0.0 é o erro que expõe o app no Wi-Fi da faculdade inteiro,
// e a VPN não protege o que não passa por ela.
bool enderecoAberto(std::string_view host);

// A URL que o celular abre: o token vai no FRAGMENTO (#), que o navegador
// nunca manda ao servidor, então ele não aparece em log nem em proxy.
std::string urlDePareamento(const std::string& host, int porta, const std::string& token);

} // namespace sigaa::web
