#pragma once
// Correções do aluno sobre as datas de prova que o SIGAA informa.
//
// O PROBLEMA QUE ISTO RESOLVE — e ele é do mundo real, não do software:
//
//   1. O professor remarca a prova em sala e não atualiza a plataforma. O
//      SIGAA continua anunciando a data velha, o app repete a data velha, e o
//      aluno estuda para o dia errado. Até aqui não havia como corrigir: o app
//      era um espelho do SIGAA e um espelho não discorda.
//
//   2. O professor anuncia a prova e NUNCA cadastra nada. Ela não existia no
//      app de jeito nenhum.
//
//   3. A avaliação inferida de um título de tópico ("Prova 1 - 20/09") não
//      tinha como ser confirmada nem corrigida. O app dizia "confirme com o
//      professor" e não oferecia onde registrar a resposta — então o aviso
//      reaparecia, idêntico, em todo sync, até virar ruído.
//
// O QUE UM AJUSTE É: uma camada FINA por cima do que o SIGAA disse, guardada
// à parte. O `Snapshot` continua sendo só a verdade do SIGAA — um ajuste nunca
// é gravado dentro dele. Isso é o que permite responder "o que o SIGAA diz
// hoje?" e "o que eu corrigi?" como duas perguntas separadas, que é
// exatamente o que o conflito exige.
//
// QUEM VENCE NO CONFLITO: o SIGAA.
//
//   Quando o professor finalmente atualiza a plataforma com uma data diferente
//   da que o aluno corrigiu, a data do SIGAA passa a valer e o ajuste é
//   aposentado — mas com alarme, nunca em silêncio. O ajuste aposentado NÃO é
//   apagado: fica no histórico, e é dele que sai a oferta de restaurar. A
//   alternativa (o ajuste vencer para sempre) tem um modo de falha pior:
//   o aluno corrige uma data por engano no começo do semestre e o app esconde
//   dele, até o fim, toda correção que o professor fizer depois.
//
// A CHAVE é (idTurma, descricao) — a mesma do banco e a mesma que o DiffEngine
// usa para decidir se uma prova é nova ou foi remarcada. Ter três chaves
// diferentes para a mesma prova seria a receita para o histórico falar de uma
// linha e a tabela mostrar outra.

#include <cstdint>
#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::avaliacao {

// A correção do aluno para uma prova, ou a prova inteira quando ele a criou.
struct Ajuste {
    std::string idTurma;
    std::string descricao;     // com idTurma, a chave

    // Só preenchidos quando `criadaPeloAluno`: uma prova que o SIGAA não
    // conhece não tem linha de onde herdar o nome da turma.
    std::string turmaNome;

    DateTime quando;           // a data que passa a valer
    std::string horarioBruto;  // "15:45", "6T34" — texto livre, como o do SIGAA
    std::string nota;          // "adiada em sala, confirmei com o monitor"

    bool criadaPeloAluno{false};
    bool confirmada{false};    // inferida que o aluno deu como boa

    // Falso = o SIGAA mudou a data depois desta correção e venceu. A linha
    // continua no banco porque é o histórico, e porque é dela que sai o botão
    // "restaurar minha data".
    bool ativo{true};

    // O que o SIGAA dizia NO MOMENTO da correção.
    //
    // É o campo que faz o conflito ser detectável. Sem ele só dá para saber
    // que a data do aluno difere da do SIGAA — o que é verdade desde o
    // primeiro segundo e não distingue "eu corrigi o professor" de "o
    // professor acabou de me corrigir".
    DateTime quandoSigaaNaEpoca;

    int64_t editadoEm{0};      // epoch, para ordenar o histórico
};

// Como uma prova chegou ao estado em que está. A UI pinta um distintivo por
// valor, e o .ics escreve uma frase por valor.
enum class Estado {
    DoSigaa,      // painel do professor, intocada
    Inferida,     // deduzida de um título de tópico, não confirmada
    Confirmada,   // inferida que o aluno confirmou
    Editada,      // o aluno corrigiu a data
    Criada,       // o aluno criou; o SIGAA não sabe que existe
    Conflitada,   // o aluno corrigiu, o SIGAA mudou depois e venceu
};

// Uma prova pronta para mostrar: a data que VALE, mais a procedência.
struct Efetiva {
    Avaliacao av;            // `quando` e `horarioBruto` já são os que valem
    Estado estado{Estado::DoSigaa};
    std::string nota;

    // O que o SIGAA diz hoje. Diferente de `av.quando` só nos casos em que o
    // aluno corrigiu — e é o que a tela mostra ao lado, para a correção poder
    // ser conferida em vez de ter que ser confiada.
    DateTime quandoSigaa;
};

// A lista final: mescla as fontes do SIGAA (painel vence tópico), aplica os
// ajustes ativos e acrescenta as provas criadas pelo aluno.
//
// UMA definição, usada pela tabela, pelo calendário, pelo .ics e pelas
// notificações. Se cada consumidor aplicasse os ajustes por conta própria, o
// .ics exportaria a data velha enquanto a tela mostra a nova — e o aluno
// confiaria no calendário do celular, que é onde ele realmente olha.
std::vector<Efetiva> efetivas(const std::vector<Avaliacao>& doSigaa,
                              const std::vector<Ajuste>& ajustes);

// Um ajuste que o SIGAA atropelou.
struct Conflito {
    std::string idTurma;
    std::string turmaNome;
    std::string descricao;
    DateTime doAluno;      // a correção aposentada
    DateTime doSigaaAntes;  // o que o SIGAA dizia quando o aluno corrigiu
    DateTime doSigaaAgora;  // o que ele passou a dizer
};

struct Reconciliacao {
    std::vector<Ajuste> ajustes;       // a lista inteira, já com os aposentados
    std::vector<Conflito> conflitos;   // o que precisa virar alarme
    bool mudou{false};                 // há o que gravar?
};

// Confronta os ajustes com o que o SIGAA traz agora e aposenta os que ele
// atropelou. Função PURA: não grava e não notifica — devolve o que deve ser
// gravado e o que deve ser anunciado, e quem chama decide.
//
// Rodar DEPOIS de salvar o snapshot novo, e só quando a coleta incluiu turmas:
// um sync que não entra nas turmas traz zero avaliações, e tratar isso como "o
// professor apagou a prova" aposentaria todos os ajustes de uma vez.
Reconciliacao reconciliar(const std::vector<Avaliacao>& doSigaa,
                          std::vector<Ajuste> ajustes);

// ---------------------------------------------------------------------------
// Histórico
// ---------------------------------------------------------------------------

enum class TipoMudanca {
    SigaaMarcou,        // a prova apareceu no SIGAA
    SigaaRemarcou,      // o professor mudou a data na plataforma
    AlunoCorrigiu,      // o aluno editou data/hora
    AlunoConfirmou,     // o aluno deu uma inferida como boa
    AlunoCriou,         // o aluno cadastrou uma prova que o SIGAA não tem
    AlunoDesfez,        // o aluno removeu a própria correção
    SigaaAtropelou,     // o SIGAA mudou e aposentou a correção do aluno
};

std::string_view toString(TipoMudanca t);

// Uma linha do histórico. Imutável depois de gravada: histórico que se reescreve
// não é histórico.
struct Mudanca {
    std::string idTurma;
    std::string turmaNome;
    std::string descricao;
    TipoMudanca tipo{};
    std::string de;      // ISO, ou "" quando não havia data antes
    std::string para;    // ISO
    std::string nota;
    int64_t quando{0};   // epoch de quando o app registrou
};

} // namespace sigaa::avaliacao
