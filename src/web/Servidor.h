#pragma once
// O servidor HTTP do acesso mobile (docs/WEB.md).
//
// Leve de propósito: cpp-httplib, um cabeçalho só, quatro threads, só GET
// (mais o POST do pareamento).
// Serve as páginas embutidas (Paginas.h) e as rotas de Api.h. TLS fica de
// fora: quem cifra é a VPN, e quem quiser HTTPS põe `tailscale serve`, Caddy
// ou parecido na frente, escutando em 127.0.0.1.
//
// Sem Qt: roda dentro da janela (aba Acesso mobile) e no `sigaa-cli web`.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "web/Api.h"

namespace sigaa::web {

struct Config {
    std::string host{"127.0.0.1"};
    int porta{8765};
    Fonte fonte;
};

// Um pedido atendido, para a lista "Últimos acessos" da aba. Nunca traz o
// token nem a query.
struct Acesso {
    std::int64_t quando{0};
    std::string ip;
    std::string metodo;
    std::string caminho;
    int status{0};
    std::string aparelho;   // nome do aparelho pareado; vazio se recusado
};

class Servidor {
public:
    explicit Servidor(Config c);
    ~Servidor();   // para, se estiver rodando
    Servidor(const Servidor&) = delete;
    Servidor& operator=(const Servidor&) = delete;

    // Abre a porta AQUI, na thread de quem chama, para o erro ("porta em
    // uso", "endereço não existe") voltar como resposta e não como silêncio;
    // depois atende numa thread própria. Recusa 0.0.0.0 (Pareamento.h).
    bool iniciar(std::string* erro = nullptr);
    void parar();
    bool rodando() const;

    const Config& config() const;

    // Chamado da thread do servidor, um por pedido. Quem mexe em interface
    // deve repassar para a própria thread.
    std::function<void(const Acesso&)> aoAcessar;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sigaa::web
