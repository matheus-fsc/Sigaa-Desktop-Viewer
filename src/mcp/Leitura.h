#pragma once
// As ferramentas de leitura do servidor MCP (docs/MCP.md §4): o que o agente
// de IA pode perguntar sobre as turmas do aluno.
//
// Cada ferramenta lê o banco que a UI e o CLI já preenchem e responde em dois
// formatos: `dados` (JSON, vira o `structuredContent` do MCP) e `texto`
// (Markdown curto, para clientes que só leem texto). O conteúdo escrito pelo
// professor — notícias, tópicos — vai em CAMPOS, nunca colado numa frase de
// instrução: é texto de terceiro e pode conter qualquer coisa (§7).
//
// Nada aqui fala com o SIGAA. Nada aqui lê participantes: dado de colega não
// sai do computador por esta porta, com ou sem consentimento.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/avaliacao/Ajustes.h"
#include "core/model/Models.h"
#include "core/store/Database.h"

namespace sigaa::mcp {

// O que o aluno liberou, por categoria (D4). Guardado em `meta` como
// "mcp.<nome>" = "1".
enum class Permissao { Leitura, Arquivos, Escrita, Rede };

const char* nomePermissao(Permissao p);           // "leitura", "arquivos"...
std::optional<Permissao> permissaoPorNome(const std::string& nome);
bool permitido(store::Database& db, Permissao p);

// O que uma chamada enxerga. O banco é aberto a cada pedido, e não uma vez no
// início: o aluno pode sincronizar ou mudar uma permissão na UI com o agente
// aberto, e a resposta seguinte já tem de refletir isso.
class Contexto {
public:
    Contexto(store::Database& db, std::string materiais, std::string cliente);

    store::Database& db() { return db_; }
    const std::string& materiais() const { return materiais_; }
    const std::string& cliente() const { return cliente_; }

    const Snapshot& snapshot();
    // Efetivas: SIGAA + correções do aluno, em ordem cronológica.
    const std::vector<avaliacao::Efetiva>& provas();
    bool permite(Permissao p) { return permitido(db_, p); }

    // A turma da última chamada, para a auditoria.
    std::string turmaUsada;

private:
    store::Database& db_;
    std::string materiais_;
    std::string cliente_;
    std::optional<Snapshot> snapshot_;
    std::optional<std::vector<avaliacao::Efetiva>> provas_;
};

struct Resultado {
    std::string texto;                        // Markdown
    nlohmann::json dados;                     // objeto, ou null
    std::vector<nlohmann::json> extras;       // resource_link etc.
    bool erro{false};

    static Resultado falha(std::string msg) {
        Resultado r;
        r.texto = std::move(msg);
        r.erro = true;
        return r;
    }
};

struct Ferramenta {
    std::string nome;
    std::string titulo;
    std::string descricao;
    nlohmann::json entrada;   // JSON Schema dos argumentos
    std::function<Resultado(Contexto&, const nlohmann::json&)> rodar;
    Permissao exige{Permissao::Leitura};
    bool somenteLeitura{true};   // vira a anotação readOnlyHint
};

const std::vector<Ferramenta>& ferramentasDeLeitura();

// Turma por id, código ou trecho do nome (sem acento e sem caixa, D5).
// Nenhuma ou mais de uma: `turma` nulo e `erro` diz quais existem.
struct EscolhaTurma {
    const Turma* turma{nullptr};
    std::string erro;
};
EscolhaTurma acharTurma(const Snapshot& s, const std::string& consulta);

// O `turma.md` da turma (core/report/TurmaMd), o mesmo do botão Resumo .md.
std::string resumoDaTurma(Contexto& c, const Turma& t);

// Caminho absoluto do material no disco, ou "" se não foi baixado.
std::string caminhoDoArquivo(Contexto& c, const ArquivoTurma& a);

// "application/pdf" pela extensão; "application/octet-stream" no resto.
std::string tipoMime(const std::string& nomeArquivo);

} // namespace sigaa::mcp
