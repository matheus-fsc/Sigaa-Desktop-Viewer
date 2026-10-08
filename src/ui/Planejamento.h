#pragma once
// A aba Estudo: um menu lateral à esquerda e, à direita, a página escolhida.
//
//   - Planejamento: onde o semestre aperta, o que fazer hoje e na semana, e
//     o preparo para as provas.
//   - Progresso: o que os agentes de IA devolveram e as mudanças que eles
//     propõem no plano (ui/Progresso.h).
//   - Horas e dificuldade: o tempo disponível por dia da semana, com as aulas
//     da grade já descontadas, e o peso de cada matéria.
//   - Agentes de IA: permissões, conexão e atividade (ui/Agentes.h).
//
// POR QUE UMA ABA, E NÃO UM DIÁLOGO: o plano é para consultar todo dia, não
// uma tela que se abre uma vez. E o menu lateral deixa lugar para o que mais
// vier do estudo — histórico de sessões, metas — sem abrir outra aba no topo.
//
// A conta é toda do núcleo (core/planejamento). Estes painéis só leem o
// banco, pedem o plano, gravam e mostram.

#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/planejamento/Planejamento.h"

class QLabel;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace sigaa::ui {

class Passo;
class Segmentado;

// O que o planejamento recebe da janela principal.
struct EntradasEstudo {
    std::vector<planejamento::ProvaAlvo> provas;
    std::vector<planejamento::EntregaAlvo> entregas;      // pendentes, com prazo pela frente
    std::vector<planejamento::EntregaAlvo> atrasadas;     // pendentes, com prazo vencido
    // (idTurma, nome) de todas as turmas, para a dificuldade e as cores.
    std::vector<std::pair<std::string, std::string>> turmas;
    // Minutos de aula da grade por dia da semana ([0] = segunda).
    std::array<int, 7> aulas{};

    // O que a aba Provas sabe de cada prova, por "idTurma|descricao".
    struct InfoProva {
        bool topicosColetados{false};
        std::vector<std::string> topicos;   // a matéria: tópicos desde a prova anterior
        bool corrigida{false};              // o aluno mudou a data
        DateTime quandoSigaa;               // o que o SIGAA diz, quando corrigida
    };
    std::map<std::string, InfoProva> infoProvas;
};

class GraficoCarga;

// A página Planejamento, no desenho do protótipo web (Área de Estudo):
//
//   - cabeçalho: a semana, a próxima prova, entregas atrasadas e quanto do
//     estudo da semana já foi feito;
//   - "Onde o semestre aperta" (ui/GraficoCarga.h): a carga por semana e por
//     matéria contra o tempo livre; clicar numa semana filtra o "O que fazer";
//   - o agente de IA: conectado ou não, a última atividade, as permissões;
//   - "O que fazer": um checklist em grupos — atrasado, hoje, esta semana, até
//     a próxima prova —, com as sessões do plano para marcar como feitas;
//   - ao lado: o preparo para as próximas provas, os pontos de foco e o
//     desempenho por tópico.
//
// O plano é refeito toda vez que a página aparece; marcar uma sessão grava e
// redesenha, mas não replaneja debaixo do cursor.
class PainelPlanejamento : public QWidget {
public:
    explicit PainelPlanejamento(QWidget* pai = nullptr);

    // Guarda as entradas; replaneja já se o painel estiver à vista, ou da
    // próxima vez que aparecer.
    void definirEntradas(const EntradasEstudo& e);
    // Lê o banco, recalcula, grava e redesenha.
    void replanejar();
    // Replaneja só se o plano já existe (o aluno já abriu o Planejamento):
    // o plano nasce quando ele quer, não porque um agente mexeu nas horas.
    void replanejarSeExistir();

    // O plano no banco mudou (replano ou check), para a Agenda acompanhar.
    std::function<void()> aoMudar;
    // "Alterar permissões" (false) ou "Conectar um agente" (true).
    std::function<void(bool conectar)> aoPedirAgentes;
    // Confirmar (false) ou corrigir (true) a data de uma prova deduzida.
    std::function<void(const std::string& idTurma, const std::string& descricao, bool corrigir)>
        aoTratarProva;

protected:
    void showEvent(QShowEvent* e) override;

private:
    struct Dados;
    void mostrar();
    void mostrarCabecalho();
    void mostrarGrafico();
    void mostrarAgente();
    void mostrarChecklist();
    void mostrarSemana(int semana);
    void mostrarLateral();
    void marcar(const std::string& chave, bool feita);
    int materiaDe(const std::string& idTurma) const;

