#pragma once
// O que cai na prova: os tópicos de aula entre a prova anterior da turma e
// esta, e os arquivos ligados a eles.
//
// Mora no core, e não na UI onde nasceu, porque é a pergunta mais valiosa que
// um agente de IA pode fazer pelo MCP ("o que cai na P2 de compiladores?"), e
// a resposta precisa ser a MESMA que a "Próxima prova" da janela mostra.

#include <string>
#include <vector>

#include "core/avaliacao/Ajustes.h"
#include "core/model/Models.h"

namespace sigaa::avaliacao {

struct MateriaDaProva {
    bool coletada{false};            // a turma tem tópicos no banco?
    DateTime desde;                  // inválida = desde o início do período
    std::string provaAnterior;       // "Prova 1", quando `desde` é válida
    std::vector<std::string> topicos;
    std::vector<std::string> idsArquivos;
};

// `todas` em ordem cronológica, como `efetivas` devolve.
MateriaDaProva materiaDaProva(const Snapshot& s, const Efetiva& prova,
                              const std::vector<Efetiva>& todas);

} // namespace sigaa::avaliacao
