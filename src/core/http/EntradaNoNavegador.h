#pragma once
// "Entrar no SIGAA": abrir o SIGAA no navegador do aluno, já autenticado.
//
// POR QUE NÃO DÁ PARA ENTREGAR A SESSÃO DO APP. O navegador não aceita cookie
// vindo de outro programa — não existe API para isso, e ainda bem. O atalho
// seria colar o JSESSIONID na URL (";jsessionid=..."), e as duas saídas dele
// são ruins: a sessão passaria a ser dividida entre o app e o navegador, que
// guardam ViewStates diferentes no servidor (RECON §2.2) e se derrubariam; e o
// identificador de sessão ficaria no histórico do navegador, que é uma
// credencial num lugar que sincroniza com a nuvem.
//
// O QUE FAZEMOS. O navegador faz o PRÓPRIO login, numa sessão só dele. O app
// serve uma página local de uso único com o formulário de login do SIGAA já
// preenchido, e ela se envia sozinha. Isso só funciona porque o login do
// SIGAA é um POST sem token anti-CSRF (RECON §1.8) — a mesma ausência que
// está registrada como falha em RECON §6.2. No dia em que a instituição
// corrigir, esta página passa a cair na tela de login do SIGAA, e o aluno
// digita a senha como faria de qualquer forma. Não quebra nada além do atalho.
//
// Este arquivo só MONTA a página. Quem a serve é a interface
// (ui/EntrarNoSigaa.h), porque servir é Qt e aqui não entra Qt.

#include <cstddef>
#include <string>
#include <string_view>

namespace sigaa::http {

// A página que o navegador abre e que posta o login no SIGAA.
//
// `baseUrl` é a da instituição escolhida ("https://sigaa.unifei.edu.br"). O
// `nonce` libera o único <script> da página na política de segurança — sem
// ele, a política bloquearia o envio automático.
std::string paginaDeEntrada(std::string_view baseUrl, std::string_view login,
                            std::string_view senha, std::string_view nonce);

// Os cabeçalhos HTTP que acompanham a página. Separados porque a política de
// segurança depende do mesmo `nonce` do corpo.
std::string cabecalhosDaEntrada(std::string_view nonce, std::size_t tamanhoCorpo);

// --- exposto para teste ----------------------------------------------------

// Escapa para dentro de um valor de atributo entre aspas duplas. Uma senha com
// aspas ou "<" não pode fechar o atributo: o formulário mandaria outra coisa,
// o SIGAA recusaria, e o aluno perderia uma das poucas tentativas de login
// antes do bloqueio da conta — por causa de um caractere da própria senha.
std::string escaparAtributo(std::string_view s);

} // namespace sigaa::http
