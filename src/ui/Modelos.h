#pragma once
// Snapshot -> modelos de tabela do Qt.
//
// Camada de apresentação pura: lê `sigaa::Snapshot` e devolve QStandardItemModel
// pronto para uma QTableView. Nada aqui fala com a rede nem com o banco.
//
// Cada coluna guarda uma chave de ordenação em `PapelOrdenacao`, e não confia
// no texto exibido. "em 3 dias" e "atrasado 2 dias" ordenam corretamente por
// urgência; ordenar pelo texto colocaria "atrasado" antes de "amanhã" só porque
// 'a' vem antes de 'a'... e "em 10 dias" antes de "em 3 dias".

#include <QColor>
#include <QDate>
#include <QHash>
#include <QMap>
#include <QString>
#include <Qt>

#include <vector>

class QObject;
class QStandardItemModel;

#include "core/avaliacao/Ajustes.h"
#include "core/frequencia/Presenca.h"

namespace sigaa {
struct ArquivoTurma;
struct DateTime;
struct Participante;
struct Snapshot;
struct TopicoAula;
}

namespace sigaa::ui {

// Ordenação real da coluna (int ou string ISO), separada do texto mostrado.
inline constexpr int PapelOrdenacao = Qt::UserRole + 1;

// Cores semânticas da apresentação.
//
// Existem porque significam algo ("isto está atrasado") em vez de decorar, e a
// paleta do sistema não tem papel para "urgente".
//
// Ficam num só lugar porque a tabela de provas e o calendário precisam
// concordar: um ponto laranja no dia 4 e uma linha cinza para a mesma prova
// seriam duas respostas diferentes para a mesma pergunta.
//
// O TOM não é decidido aqui: estas funções repassam a ui/Tema.h, que tem um
// vermelho para o tema claro e outro para o escuro. Antes eram tons médios
// cravados, escolhidos para serem sofríveis nos dois — o tema acabou com essa
// concessão.
namespace cor {
QColor atrasado();    // prazo vencido
QColor urgente();     // vence em até 2 dias / prova nos próximos 7
QColor inferido();    // dado deduzido, não cadastrado pelo professor
QColor apagado();     // concluído, ou já passou — presente mas sem peso
} // namespace cor

// "12/08/2026" ou "12/08/2026 23:59". Vazio se a data for inválida.
QString formatarData(const DateTime& d);

// Dias do hoje até a data. Negativo = já passou.
// Data inválida devolve `semPrazo`, um valor que ordena por último de propósito:
// atividade sem prazo não é urgente, e jogá-la para o topo por acidente
// esconderia a que vence amanhã.
// A partir de quantos dias de atraso uma entrega sai da lista principal.
//
// Uma semana: atrasado ha dois dias ainda e assunto — da para pedir prorrogacao,
// da para entregar fora do prazo. Ha tres semanas nao volta, e ocupar a primeira
// tela com isso empurra para baixo o que vence amanha.
inline constexpr int kDiasParaHistorico = 7;

inline constexpr int semPrazo = 1'000'000;
int diasAte(const DateTime& d);

// "vence hoje", "amanhã", "em 3 dias", "atrasado 2 dias", "sem prazo".
QString prazoRelativo(const DateTime& d);

// Colunas: Prazo | Vence | Turma | Atividade | Tipo
QStandardItemModel* modeloPrazos(const Snapshot& s, QObject* pai);

// Colunas: Data | Turma | Avaliação | Horário | Origem
// A coluna Origem existe porque as duas fontes não merecem a mesma confiança:
// o painel do professor é dado cadastrado, o tópico de aula é regex sobre
// título livre. Esconder essa diferença faria o aluno estudar para a data
// errada sem desconfiar (RECON §1.6).
// Recebe a lista JÁ RESOLVIDA (`avaliacao::efetivas`), não o Snapshot cru.
//
// É o que garante que a tabela, o calendário, o .ics e as notificações falem
// da mesma data: se cada um aplicasse as correções do aluno por conta própria,
// o .ics exportaria a data velha enquanto a tela mostra a nova — e o aluno
// confia no calendário do celular, que é onde ele realmente olha.
QStandardItemModel* modeloProvas(const std::vector<avaliacao::Efetiva>& provas,
                                 QObject* pai);

// A chave da prova viaja nos itens da linha, para o botão de corrigir saber
// sobre qual delas o aluno clicou. Fora da faixa dos outros papéis.
// A coluna "SIGAA diz" da tabela de provas. Nomeada porque a janela precisa
// escondê-la quando ninguém discorda, e um `5` solto no meio da JanelaPrincipal
// vira um mistério no dia em que uma coluna for inserida antes dela.
inline constexpr int kColunaSigaaDiz = 5;

inline constexpr int PapelIdTurmaProva = Qt::UserRole + 20;
inline constexpr int PapelDescricaoProva = Qt::UserRole + 21;
inline constexpr int PapelEstadoProva = Qt::UserRole + 22;

// Colunas: Data | Turma | Atualização
QStandardItemModel* modeloAtualizacoes(const Snapshot& s, QObject* pai);

// --- dashboard de provas ---------------------------------------------------

// Quantas provas caem num dia, separadas por confiança. A distinção sobrevive
// até o desenho: o calendário pinta ponto cheio para o que o professor
// cadastrou e ponto vazado para o que nós deduzimos, porque estudar para uma
// data errada custa caro e o aluno merece ver a diferença sem abrir a tabela.
struct DiaComProva {
    int confirmadas{0};
    int inferidas{0};
    int total() const { return confirmadas + inferidas; }
};

// Chaveado por dia, já com as duas fontes mescladas (mesma regra do .ics).
QMap<QDate, DiaComProva> provasPorDia(const std::vector<avaliacao::Efetiva>& provas);

struct ResumoProvas {
    int total{0};
    int proximos30{0};
    int inferidas{0};

