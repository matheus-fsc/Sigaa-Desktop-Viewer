#pragma once
// As rotas JSON que as páginas do celular leem (docs/WEB.md §4).
//
// Separado do servidor HTTP de propósito: aqui não há socket, cabeçalho nem
// token. Recebe um caminho e devolve uma resposta, e é assim que os testes o
// exercitam — sem porta aberta e sem esperar rede.
//
// SÓ LEITURA. Nenhuma rota grava no banco nem fala com o SIGAA. Um botão de
// "atualizar" no celular é fácil de apertar dez vezes no ônibus, e quem paga
// pelo bloqueio é a conta do aluno (ver DialogoOpcoes.h); quando a escrita
// vier, ela passa pelo mesmo orçamento do MCP, e não por um caminho próprio.
//
// Participantes não saem por aqui, como no MCP: dado de colega não deixa o
// computador por nenhuma porta.

#include <map>
#include <string>

namespace sigaa::web {

struct Resposta {
    int status{200};
    std::string tipo{"application/json; charset=utf-8"};
    std::string corpo;

    // Material baixado: o servidor transmite este arquivo em vez de `corpo`.
    std::string arquivo;
    std::string nomeArquivo;
};

struct Fonte {
    std::string banco;       // caminho absoluto do sigaa-viewer.db
    std::string materiais;   // pasta base dos materiais baixados
};

// `caminho` começa em "/api/" e não traz a query; `query` são os parâmetros.
Resposta responder(const Fonte& f, const std::string& caminho,
                   const std::map<std::string, std::string>& query);

} // namespace sigaa::web
