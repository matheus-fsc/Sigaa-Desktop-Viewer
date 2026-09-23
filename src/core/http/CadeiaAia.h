#pragma once
// AIA chasing: buscar sozinho o pedaço da cadeia TLS que o servidor esqueceu.
//
// O PROBLEMA. Um servidor mal configurado envia o certificado dele e a raiz,
// mas não o intermediário que liga os dois. O certificado é válido, a raiz é
// confiável, e mesmo assim a verificação falha — falta a peça do meio. Foi o
// que o SIGAA da UNIFEI passou a fazer em 16/09/2026 (docs/RECON.md §6.1).
//
// Os navegadores disfarçam porque fazem isto aqui: leem a extensão Authority
// Information Access do certificado, que diz onde baixar o emissor, e buscam a
// peça que falta. A libcurl não faz. O resultado é "abre no Chrome, não abre no
// app" — a pior forma de um erro se apresentar, porque convence o usuário de
// que o app é que está quebrado.
//
// POR QUE ISTO NÃO ENFRAQUECE NADA — e o raciocínio importa, porque a
// alternativa preguiçosa (desligar `CURLOPT_SSL_VERIFYPEER`) é uma linha e
// resolveria o sintoma:
//
//   O intermediário buscado NÃO vira âncora de confiança. Ele entra no bundle
//   ao lado das CAs do sistema, e o OpenSSL, com as flags padrão, continua
//   exigindo que a cadeia termine num certificado AUTO-ASSINADO da lista. Um
//   intermediário falso não é auto-assinado e não fecha cadeia com raiz
//   nenhuma: a verificação falha igual.
//
//   Por isso a sondagem que lê o certificado pode rodar sem verificação sem
//   consequência: tudo o que ela descobre é reconferido depois, pela
//   verificação de verdade. Um atacante no meio do caminho consegue nos mandar
//   para um intermediário forjado — e ganha nada, porque ele não vai fechar
//   cadeia com nenhuma raiz confiável.
//
//   A sondagem NÃO carrega credencial nem cookie, e o corpo da resposta é
//   descartado. Ela existe para ler um certificado, não para transportar dado.

#include <optional>
#include <vector>
#include <string>

namespace sigaa::http {

// Um bundle PEM com as CAs do sistema mais os intermediários que o servidor de
// `baseUrl` deveria ter enviado. `nullopt` quando não deu para montar — e aí o
// erro original vale, porque inventar confiança seria pior que falhar.
//
// Custa duas requisições (a sondagem e o download do certificado) e roda no
// máximo UMA vez por sessão: quem chama guarda o resultado.
std::optional<std::string> montarBundleAia(const std::string& baseUrl);

// Expostos para teste — são a parte com aritmética, e é onde um erro passaria
// despercebido até o dia em que a cadeia real precisasse deles.

// DER (binário) -> PEM. Entrada que já é PEM volta intacta.
std::string derParaPem(const std::string& bytes);

// As URLs de "CA Issuers" do texto que o CURLINFO_CERTINFO devolve.
std::vector<std::string> urlsDeEmissor(const std::string& certinfo);

} // namespace sigaa::http
