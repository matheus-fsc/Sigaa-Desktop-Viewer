#pragma once
// Credenciais guardadas no cofre do sistema operacional.
//
// ATÉ ONDE ISSO PROTEGE — e é importante não vender mais do que entrega:
//
//   PROTEGE de senha em texto puro num arquivo. Um `.env` vaza pelos caminhos
//   bobos, que são justamente os que acontecem: backup automático, pasta
//   sincronizada em nuvem, `git add -A` distraído, alguém olhando o diretório,
//   outra conta de usuário na mesma máquina. O cofre resolve tudo isso: no
//   Windows o blob é cifrado por DPAPI com chave derivada do logon do usuário.
//
//   NÃO PROTEGE de programa malicioso rodando NA SUA CONTA. Ele pede a
//   decifragem ao sistema exatamente como nós pedimos. Não existe truque que
//   evite isso: qualquer chave que o app consiga usar sozinho, sem você digitar
//   nada, um programa seu também consegue. Cifrar com uma chave embutida no
//   binário seria pior — a chave viajaria junto com o .exe.
//
//   O remédio para aquele cenário é senha-mestra digitada a cada abertura, e
//   isso quebraria a tarefa agendada, que roda sem ninguém por perto. A troca
//   está feita de propósito e documentada aqui para não ser esquecida.
//
// O QUE VAI PARA O COFRE: login E senha, juntos, dentro do blob cifrado. O CPF
// é dado pessoal (docs/RECON.md §4) e não merece ficar no campo de usuário em
// claro, onde `cmdkey /list` o mostraria para qualquer processo.

#include <optional>
#include <string>

namespace sigaa::plat {

struct Credenciais {
    std::string login;
    std::string senha;
};

// Nome legível do backend, para o `doctor` e a UI: "Gerenciador de Credenciais
// do Windows", "libsecret (secret-tool)", "Chaveiro do macOS", ou
// "nenhum (indisponivel)".
std::string backendCofre();

// Há cofre utilizável nesta máquina? No Linux depende de `secret-tool` estar
// instalado, então isso não é decidível em tempo de compilação.
bool cofreDisponivel();

// Grava (ou substitui). Devolve false e preenche `erro` em falha.
bool guardarNoCofre(const std::string& login, const std::string& senha,
                    std::string* erro = nullptr);

// nullopt = não há nada guardado (não é erro).
std::optional<Credenciais> lerDoCofre(std::string* erro = nullptr);

// Apagar o que não existe é sucesso — a pós-condição desejada já vale.
bool apagarDoCofre(std::string* erro = nullptr);

// --- resolução -------------------------------------------------------------

enum class Origem {
    Nenhuma,
    Ambiente,   // SIGAA_LOGIN / SIGAA_SENHA — automação e CI mandam mais
    Cofre,      // o caminho normal depois do onboarding
    DotEnv,     // legado; ainda aceito, mas o app sugere migrar
};

struct CredenciaisResolvidas {
    Credenciais cred;
    Origem origem{Origem::Nenhuma};
    bool ok() const { return !cred.login.empty() && !cred.senha.empty(); }
};

// Ambiente > cofre > .env.
//
// O ambiente vem primeiro para que CI e scripts continuem mandando sem depender
// de estado da máquina. O .env fica por último e só como legado: quem já tem um
// continua funcionando, e a UI oferece migrar para o cofre.
CredenciaisResolvidas resolverCredenciais(const std::string& caminhoDotEnv = ".env");

// Zera uma string na memória de forma que o compilador não possa otimizar.
// Mitigação parcial e assumida: std::string pode ter realocado antes.
void limparSegredo(std::string& s);

} // namespace sigaa::plat
