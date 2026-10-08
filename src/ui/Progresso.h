#pragma once
// A página Progresso da aba Estudo: o que os agentes de IA devolveram e o que
// eles querem mudar no plano (docs/MCP.md §5, mcp/Propostas.h).
//
//   - Propostas do agente: cada mudança pendente com motivo e efeito, para
//     aceitar, ajustar ou recusar; as respondidas ficam um tempo à vista com
//     Desfazer ou Reabrir;
//   - Métricas: quanto do plano o aluno seguiu, o acerto e a próxima prova, e
//     uma linha por matéria;
//   - Pontos de foco, desempenho por tópico e o histórico de tudo, com quem
//     gravou.
//
// Nada tem botão Salvar, e tudo que é do agente pode ser desfeito. Texto que
// veio do agente é texto puro, nunca HTML (§7).

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "core/estudo/Registros.h"
#include "ui/Planejamento.h"

class QFrame;
class QLabel;
class QHBoxLayout;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace sigaa::ui {

class PainelProgresso : public QWidget {
public:
    explicit PainelProgresso(QString banco, QWidget* pai = nullptr);

    // Provas e turmas: a base das métricas e do efeito de cada proposta.
    void definirEntradas(const EntradasEstudo& e);
    // Relê o banco. Chamado ao aparecer e quando um agente gravou algo.
    void atualizar();

    // "Conectar um agente", no estado vazio.
    std::function<void()> aoPedirConexao;
    // "Alterar o que ele pode mudar": a aba Permissões dos agentes.
    std::function<void()> aoPedirPermissoes;
    // O plano mudou aqui (proposta aceita, foco resolvido, registro apagado).
    std::function<void()> aoMudar;

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    struct Dados;
    void mostrarPropostas(const Dados& d);
    QWidget* cartaoProposta(const estudo::Proposta& q, const Dados& d);
    void mostrarMetricas(const Dados& d);
    void mostrarFocos(const Dados& d);
    void mostrarDesempenho(const Dados& d);
    void mostrarHistorico(const Dados& d);
    // Depois de uma ação: relê e avisa quem depende do plano.
    void mudou();

    QString banco_;
    EntradasEstudo entradas_;

    QLabel* resumo_{nullptr};
    QWidget* vazio_{nullptr};
    QWidget* conteudo_{nullptr};

    QLabel* pendentes_{nullptr};
    QLabel* modos_{nullptr};
    QVBoxLayout* areaPropostas_{nullptr};
    QLabel* escondidas_{nullptr};

    QHBoxLayout* areaKpis_{nullptr};
    QTableWidget* porMateria_{nullptr};
    QVBoxLayout* areaFocos_{nullptr};
    QTableWidget* desempenho_{nullptr};
    QTableWidget* historico_{nullptr};
    QPushButton* apagarSel_{nullptr};
    QFrame* perigo_{nullptr};
    QHBoxLayout* chips_{nullptr};

    // Só desta visita à página: o ajuste aberto, os valores ajustados e os
    // focos fechados agora (que mostram "Desfazer" até a página sair).
    std::int64_t ajustando_{0};
    std::map<std::int64_t, int> ajuste_;
    struct Fechado {
        estudo::PontoFoco foco;
        bool dominado{false};
    };
    std::vector<Fechado> fechados_;
};

} // namespace sigaa::ui
