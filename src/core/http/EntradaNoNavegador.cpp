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

    // O VISUAL. A página fica na tela de um a sete segundos — o tempo do login
    // do SIGAA —, e nesse intervalo ela é a cara do app no navegador. Um
    // parágrafo solto em fonte padrão parecia erro; um cartão com a barra
    // andando diz "está funcionando, espere", que é tudo o que ela precisa
    // dizer.
    //
    // Tudo inline, e não por acaso: a política da página (ver
    // cabecalhosDaEntrada) só deixa carregar o que está DENTRO dela. SVG
    // embutido no HTML não é um recurso buscado, então o ícone passa; uma
    // imagem externa ou uma fonte da web seriam recusadas.
    //
    // A barra é INDETERMINADA de propósito: o app não tem como saber quanto
    // falta — quem responde é o SIGAA —, e uma barra que enche até 90% e para
    // é uma mentira que todo mundo já aprendeu a reconhecer.
    static constexpr const char* kEstilo = R"CSS(
:root{--fundo:#f3f5f8;--cartao:#fff;--texto:#1d2330;--suave:#5b6474;--borda:#e1e5ec;
--acento:#2f6fed;--trilho:#e6ecf7;--aviso:#c77700;--aviso-fundo:#fff6e5;--erro:#c62828;--erro-fundo:#fdecec}
@media (prefers-color-scheme:dark){:root{--fundo:#15181d;--cartao:#1e222a;--texto:#e7eaf0;
--suave:#a0a8b8;--borda:#2c323d;--acento:#6c9bff;--trilho:#2a3140;--aviso:#f0b24a;
--aviso-fundo:#2e2616;--erro:#ff7b7b;--erro-fundo:#321b1d}}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--fundo);
color:var(--texto);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:16px}
.cartao{width:100%;max-width:440px;background:var(--cartao);border:1px solid var(--borda);
border-radius:16px;padding:32px 28px;box-shadow:0 10px 30px rgba(0,0,0,.08);text-align:center}
.icone{width:56px;height:56px;margin:0 auto 16px;border-radius:14px;display:grid;place-items:center;
background:var(--trilho);color:var(--acento)}
h1{font-size:1.25rem;margin:0 0 6px}
p{margin:0;color:var(--suave)}
.trilho{position:relative;height:6px;border-radius:3px;background:var(--trilho);overflow:hidden;margin:24px 0 8px}
.barra{position:absolute;top:0;bottom:0;left:-40%;width:40%;border-radius:3px;background:var(--acento);
animation:corre 1.2s ease-in-out infinite}
@keyframes corre{0%{left:-40%}100%{left:100%}}
@media (prefers-reduced-motion:reduce){.barra{animation:pulsa 1.6s ease-in-out infinite;left:0;width:100%}
@keyframes pulsa{50%{opacity:.35}}}
.aviso{margin-top:20px;padding:12px 14px;border-radius:10px;text-align:left;font-size:.93rem}
.aviso.demora{background:var(--aviso-fundo);color:var(--texto);border:1px solid var(--aviso)}
.aviso.bloqueio{background:var(--erro-fundo);color:var(--texto);border:1px solid var(--erro)}
.aviso a{color:var(--acento);font-weight:600}
code{font-size:.85em;word-break:break-all}
button{font:inherit;margin-top:20px;padding:.6em 1.2em;border-radius:8px;border:0;
background:var(--acento);color:#fff;cursor:pointer}
body.lento .barra{background:var(--aviso);animation-duration:2.4s}
body.parado .barra{animation:none;left:0;width:100%;background:var(--erro)}
)CSS";

    std::string h;
    h += "<!doctype html>\n<html lang=\"pt-BR\"><head><meta charset=\"utf-8\">\n"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
         "<meta name=\"referrer\" content=\"no-referrer\">\n"
         "<title>Entrando no SIGAA…</title>\n"
         // Ícone vazio embutido: sem ele o navegador pede /favicon.ico à porta
         // local, a política recusa, e o console do aluno ganha um erro que
         // parece problema de segurança e não é nada.
         "<link rel=\"icon\" href=\"data:,\">\n"
         "<style>";
    h += kEstilo;
    h += "</style>\n</head><body>\n<main class=\"cartao\">\n";

    // O mesmo desenho do botão na barra do app (icones/abrir-sigaa.svg): quem
    // clicou reconhece de onde veio a página.
    h += "<div class=\"icone\" aria-hidden=\"true\"><svg width=\"28\" height=\"28\" "
         "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" "
         "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M14 4h6v6\"/>"
         "<path d=\"M20 4l-9 9\"/><path d=\"M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7"
         "a1 1 0 0 1 1-1h5\"/></svg></div>\n";

    h += "<h1>Entrando no SIGAA</h1>\n"
         "<p>Aguarde enquanto seu navegador é redirecionado para o SIGAA.</p>\n"
         "<div class=\"trilho\" role=\"progressbar\" aria-label=\"Entrando no SIGAA\">"
         "<div class=\"barra\"></div></div>\n";

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
    h += "<noscript><button type=\"submit\">Entrar no SIGAA</button></noscript>\n"
         "</form>\n";

    // O AVISO DE DEMORA. Enquanto o POST não é respondido, o navegador segue
    // mostrando ESTA página e este endereço — e "parado em 127.0.0.1" parece
    // defeito do app mesmo quando quem não responde é o SIGAA (o segundo login
    // simultâneo de RECON §6.0). O script continua rodando até a resposta
    // chegar, então dá para dizer isso na própria página, com a saída manual
    // ao lado.
    const std::string telaLogin =
        escaparAtributo(std::string(semBarraFinal(baseUrl)) + "/sigaa/verTelaLogin.do");
    h += "<div id=\"demora\" class=\"aviso demora\" hidden>O SIGAA ainda não "
         "respondeu. Isso acontece quando o servidor está lento ou quando ele segura "
         "um segundo login enquanto o app está conectado na mesma conta. Você pode "
         "esperar mais um pouco ou <a href=\"" + telaLogin + "\">abrir a tela de "
         "login do SIGAA</a> e entrar com a senha.</div>\n";

    // O AVISO DE BLOQUEIO. Se o navegador recusar o envio por alguma regra de
    // segurança, ele cancela em silêncio e a aba fica parada sem motivo
    // aparente. O evento abaixo transforma esse silêncio numa frase — e numa
    // pista de diagnóstico, com o endereço que foi barrado.
    h += "<div id=\"bloqueio\" class=\"aviso bloqueio\" hidden>O navegador bloqueou "
         "a entrada: <code id=\"barrado\"></code>. <a href=\"" + telaLogin +
         "\">Abrir a tela de login do SIGAA</a>.</div>\n";

    h += "</main>\n";

    // A barra muda junto com o aviso: âmbar e mais lenta quando o SIGAA
    // demora, vermelha e parada quando o navegador bloqueia. Uma barra que
    // continua correndo feliz ao lado de "bloqueado" contradiz o texto.
    h += "<script nonce=\"" + escaparAtributo(nonce) + "\">\n"
         "document.addEventListener('securitypolicyviolation', function (e) {\n"
         "  document.getElementById('barrado').textContent = e.blockedURI || e.violatedDirective;\n"
         "  document.getElementById('bloqueio').hidden = false;\n"
         "  document.getElementById('demora').hidden = true;\n"
         "  document.body.className = 'parado';\n"
         "});\n"
         "setTimeout(function () {\n"
         "  if (document.body.className === 'parado') return;\n"
         "  document.getElementById('demora').hidden = false;\n"
         "  document.body.className = 'lento';\n"
         "}, 15000);\n"
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
