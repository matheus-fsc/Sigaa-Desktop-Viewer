#pragma once
// Persistência local (SQLite).
//
// Guarda o último snapshot conhecido de cada coisa, com `primeiro_visto` e
// `ultimo_visto`. É isso que permite responder "o que mudou desde a última
// verificação?" — a pergunta que o app existe para responder.
//
// O arquivo do banco NÃO guarda credenciais. Só dados acadêmicos já públicos
// para o próprio aluno.

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/avaliacao/Ajustes.h"
#include "core/planejamento/Planejamento.h"
#include "core/estudo/Registros.h"
#include "core/frequencia/Presenca.h"
#include "core/model/Models.h"

namespace sigaa::store {

class Database {
public:
    enum class Abertura {
        CriarSeFaltar,   // a UI e o CLI: primeira execução cria o banco
        SoExistente,     // o servidor MCP: caminho errado é erro, não banco vazio
    };

    explicit Database(const std::string& caminho = "sigaa-viewer.db",
                      Abertura modo = Abertura::CriarSeFaltar);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool aberto() const;
    const std::string& erro() const;

    // Cria/atualiza o esquema. Idempotente.
    bool migrar();

    // Chave/valor livre da tabela `meta` (contador de ciclos, consentimento
    // do MCP...). nullopt = chave ausente, que é diferente de valor vazio.
    std::optional<std::string> lerMeta(const std::string& chave);
    bool gravarMeta(const std::string& chave, const std::string& valor);

    // Auditoria do servidor MCP: um registro por chamada de ferramenta ou
    // leitura de recurso. Só o pedido, nunca o conteúdo devolvido.
    bool registrarAcessoMcp(std::int64_t quando, const std::string& origem,
                            const std::string& ferramenta, const std::string& turma, bool ok,
                            const std::string& motivo = {});

    struct AcessoMcp {
        std::int64_t quando{0};
        std::string origem;
        std::string ferramenta;
        std::string turma;
        bool ok{false};
        std::string motivo;   // por que foi recusado; vazio quando ok
    };
    // Os mais recentes primeiro.
    std::vector<AcessoMcp> ultimosAcessosMcp(int limite = 50);

    // --- acesso mobile (docs/WEB.md §3) ---------------------------------
    struct DispositivoWeb {
        std::int64_t id{0};
        std::string nome;          // do navegador do celular: texto de terceiro
        std::string via;           // "qr" ou "pin"
        std::int64_t criadoEm{0};
        std::int64_t ultimoAcesso{0};
        std::string ultimoIp;
    };
    // `tokenHash` é o SHA-256 do token do aparelho; o token em si não é gravado.
    // Devolve o id, ou 0 se falhou.
    std::int64_t criarDispositivoWeb(const std::string& tokenHash, const std::string& nome,
                                     const std::string& via, const std::string& ip,
                                     std::int64_t agora);
    std::optional<DispositivoWeb> dispositivoWebPorToken(const std::string& tokenHash);
    bool tocarDispositivoWeb(std::int64_t id, const std::string& ip, std::int64_t agora);
    // O usado por último primeiro.
    std::vector<DispositivoWeb> dispositivosWeb();
    // Um aparelho (`id`), ou todos com `id` = 0. Devolve quantos saíram.
    int removerDispositivosWeb(std::int64_t id);

    // --- devolução do agente de IA (core/estudo/Registros.h) --------------
    //
    // Os `registrar*` devolvem o id novo, ou 0 em falha (com `erro()`).
    std::int64_t registrarEstudo(const estudo::RegistroEstudo& r);
    std::int64_t registrarDesempenho(const estudo::Desempenho& d);
    // Atualiza o ponto ABERTO do mesmo tópico (sem acento e caixa) em vez de
    // duplicar; `atualizou` diz qual dos dois aconteceu.
    std::int64_t marcarFoco(const estudo::PontoFoco& f, bool* atualizou = nullptr);
    // Falso se o id não existe ou já estava resolvido.
    bool resolverFoco(std::int64_t id, const std::string& motivo, std::int64_t agora);

    // Vazio = todas as turmas. Mais recentes primeiro.
    std::vector<estudo::RegistroEstudo> carregarRegistrosEstudo(const std::string& idTurma = {});
    std::vector<estudo::Desempenho> carregarDesempenho(const std::string& idTurma = {});
    // Abertos primeiro, do nível mais alto para o mais baixo.
    std::vector<estudo::PontoFoco> carregarFocos(const std::string& idTurma = {},
                                                 bool soAbertos = false);

    // Falso se o id não existe ou não estava resolvido. É o "Desfazer" do
    // "Já domino".
    bool reabrirFoco(std::int64_t id, std::int64_t agora);

    // Propostas de mudança no plano (mcp/Propostas.h). Mais recentes primeiro.
    std::int64_t inserirProposta(const estudo::Proposta& p);
    std::vector<estudo::Proposta> carregarPropostas();
    // Grava estado, valor final (`para`), `ref`, `respondidaEm` e `motivo`.
    bool responderProposta(const estudo::Proposta& p);

    enum class TabelaAgente { Estudo, Desempenho, Foco, Proposta };
    bool apagarDoAgente(TabelaAgente t, std::int64_t id);
    // Tudo que uma origem gravou; origem vazia = de todos os agentes.
    // Devolve quantas linhas saíram.
    int apagarTudoDoAgente(const std::string& origem);

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

    // Chaves (parse::chaveNoticia) das notícias que JÁ TÊM texto no banco.
    //
    // A coleta consulta antes de entrar nas turmas: o texto de cada notícia
    // custa uma requisição, e buscar de novo o que já está guardado seria
    // pagar por nada a cada ciclo.
    std::set<std::string> chavesNoticiasComTexto();

    // As notícias de uma turma, da mais nova para a mais antiga.
    std::vector<Noticia> carregarNoticias(const std::string& idTurma);

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

    // --- planejamento de estudo -------------------------------------------
    //
    // Dado do aluno, à parte do snapshot como os ajustes. Ver
    // core/planejamento/Planejamento.h para o que o plano é.

    // Sem nada gravado, os padrões de `Preferencias`.
    planejamento::Preferencias carregarPreferenciasEstudo();
    bool gravarPreferenciasEstudo(const planejamento::Preferencias& p);

    std::vector<planejamento::Sessao> carregarSessoesEstudo();
    // Grava o plano recalculado: apaga as pendentes e insere `ss`. As feitas
    // nunca são apagadas, nem desmarcadas por um replano. Com `desde`
    // ("aaaa-mm-dd", o hoje), as pendentes de antes dele ficam: o que foi
    // planejado e não feito é a medida de quanto o aluno seguiu o plano.
    bool substituirSessoesEstudo(const std::vector<planejamento::Sessao>& ss,
                                 const std::string& desde = {});
    // O check do aluno. Falso se a chave não existe.
    bool marcarSessaoEstudo(const std::string& chave, bool feita, std::int64_t agora);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sigaa::store
