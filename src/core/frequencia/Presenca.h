#pragma once
// A presença que o ALUNO registra, nos dias em que o professor não registrou.
//
// O PROBLEMA. O mapa de frequência do SIGAA tem três situações por dia:
// "Presente", "N Falta(s)" e "Não Registrada". A terceira não quer dizer que o
// aluno faltou — quer dizer que o professor não abriu o diário naquele dia. O
// aluno esteve lá, e não tem onde dizer isso.
//
// Meses depois, quando o professor finalmente lança (ou não lança, e a
// secretaria pergunta), a memória de quem esteve em aula em 17 de agosto não
// vale nada sozinha. O registro do próprio aluno, feito no dia, vale.
//
// O QUE ISTO NÃO FAZ, e o ponto é importante: marcar presença AQUI não muda a
// contagem de faltas do SIGAA. Os totais continuam sendo os que o servidor
// calculou (ver core/parse/FrequenciaParser.h). Uma marcação é prova pessoal
// para uma conversa futura, não um jeito de melhorar o próprio número — e o
// app não pode sugerir o contrário, senão vira uma calculadora de mentira que
// o aluno consulta em vez do diário oficial.
//
// QUEM VENCE. A mesma regra das datas de prova (core/avaliacao/Ajustes.h): a
// última palavra é a do SIGAA. Enquanto o dia estiver "Não Registrada", vale a
// marcação do aluno. Quando o professor registra, a dele passa a valer — com
// alarme, e com a marcação preservada no histórico, porque é exatamente aí que
// ela pode ser necessária: o professor lançou falta num dia em que o aluno
// tinha registrado presença.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::frequencia {

// O que o aluno registrou para um dia. Chave: (idTurma, data).
struct Marcacao {
    std::string idTurma;
    DateTime data;

    // Presente ou Falta — o aluno também pode registrar que faltou, e é o
    // caso mais honesto de todos: ele sabe que faltou e quer o próprio
    // controle antes de o professor lançar.
    SituacaoDia situacao{SituacaoDia::Presente};
    int faltas{0};          // quando `situacao` é Falta
    std::string nota;       // "cheguei atrasado, o professor viu"

    // Falso = o professor registrou o dia e a marcação foi aposentada. Fica no
    // banco porque é justamente o registro que pode ser preciso depois.
    bool ativo{true};

    // O que o SIGAA dizia quando o aluno marcou — quase sempre NaoRegistrada.
    // É o campo que torna o conflito detectável, pelo mesmo motivo do
    // `quandoSigaaNaEpoca` das provas.
    SituacaoDia situacaoSigaaNaEpoca{SituacaoDia::NaoRegistrada};

    std::int64_t editadoEm{0};
};

enum class EstadoDia {
    DoSigaa,          // o professor registrou, e o aluno não tocou
    NaoRegistrada,    // o professor não registrou, e o aluno também não
    MarcadaPeloAluno, // o professor não registrou; vale o que o aluno disse
    Conflitada,       // o professor registrou DIFERENTE do que o aluno marcou
};

// Um dia pronto para mostrar.
struct DiaEfetivo {
    DateTime data;
    SituacaoDia situacao{SituacaoDia::NaoRegistrada};   // a que vale
    int faltas{0};
    EstadoDia estado{EstadoDia::DoSigaa};
    std::string nota;

    // O que o SIGAA diz hoje. Diferente de `situacao` só quando o aluno marcou
    // um dia que o SIGAA ainda não registrou.
    SituacaoDia situacaoSigaa{SituacaoDia::NaoRegistrada};
};

// Os dias do mapa com as marcações aplicadas, em ordem cronológica.
//
// UMA definição, para a aba de presença e para qualquer coisa que venha a
// mostrar esses dias. A contagem de faltas NÃO sai daqui — ela continua vindo
// dos totais do SIGAA.
std::vector<DiaEfetivo> efetivos(const Frequencia& f,
                                 const std::vector<Marcacao>& marcacoes);

// Quantas HORAS-AULA o aluno perde ao faltar o encontro do dia `data`.
//
// POR QUE NÃO É 2. Era, e estava errado: a unidade da frequência do SIGAA é a
// hora-aula, não o dia, e um encontro de laboratório com quatro horários
// seguidos ("6M2345") lança 4. Gravar 2 registrava METADE do dia — e o erro
// não era só cosmético: quando o professor lançasse as 4 de verdade, a
// marcação do aluno viraria "conflito" por um número que ele nunca escolheu,
// bem no dia em que ele precisava que o próprio registro batesse.
//
// A ORDEM DAS FONTES:
//   1. a grade horária da turma no dia da semana daquela data — é o número que
//      a universidade publicou, e é específico do dia;
//   2. o próprio mapa: as aulas que o SIGAA já registrou divididas pelos dias
//      em que registrou alguma coisa. Exato quando os encontros são iguais, e
//      serve para a reposição em dia que a grade não prevê;
//   3. 2, quando nada disso existe. É o encontro mais comum da grade e o menos
//      pior dos chutes — mas só depois de as duas fontes falharem.
int aulasDoEncontro(const Turma& turma, const Frequencia& f, const DateTime& data);

// Uma marcação que o professor atropelou.
struct Conflito {
    std::string idTurma;
    std::string turmaNome;
    DateTime data;
    SituacaoDia doAluno{};
    int faltasDoAluno{0};
    SituacaoDia doSigaa{};
    int faltasDoSigaa{0};
};

struct Reconciliacao {
    std::vector<Marcacao> marcacoes;   // a lista inteira, já com as aposentadas
    std::vector<Conflito> conflitos;
    bool mudou{false};
};

// Confronta as marcações com o mapa que o SIGAA acabou de trazer e aposenta as
// que ele registrou. Pura: não grava e não notifica.
//
// Rodar só com um mapa DE VERDADE (`Frequencia::temDados`): um mapa vazio
// porque a coleta não entrou na turma aposentaria tudo de uma vez.
Reconciliacao reconciliar(const Frequencia& f, std::vector<Marcacao> marcacoes);

// ---------------------------------------------------------------------------
// Histórico
// ---------------------------------------------------------------------------

enum class TipoMudanca {
    AlunoMarcou,      // o aluno registrou a própria presença ou falta
    AlunoDesfez,      // o aluno removeu a própria marcação
    SigaaRegistrou,   // o professor lançou o dia, concordando com o aluno
    SigaaAtropelou,   // o professor lançou DIFERENTE do que o aluno marcou
};

std::string_view toString(TipoMudanca t);

struct Mudanca {
    std::string idTurma;
    std::string turmaNome;
    DateTime data;
    TipoMudanca tipo{};
    std::string de;      // situação anterior, legível
    std::string para;
    std::string nota;
    std::int64_t quando{0};
};

// "Presente", "2 falta(s)", "não registrada" — para o histórico e para a tela.
std::string descrever(SituacaoDia s, int faltas);

} // namespace sigaa::frequencia
