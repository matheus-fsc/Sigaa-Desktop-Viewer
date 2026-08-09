// Parte independente de SO: precedência e limpeza de memória.
// O acesso ao cofre em si está em Credenciais_win.cpp / Credenciais_posix.cpp.

#include "platform/Credenciais.h"

#include <cstdlib>

#include "core/config/DotEnv.h"

namespace sigaa::plat {

void limparSegredo(std::string& s) {
    volatile char* p = const_cast<volatile char*>(s.data());
    for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

CredenciaisResolvidas resolverCredenciais(const std::string& caminhoDotEnv) {
    CredenciaisResolvidas r;

    // 1. Ambiente. Vem primeiro para CI e scripts não dependerem do estado da
    //    máquina — e para dar uma saída de emergência se o cofre der problema.
    {
        const auto vazio = std::map<std::string, std::string>{};
        r.cred.login = config::credencial("SIGAA_LOGIN", vazio);
        r.cred.senha = config::credencial("SIGAA_SENHA", vazio);
        if (r.ok()) {
            r.origem = Origem::Ambiente;
            return r;
        }
        r.cred = {};
    }

    // 2. Cofre do SO — o caminho normal depois do onboarding.
    if (cofreDisponivel()) {
        if (auto c = lerDoCofre()) {
            r.cred = std::move(*c);
            r.origem = Origem::Cofre;
            return r;
        }
    }

    // 3. .env, legado. Continua funcionando para quem já tinha um; a UI oferece
    //    migrar para o cofre e apagar o arquivo.
    {
        const auto dotenv = config::carregarDotEnv(caminhoDotEnv);
        r.cred.login = config::credencial("SIGAA_LOGIN", dotenv);
        r.cred.senha = config::credencial("SIGAA_SENHA", dotenv);
        if (r.ok()) {
            r.origem = Origem::DotEnv;
            return r;
        }
        r.cred = {};
    }

    r.origem = Origem::Nenhuma;
    return r;
}

} // namespace sigaa::plat
