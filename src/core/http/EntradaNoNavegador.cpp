#include "core/http/EntradaNoNavegador.h"

namespace sigaa::http {
namespace {

// "https://sigaa.unifei.edu.br/" -> "https://sigaa.unifei.edu.br". A barra no
// fim duplicaria a do caminho, e "//sigaa/logar.do" é outra URL para o Tomcat.
std::string_view semBarraFinal(std::string_view u) {
    while (!u.empty() && u.back() == '/') u.remove_suffix(1);
    return u;
}

} // namespace

std::string escaparAtributo(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '"':  o += "&quot;"; break;
            case '\'': o += "&#39;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            default:   o.push_back(c);
        }
    }
    return o;
}

std::string paginaDeEntrada(std::string_view baseUrl, std::string_view login,
                            std::string_view senha, std::string_view nonce) {
    const std::string acao =
        escaparAtributo(std::string(semBarraFinal(baseUrl)) +
                        "/sigaa/logar.do?dispatch=logOn");

    // Os 8 campos que a tela de login do próprio SIGAA manda (RECON §1.8).
    // TODOS ocultos: o gerenciador de senhas do navegador ignora campo oculto,
    // e sem isso ele ofereceria "salvar senha para 127.0.0.1" — um endereço
    // que muda de porta a cada clique e onde a senha não tem por que ficar.
    struct Campo { const char* nome; std::string_view valor; };
    const Campo campos[] = {
        {"width", "1920"},          {"height", "1080"},
        {"urlRedirect", ""},        {"subsistemaRedirect", ""},
        {"acao", ""},               {"acessibilidade", ""},
        {"user.login", login},      {"user.senha", senha},
    };

    std::string h;
    h += "<!doctype html>\n<html lang=\"pt-BR\"><head><meta charset=\"utf-8\">\n"
         "<meta name=\"referrer\" content=\"no-referrer\">\n"
         "<title>Entrando no SIGAA…</title>\n"
         // Ícone vazio embutido: sem ele o navegador pede /favicon.ico à porta
         // local, a política recusa, e o console do aluno ganha um erro que
         // parece problema de segurança e não é nada.
         "<link rel=\"icon\" href=\"data:,\">\n"
         "<style>body{font:16px system-ui,sans-serif;margin:3em;color:#333}"
         "button{font:inherit;padding:.5em 1em}</style>\n"
         "</head><body>\n";

    // accept-charset windows-1252: é o que a tela de login do SIGAA declara
    // (RECON §1.10), e portanto o que o navegador mandaria se o aluno digitasse
    // lá. Para senha só com ASCII não muda nada; com "ç" ou "ã", mandar UTF-8
    // seria mandar bytes que o servidor nunca recebeu da própria página.
    h += "<form id=\"entrar\" method=\"post\" autocomplete=\"off\" "
         "accept-charset=\"windows-1252\" action=\"" + acao + "\">\n";
    for (const auto& c : campos) {
        h += "<input type=\"hidden\" name=\"";
        h += c.nome;
        h += "\" value=\"";
        h += escaparAtributo(c.valor);
        h += "\">\n";
    }
    // O botão existe para quem tem JavaScript desligado: sem ele a página
    // ficaria parada, com a senha dentro, sem dizer o que fazer.
    h += "<p>Entrando no SIGAA…</p>\n"
         "<noscript><button type=\"submit\">Entrar no SIGAA</button></noscript>\n"
         "</form>\n";

    // O AVISO DE DEMORA. Enquanto o POST não é respondido, o navegador segue
    // mostrando ESTA página e este endereço — e "parado em 127.0.0.1" parece
    // defeito do app mesmo quando quem não responde é o SIGAA (o segundo login
    // simultâneo de RECON §6.0). O script continua rodando até a resposta
    // chegar, então dá para dizer isso na própria página, com a saída manual
    // ao lado.
    const std::string telaLogin =
        escaparAtributo(std::string(semBarraFinal(baseUrl)) + "/sigaa/verTelaLogin.do");
    h += "<p id=\"demora\" hidden>O SIGAA ainda não respondeu. Isso acontece "
         "quando o servidor está lento ou quando ele segura um segundo login "
         "enquanto o app está conectado na mesma conta. Você pode esperar mais um "
         "pouco ou <a href=\"" + telaLogin + "\">abrir a tela de login do SIGAA"
         "</a> e entrar com a senha.</p>\n";

    // O AVISO DE BLOQUEIO. Se o navegador recusar o envio por alguma regra de
    // segurança, ele cancela em silêncio e a aba fica parada sem motivo
    // aparente. O evento abaixo transforma esse silêncio numa frase — e numa
    // pista de diagnóstico, com o endereço que foi barrado.
    h += "<p id=\"bloqueio\" hidden>O navegador bloqueou a entrada: "
         "<code id=\"barrado\"></code>. <a href=\"" + telaLogin +
         "\">Abrir a tela de login do SIGAA</a>.</p>\n";

    h += "<script nonce=\"" + escaparAtributo(nonce) + "\">\n"
         "document.addEventListener('securitypolicyviolation', function (e) {\n"
         "  document.getElementById('barrado').textContent = e.blockedURI || e.violatedDirective;\n"
         "  document.getElementById('bloqueio').hidden = false;\n"
         "});\n"
         "setTimeout(function () { document.getElementById('demora').hidden = false; }, 15000);\n"
         "document.getElementById('entrar').submit();\n"
         "</script>\n";
    h += "</body></html>\n";
    return h;
}

std::string cabecalhosDaEntrada(std::string_view nonce, std::size_t tamanhoCorpo) {
    // A política é o que faz a página valer só para o que ela promete:
    //   - script-src com nonce: só o nosso <script> roda;
    //   - default-src 'none': nada mais é carregado, nem imagem.
    //
    // SEM `form-action`, de propósito. Ela restringiria o envio ao SIGAA, mas
    // o Chrome a aplica também a cada REDIRECIONAMENTO depois do POST — e o
    // login do SIGAA termina num 302 cujo destino o app não controla (outro
    // host, http, uma tela de vínculo). Um destino fora da lista e o Chrome
    // cancela a navegação em silêncio, com a aba parada em 127.0.0.1. O que a
    // regra protegeria já está garantido de outro jeito: nenhum valor entra na
    // página sem escape (ver os testes), e só o nosso script roda.
    // no-store: a página tem a senha dentro, e não pode ficar no cache de
    // disco do navegador nem no "voltar" guardado em memória.
    std::string h;
    h += "HTTP/1.1 200 OK\r\n";
    h += "Content-Type: text/html; charset=utf-8\r\n";
    h += "Content-Length: " + std::to_string(tamanhoCorpo) + "\r\n";
    h += "Cache-Control: no-store\r\n";
    h += "Referrer-Policy: no-referrer\r\n";
    h += "X-Content-Type-Options: nosniff\r\n";
    h += "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
         "img-src data:; script-src 'nonce-" + std::string(nonce) + "'\r\n";
    h += "Connection: close\r\n\r\n";
    return h;
}

} // namespace sigaa::http
