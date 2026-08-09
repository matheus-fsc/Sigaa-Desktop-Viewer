#pragma once
// Leitura de credenciais no terminal.
//
// A senha NUNCA vem por argumento de linha de comando: `argv` fica no
// histórico do shell e é visível para qualquer processo da máquina
// (`ps`, Get-Process, /proc/<pid>/cmdline). As duas vias aceitas são
// variável de ambiente (para automação) e prompt interativo com eco
// desligado (para uso manual).

#include <string>

namespace sigaa::app {

// Lê uma linha com eco visível.
std::string lerLinha(const std::string& rotulo);

// Lê uma linha SEM eco. Em terminal não interativo, cai para leitura normal
// e avisa no stderr — melhor funcionar com aviso do que travar num pipe.
std::string lerSenha(const std::string& rotulo);

// Zera o conteúdo de uma string de forma que o compilador não possa otimizar.
// Mitigação parcial: std::string pode ter realocado antes. Não substitui um
// keychain — é o que dá para fazer na Fase 1.
void limpar(std::string& s);

} // namespace sigaa::app
