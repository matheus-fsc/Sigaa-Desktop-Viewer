#pragma once
// Persistência local (SQLite).
//
// Guarda o último snapshot conhecido de cada coisa, com `primeiro_visto` e
// `ultimo_visto`. É isso que permite responder "o que mudou desde a última
// verificação?" — a pergunta que o app existe para responder.
//
// O arquivo do banco NÃO guarda credenciais. Só dados acadêmicos já públicos
// para o próprio aluno.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::store {

class Database {
public:
    explicit Database(const std::string& caminho = "sigaa-viewer.db");
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool aberto() const;
    const std::string& erro() const;

    // Cria/atualiza o esquema. Idempotente.
    bool migrar();

    // Lê o último estado conhecido — a base de comparação do diff.
    Snapshot carregarUltimo();

    // Grava o snapshot novo (upsert), marcando `ultimo_visto`.
    bool gravar(const Snapshot& s, std::int64_t agora);

    // Quantos ciclos de sync já rodaram. Zero = primeira execução, e nesse
    // caso o diff não deve gritar "tudo é novidade".
    int ciclos();
    void registrarCiclo(std::int64_t agora);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sigaa::store