    EntradasEstudo entradas_;
    bool pendente_{true};    // entradas novas ainda não planejadas
    planejamento::Plano plano_;
    std::shared_ptr<Dados> dados_;

    QLabel* semana_{nullptr};
    QLabel* indicadores_{nullptr};
    QLabel* alertas_{nullptr};
    QWidget* legenda_{nullptr};
    GraficoCarga* grafico_{nullptr};
    QVBoxLayout* areaAgente_{nullptr};
    QLabel* subChecklist_{nullptr};
    QVBoxLayout* areaChecklist_{nullptr};
    QVBoxLayout* areaPreparo_{nullptr};
    QVBoxLayout* areaFocos_{nullptr};
    QVBoxLayout* areaDesempenho_{nullptr};

    // Estado desta visita: grupos fechados, "Estudar com IA" abertos, focos
    // resolvidos agora (com Desfazer), a semana escolhida no gráfico.
    std::set<QString> fechados_;
    std::set<std::string> iaAberta_;
    std::vector<std::int64_t> resolvidos_;
    int semanaEscolhida_{-1};
};

// Tempo disponível por dia e dificuldade por matéria. Salva sozinho, a cada
// mudança: um formulário com botão "Salvar" dentro de uma aba esquece o que
// se digitou quando a pessoa troca de página sem clicar.
//
// O que um agente de IA mudou aqui aparece marcado ("↑ pelo agente"), com
// Desfazer; e uma proposta dele que espera resposta aparece na linha da
// matéria, com o caminho até ela no Progresso.
class PainelDisponibilidade : public QWidget {
public:
    explicit PainelDisponibilidade(QWidget* pai = nullptr);
    void definirEntradas(const EntradasEstudo& e);
    // Relê do banco: uma proposta aceita no Progresso mudou um valor.
    void recarregar();

    // Depois de gravar no banco (uma mudança do aluno ou um Desfazer).
    std::function<void()> aoSalvar;
    // "Ver proposta": a página Progresso.
    std::function<void()> aoVerProposta;

private:
    void montar();
    void salvar();
    void atualizarConta(int dia);
    void atualizarTotal();
    // As marcas "↑ pelo agente": o valor de agora ainda é o que ele pôs?
    void atualizarMarcas();
    void desfazerDoAgente(int tipo, const std::string& idTurma, int dia);

    EntradasEstudo entradas_;
    planejamento::Preferencias prefs_;
    QVBoxLayout* area_{nullptr};
    QWidget* conteudo_{nullptr};
    QTimer* adiar_{nullptr};
    QLabel* estado_{nullptr};
    QLabel* total_{nullptr};
    struct LinhaDia {
        Passo* passo{nullptr};
        QLabel* conta{nullptr};
        QWidget* marca{nullptr};
    };
    std::array<LinhaDia, 7> dias_{};
    struct LinhaMateria {
        std::string idTurma;
        Segmentado* escolha{nullptr};
        QLabel* efeito{nullptr};
        QWidget* marca{nullptr};
    };
    std::vector<LinhaMateria> materias_;
};

class PainelAgentes;
class PainelProgresso;

class PainelEstudo : public QWidget {
public:
    // `banco` e `materiais`: os caminhos absolutos que este app usa, para o
    // registro do servidor MCP em cada agente (ui/Agentes.h).
    PainelEstudo(QString banco, QString materiais, QWidget* pai = nullptr);

    void definirEntradas(const EntradasEstudo& e);
    void mostrarPlanejamento();
    // A página Agentes de IA, na aba Conectar (o caminho vindo de Opções).
    void mostrarAgentes();

    // Repassado do planejamento: o plano no banco mudou.
    std::function<void()> aoMudarPlano;
    // Repassado do planejamento: confirmar (false) ou corrigir (true) a data
    // de uma prova deduzida.
    std::function<void(const std::string& idTurma, const std::string& descricao, bool corrigir)>
        aoTratarProva;

private:
    // Uma página mudou algo que as outras mostram (proposta aceita, modo
    // trocado, horas desfeitas): replaneja, relê e atualiza o contador.
    void sincronizar(QWidget* origem);
    // Quantas propostas esperam resposta, no item Progresso do menu.
    void atualizarContador();

    QString banco_;
    QListWidget* menu_{nullptr};
    QStackedWidget* paginas_{nullptr};
    PainelPlanejamento* planejamento_{nullptr};
    PainelDisponibilidade* disponibilidade_{nullptr};
    PainelProgresso* progresso_{nullptr};
    PainelAgentes* agentes_{nullptr};
};

} // namespace sigaa::ui