    // Próxima prova ainda por vir. `data` inválida = não há nenhuma à frente.
    QDate data;
    int emDias{0};
    QString descricao;
    QString turma;
};

ResumoProvas resumoProvas(const std::vector<avaliacao::Efetiva>& provas);

// --- a agenda --------------------------------------------------------------

// As aulas de uma faixa de dias, com o material pendurado em cada uma.
//
// É a pergunta que o aluno tem ao abrir o app de manhã, e a que o SIGAA
// responde pior: lá é preciso entrar em cada turma para descobrir o que caiu na
// aula de hoje. Aqui as duas fontes já estão no banco.
//
// A faixa é um PARÂMETRO, e não "hoje e amanhã" cravado, porque o semestre
// inteiro já está no banco depois do primeiro ciclo com turmas: recortar em
// dois dias jogava fora dado que custou ~15 requisições para coletar, e obrigava
// o aluno a voltar ao SIGAA justamente para a pergunta que o app deveria
// responder melhor ("o que tem semana que vem?"). Quem chama pagina.
//
// Colunas: Aula | Turma | Material
// A linha da aula guarda o `idTurma` em PapelIdTurma, para o duplo clique
// abrir a turma certa. A linha de GRUPO (o dia) guarda em PapelOrdenacao
// quantas aulas caem nele — é o que permite a janela expandir só os dias com
// aula sem reabrir o modelo para contar.
inline constexpr int PapelIdTurma = Qt::UserRole + 4;

struct ResumoDia {
    int aulasHoje{0};
    int aulasAmanha{0};
    // Nenhum tópico no banco — provável que nunca tenha rodado um sync com
    // `--turmas`. É diferente de "não há aula hoje", e a tela precisa saber
    // distinguir para não afirmar o que não sabe.
    bool semDados{true};
};

ResumoDia resumoDia(const Snapshot& s, QDate hoje);

// Uma aula na agenda de um dia.
//
// A FONTE PRIMÁRIA É A GRADE HORÁRIA DA TURMA, e não o tópico registrado pelo
// professor. A diferença apareceu na prática: Compiladores tem aula toda
// segunda e quarta ("24T34"), mas o professor não publicou tópico nenhum na
// Turma Virtual — e a agenda, que só desenhava tópicos, dizia que a quarta-feira
// tinha duas aulas quando tinha três. O aluno confia nessa contagem para saber
// se precisa ir à faculdade; ela não pode depender de o professor ter
// preenchido a linha do tempo.
//
// O tópico continua sendo o CONTEÚDO daquela aula, quando existe. Quando não
// existe, a aula aparece assim mesmo, dizendo que não há tópico — que é
// informação diferente de "não há aula".
struct AulaDoDia {
    QString idTurma;
    QString turma;
    QString horario;    // código do turno, "T34"; vazio se ilegível
    QString titulo;     // título do tópico, ou vazio
    QString conteudo;   // para o tooltip
    int materiais{0};
    int ordem{0};       // manhã < tarde < noite, depois pelo horário
    bool semTopico{false};
    // A turma não tem aula neste dia da semana, mas o professor datou um tópico
    // aqui: reposição, prova, semana especial. Nunca é escondido.
    bool foraDaGrade{false};
};

// As aulas de um dia, já ordenadas por turno/horário.
std::vector<AulaDoDia> aulasDoDia(const Snapshot& s, QDate dia);

// Quantas aulas caem em [inicio, fim], inclusive nas duas pontas.
int aulasEntre(const Snapshot& s, QDate inicio, QDate fim);

// O primeiro e o último dia que a coleta conhece. É o que dá FIM à paginação:
// sem isto, o botão "semana seguinte" continuaria clicável para sempre, e a
// pessoa pagina em direção a um vazio que não é ausência de aula, é ausência
// de dado — a pior confusão que esta tela pode causar.
//
// Datas inválidas = nenhum tópico coletado ainda.
struct FaixaAgenda {
    QDate primeiro;
    QDate ultimo;
    bool valida() const { return primeiro.isValid() && ultimo.isValid(); }
};

FaixaAgenda faixaAgenda(const Snapshot& s);

// Um grupo por dia em [inicio, fim], inclusive. `hoje` só decide o destaque —
// a faixa não precisa conter o dia de hoje.
QStandardItemModel* modeloAgenda(const Snapshot& s, QDate inicio, QDate fim, QDate hoje,
                                 QObject* pai);

// --- turma -----------------------------------------------------------------

// Colunas: Turma | Horário | Local | Período
QStandardItemModel* modeloTurmas(const Snapshot& s, QObject* pai);

// Colunas: Arquivo | Tópico de aula
// O `idArquivo` viaja em PapelIdArquivo, não numa coluna: é chave de protocolo,
// não informação para o aluno — mostrá-la só ocuparia largura.
inline constexpr int PapelIdArquivo = Qt::UserRole + 2;
QStandardItemModel* modeloArquivos(const std::vector<ArquivoTurma>& arquivos, QObject* pai);

// Colunas: Papel | Nome | Curso / Departamento | Matrícula | E-mail
//
// Uma coluna só para "Curso / Departamento" porque os dois papéis nunca
// preenchem os dois campos: o docente traz departamento, o discente traz
// curso. Duas colunas deixariam metade de cada uma vazia em toda turma.
//
// A ordenação vem de PapelOrdenacao com o papel na frente do nome, e não do
// texto: ordenar pela coluna Nome jogaria o professor para o meio da lista
// alfabética dos colegas, e ele é quem a pessoa procura primeiro.
//
// A célula do nome mostra as INICIAIS num quadro colorido. Já mostrou o
// retrato de verdade; a foto saiu em 18/09/2026 junto com a coleta que a
// alimentava (ver `avatarDe` em Modelos.cpp).
QStandardItemModel* modeloParticipantes(const std::vector<Participante>& participantes,
                                        QObject* pai);

// Colunas: Data | Situação | Origem
//
// A tabela do mapa de frequência do SIGAA, mais o que o aluno registrou nos
// dias em branco. A coluna Origem distingue as três procedências — o professor,
// o aluno, e o conflito entre os dois — porque é dela que depende o valor da
// aba: uma marcação só serve de prova se der para ver que ela é uma marcação.
QStandardItemModel* modeloPresenca(const std::vector<frequencia::DiaEfetivo>& dias,
                                   QObject* pai);

// A data ISO do dia viaja na linha, para o botão de marcar saber sobre qual
// dia o aluno clicou sem depender da ordem da tabela.
inline constexpr int PapelDataDia = Qt::UserRole + 30;

// A árvore de aulas: cada tópico com os materiais pendurados nele.
//
// É a tela que o aluno abre no SIGAA — a Turma Virtual entra pela linha do
// tempo das aulas, não por uma lista de arquivos. Uma lista plana de PDFs
// responde "o que existe"; a árvore responde "o que caiu na aula do dia 18",
// que é a pergunta que ele tinha.
//
// Duas fontes se encontram aqui, e o casamento é por `id`:
//   - `topicos[].materiais` — o que está pendurado na aula (RECON §1.6.2)
//   - `arquivos`            — a aba Arquivos, cuja coluna "Tópico de Aula"
//                             casa com o título do tópico (§1.6.1)
//
// Um material só é BAIXÁVEL se o id dele aparece na aba Arquivos. Deduzir pelo
// tipo do ícone seria chutar: uma tarefa e um fórum também são materiais de
// tópico, e oferecer download deles daria erro na cara do aluno.
//
// Arquivo cujo tópico não casa com nenhuma aula não some — vai para um grupo
// no fim. Sumir seria pior que qualquer desarrumação: é material que existe.
//
// Colunas: Aula / material | Quando | Offline
//
// Linha de aula não tem PapelIdArquivo; linha de material tem. É assim que a
// janela sabe o que fazer quando alguém seleciona uma aula inteira e clica em
// Baixar: pega os filhos baixáveis, em vez de reclamar da seleção.
inline constexpr int PapelBaixavel = Qt::UserRole + 3;

// `offline` mapeia idArquivo -> caminho local do que já está no disco. É um
// mapa, e não um conjunto, porque a mesma informação responde às duas
// perguntas da tela: "mostrar ✓?" e "abrir qual arquivo?".
QStandardItemModel* modeloAulas(const std::vector<TopicoAula>& topicos,
                                const std::vector<ArquivoTurma>& arquivos,
                                const QHash<QString, QString>& offline, QObject* pai);

} // namespace sigaa::ui
