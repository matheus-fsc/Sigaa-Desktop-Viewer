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

#include "core/avaliacao/Ajustes.h"
#include "core/frequencia/Presenca.h"
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

    // Quantas pessoas ja estao guardadas na tabela `participante`.
    //
    // Serve para decidir, ANTES de coletar, se vale pagar uma requisicao por
    // turma para abrir a aba Participantes: zero significa banco limpo, e essa
    // e a unica coleta em que a lista precisa vir inteira. Depois disso quem
    // reatualiza e a janela da turma, so na turma que o aluno abriu.
    int participantesGuardados();

    // --- correções do aluno sobre datas de prova ---------------------------
    //
    // Vivem à parte do `Snapshot`, e é o ponto do desenho: o snapshot é o que
    // o SIGAA disse, e um sync reescreve a tabela `avaliacao` inteira. Uma
    // correção guardada lá seria apagada pelo ciclo seguinte, sem aviso.

    std::vector<avaliacao::Ajuste> carregarAjustes();

    // Grava um ajuste (upsert por (idTurma, descricao)) e devolve se deu certo.
    bool gravarAjuste(const avaliacao::Ajuste& a);

    // Substitui a lista inteira de ajustes. Usado pela reconciliação, que
    // decide de uma vez quais foram atropelados pelo SIGAA.
    bool gravarAjustes(const std::vector<avaliacao::Ajuste>& as);

    // Apaga a correção — o "desfazer". O histórico NÃO é apagado junto: é o
    // registro de que a correção existiu, e é o que explica, em outubro, por
    // que a data mudou duas vezes em agosto.
    bool removerAjuste(const std::string& idTurma, const std::string& descricao);

    // --- histórico ---------------------------------------------------------

    bool registrarMudanca(const avaliacao::Mudanca& m);

    // --- presença marcada pelo aluno ---------------------------------------
    //
    // Mesma separação das correções de prova, e pelo mesmo motivo: a linha de
    // `frequencia_dia` é reescrita a cada sync.

    std::vector<frequencia::Marcacao> carregarMarcacoes(const std::string& idTurma = {});
    bool gravarMarcacao(const frequencia::Marcacao& m);
    bool gravarMarcacoes(const std::vector<frequencia::Marcacao>& ms);
    bool removerMarcacao(const std::string& idTurma, const std::string& dataIso);

    bool registrarMudancaPresenca(const frequencia::Mudanca& m);
    std::vector<frequencia::Mudanca> historicoPresenca(const std::string& idTurma = {},
                                                       int limite = 200);

    // Mais recentes primeiro. Sem argumentos, o histórico de todas as provas;
    // com `idTurma`/`descricao`, o de uma só.
    std::vector<avaliacao::Mudanca> historico(const std::string& idTurma = {},
                                              const std::string& descricao = {},
                                              int limite = 200);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sigaa::store
