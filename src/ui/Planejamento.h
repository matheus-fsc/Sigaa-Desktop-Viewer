#pragma once
// A aba Estudo: um menu lateral à esquerda e, à direita, a página escolhida.
//
//   - Planejamento: o mapa de pressão (onde aperta), as dicas (o que fazer) e
//     o plano dia a dia com um check por sessão.
//   - Horas e dificuldade: o tempo disponível por dia da semana, com as aulas
//     da grade já descontadas, e o peso de cada matéria.
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
#include <functional>
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

// O que o planejamento recebe da janela principal.
struct EntradasEstudo {
    std::vector<planejamento::ProvaAlvo> provas;
    std::vector<planejamento::EntregaAlvo> entregas;
    // (idTurma, nome) de todas as turmas, para a dificuldade.
    std::vector<std::pair<std::string, std::string>> turmas;
    // Minutos de aula da grade por dia da semana ([0] = segunda).
    std::array<int, 7> aulas{};
};

// O mapa de pressão: uma coluna por semana.
class GraficoPressao : public QWidget {
public:
    explicit GraficoPressao(QWidget* pai = nullptr);
    void definir(std::vector<planejamento::SemanaPlano> semanas,
                 std::vector<planejamento::Sessao> sessoes);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<planejamento::SemanaPlano> semanas_;
    std::vector<int> feitosPorSemana_;   // minutos já feitos, por coluna
};

class PainelPlanejamento : public QWidget {
public:
    explicit PainelPlanejamento(QWidget* pai = nullptr);

    // Guarda as entradas; replaneja já se o painel estiver à vista, ou da
    // próxima vez que aparecer.
    void definirEntradas(const EntradasEstudo& e);
    // Lê o banco, recalcula, grava e redesenha.
    void replanejar();

    // Chamado quando o plano no banco muda (replano ou check), para a Agenda
    // acompanhar.
    std::function<void()> aoMudar;

protected:
    void showEvent(QShowEvent* e) override;

private:
    void mostrarDicas();
    void mostrarPlano();
    void atualizarResumo();
    void marcar(size_t i, bool feita);

    EntradasEstudo entradas_;
    bool pendente_{true};    // entradas novas ainda não planejadas
    planejamento::Plano plano_;

    QLabel* resumo_{nullptr};
    GraficoPressao* grafico_{nullptr};
    QScrollArea* rolagemGrafico_{nullptr};
    QVBoxLayout* areaDicas_{nullptr};
    QWidget* conteudoDicas_{nullptr};
    QVBoxLayout* areaPlano_{nullptr};
    QWidget* conteudoPlano_{nullptr};
};

// Tempo disponível por dia e dificuldade por matéria. Salva sozinho, a cada
// mudança: um formulário com botão "Salvar" dentro de uma aba esquece o que
// se digitou quando a pessoa troca de página sem clicar.
class PainelDisponibilidade : public QWidget {
public:
    explicit PainelDisponibilidade(QWidget* pai = nullptr);
    void definirEntradas(const EntradasEstudo& e);

    // Depois de gravar no banco.
    std::function<void()> aoSalvar;

private:
    void montar();
    void salvar();

    EntradasEstudo entradas_;
    planejamento::Preferencias prefs_;
    QVBoxLayout* area_{nullptr};
    QWidget* conteudo_{nullptr};
    QTimer* adiar_{nullptr};
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

private:
    QListWidget* menu_{nullptr};
    QStackedWidget* paginas_{nullptr};
    PainelPlanejamento* planejamento_{nullptr};
    PainelDisponibilidade* disponibilidade_{nullptr};
    PainelProgresso* progresso_{nullptr};
    PainelAgentes* agentes_{nullptr};
};

} // namespace sigaa::ui
