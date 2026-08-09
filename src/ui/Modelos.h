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
#include <QMap>
#include <QString>
#include <Qt>

#include <vector>

class QObject;
class QStandardItemModel;

namespace sigaa {
struct ArquivoTurma;
struct DateTime;
struct Snapshot;
}

namespace sigaa::ui {

// Ordenação real da coluna (int ou string ISO), separada do texto mostrado.
inline constexpr int PapelOrdenacao = Qt::UserRole + 1;

// Cores semânticas da apresentação.
//
// São as ÚNICAS cores literais do app — `estilo.qss` não tem nenhuma, por
// regra. Estas ficam aqui porque significam algo ("isto está atrasado") em vez
// de decorar, e a paleta do sistema não tem papel para "urgente". São tons
// médios de propósito: legíveis sobre fundo claro e escuro, já que o Qt não
// avisa quando o Windows troca de tema.
//
// Ficam num só lugar porque a tabela de provas e o calendário precisam
// concordar: um ponto laranja no dia 4 e uma linha cinza para a mesma prova
// seriam duas respostas diferentes para a mesma pergunta.
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
QStandardItemModel* modeloProvas(const Snapshot& s, QObject* pai);

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
QMap<QDate, DiaComProva> provasPorDia(const Snapshot& s);

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

ResumoProvas resumoProvas(const Snapshot& s);

// --- turma -----------------------------------------------------------------

// Colunas: Turma | Horário | Local | Período
QStandardItemModel* modeloTurmas(const Snapshot& s, QObject* pai);

// Colunas: Arquivo | Tópico de aula
// O `idArquivo` viaja em PapelIdArquivo, não numa coluna: é chave de protocolo,
// não informação para o aluno — mostrá-la só ocuparia largura.
inline constexpr int PapelIdArquivo = Qt::UserRole + 2;
QStandardItemModel* modeloArquivos(const std::vector<ArquivoTurma>& arquivos, QObject* pai);

} // namespace sigaa::ui
