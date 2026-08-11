#pragma once
// Qual SIGAA este app vai falar.
//
// O SIGAA é um sistema da UFRN adotado por dezenas de universidades federais,
// cada uma com a própria instância, o próprio host e — o que importa aqui — a
// própria VERSÃO. Toda a engenharia reversa em docs/RECON.md foi feita contra
// `sigaa.unifei.edu.br`, vSIGAA 4.12.14_U.164. Uma instituição rodando outra
// versão pode ter outros ids de campo, outro menu, outra tela de login.
//
// Por isso o catálogo carrega uma bandeira `verificada`, e ela é FALSA para
// tudo que não é a UNIFEI. Não é falsa modéstia: prometer suporte que ninguém
// testou faz o aluno culpar a senha dele quando o parser é que não entende a
// página. O app deixa escolher assim mesmo — é software aberto, e a pessoa que
// tiver a instância na mão é justamente quem pode consertar o parser — mas
// escolhe com o aviso na frente.
//
// A ESCOLHA NÃO MORA AQUI. Este módulo guarda a seleção da execução corrente;
// quem persiste é quem tem onde: a UI usa QSettings, o CLI usa argumento e
// ambiente. core/ não conhece nem um nem outro.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sigaa::config {

struct Instituicao {
    std::string id;        // "unifei" — estável, é o que vai para disco
    std::string nome;      // "UNIFEI — Universidade Federal de Itajubá"
    std::string baseUrl;   // "https://sigaa.unifei.edu.br"

    // O protocolo desta instância foi conferido contra o site de verdade?
    // Só a UNIFEI foi (docs/RECON.md). Ver o comentário do topo.
    bool verificada{false};

    // Host puro, sem esquema nem barra: "sigaa.unifei.edu.br".
    //
    // É a chave do cofre de credenciais. Tem que sair da URL, e não ser
    // constante, senão trocar de instituição faria o app tentar a senha de uma
    // universidade na outra — e algumas tentativas erradas bloqueiam a conta.
    std::string host() const;

    bool valida() const { return !baseUrl.empty(); }
};

// O catálogo embutido. A primeira entrada é a UNIFEI.
const std::vector<Instituicao>& catalogo();

// Busca no catálogo pelo id. nullopt se não existir.
std::optional<Instituicao> porId(std::string_view id);

// Uma instância fora do catálogo, informada pelo usuário. Aceita "unifei.br",
// "sigaa.ufxx.edu.br" ou a URL inteira; devolve algo inválido se não der para
// formar uma URL.
//
// Sempre https: o SIGAA pede login, e mandar CPF e senha em claro num http://
// datilografado às pressas seria um estrago que o app pode evitar sozinho.
Instituicao personalizada(std::string_view urlOuHost);

// --- seleção da execução corrente ------------------------------------------

// Default: UNIFEI, para que nada que existia antes mude de comportamento.
const Instituicao& selecionada();
void selecionar(Instituicao inst);

} // namespace sigaa::config
