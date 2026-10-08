#pragma once
// A página Agentes de IA da aba Estudo (docs/MCP.md §5 e §6), em três abas:
//
//   - Permissões: o que o agente pode ler e gravar (interruptores) e o que ele
//     pode mudar no plano — por tipo, não pode / propõe / aplica e avisa — com
//     um teto de horas por dia (mcp/Propostas.h);
//   - Conectar: a conexão com cada agente instalado;
//   - Atividade: o que eles leram e gravaram, e o que foi recusado e por quê.
//
// Tudo grava no banco na hora, sem botão Salvar: um interruptor de
// privacidade que só vale depois de um clique a mais é um interruptor que o
// aluno acha que desligou e não desligou. E tudo que veio do agente é
// mostrado como texto puro, nunca como HTML (§7).
//
// O que os agentes devolvem (estudo, desempenho, focos, propostas) fica na
// página Progresso (ui/Progresso.h).

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QFrame;
class QLabel;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

namespace sigaa::ui {

class Interruptor;
class Segmentado;

class PainelAgentes : public QWidget {
public:
    // `banco` e `materiais`: os caminhos absolutos que vão no registro do
    // servidor em cada agente (docs/MCP.md, D3).
    PainelAgentes(QString banco, QString materiais, QWidget* pai = nullptr);

    // Mostra a aba Conectar (o caminho de quem chega pelo "Conectar um agente"
    // do Progresso, ou por Opções).
    void mostrarConexao();
    // Mostra a aba Permissões ("Alterar o que ele pode mudar", no Progresso).
    void mostrarPermissoes();

    // Um modo mudou e aplicou ou escondeu propostas: o plano e o contador do
    // menu mudam.
    std::function<void()> aoMudar;

protected:
    void showEvent(QShowEvent* e) override;

private:
    QWidget* montarPermissoes();
    QWidget* montarConexoes();
    QWidget* montarAtividade();
    void carregarPermissoes();
    void gravarPermissao(const char* nome, bool sim, const QString& rotulo);
    void atualizarConexoes();
    void atualizarAtividade();

    QString banco_;
    QString materiais_;
    QTabWidget* abas_{nullptr};

    struct LinhaPermissao {
        Interruptor* chave{nullptr};
        QWidget* linha{nullptr};
        QLabel* precisa{nullptr};
    };
    LinhaPermissao leitura_, arquivos_, escrita_, rede_;
    QLabel* estadoPermissoes_{nullptr};
    std::vector<Segmentado*> modos_;   // horas, dificuldade, sessão, foco
    Segmentado* teto_{nullptr};
    QWidget* caixaModos_{nullptr};
    QLabel* precisaEscrita_{nullptr};

    QFrame* semLeitura_{nullptr};
    QVBoxLayout* linhasConexao_{nullptr};
    QWidget* conteudoConexao_{nullptr};
    QLabel* resultado_{nullptr};
    QTableWidget* atividade_{nullptr};
};

} // namespace sigaa::ui
