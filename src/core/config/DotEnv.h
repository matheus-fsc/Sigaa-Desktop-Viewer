#pragma once
// Carregador mínimo de .env.
//
// Precedência (do mais forte para o mais fraco):
//   1. variável de ambiente do processo
//   2. arquivo .env
//   3. prompt interativo
//
// Assim CI e scripts continuam mandando pelo ambiente, e o uso manual não
// precisa redigitar a senha toda hora.
//
// ⚠️ Um .env guarda a senha em TEXTO PURO no disco. É melhor que argumento de
// linha de comando (que vaza para o histórico e para `ps`), e pior que o
// keychain do sistema — que é o destino na Fase 2. O arquivo está no
// .gitignore; ainda assim, não o sincronize em nuvem nem o compartilhe.

#include <map>
#include <string>

namespace sigaa::config {

// Lê pares CHAVE=VALOR. Ignora linhas vazias e comentários (#).
// Aceita aspas simples ou duplas em volta do valor.
// Arquivo ausente não é erro — devolve mapa vazio.
std::map<std::string, std::string> carregarDotEnv(const std::string& caminho = ".env");

// Ambiente > .env > "". Nunca lê de argv.
std::string credencial(const std::string& chave,
                       const std::map<std::string, std::string>& dotenv);

} // namespace sigaa::config
