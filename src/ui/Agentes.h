#pragma once
// Os agentes de IA na aba Estudo (docs/MCP.md §5 e §6). Duas páginas do menu
// lateral:
//
//   - Progresso: o que os agentes devolveram — pontos de foco abertos (que o
//     aluno resolve ou apaga), desempenho por tópico e o histórico de tudo,
//     com quem gravou;
//   - Agentes de IA: o que eles podem fazer (permissões), a conexão com cada
//     agente instalado e a atividade (o que leram e gravaram).
//
// Tudo grava no banco na hora, sem botão Salvar: um interruptor de
// privacidade que só vale depois de um clique a mais é um interruptor que o
// aluno acha que desligou e não desligou. E tudo que veio do agente é
// mostrado como texto puro, nunca como HTML (§7).

#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

namespace sigaa::ui {

class PainelAgentes : public QWidget {
public:
    // `banco` e `materiais`: os caminhos absolutos que vão no registro do
    // servidor em cada agente (docs/MCP.md, D3).
    PainelAgentes(QString banco, QString materiais, QWidget* pai = nullptr);

    // Mostra a aba Conectar (o caminho de quem chega pelo "Conectar um agente"
    // do Progresso, ou por Opções).
    void mostrarConexao();

protected:
    void showEvent(QShowEvent* e) override;

private:
    QWidget* montarPermissoes();
    QWidget* montarConexoes();
    QWidget* montarAtividade();
    void carregarPermissoes();
    void gravarPermissao(const char* nome, bool sim);
    void atualizarConexoes();
    void atualizarAtividade();

    QString banco_;
    QString materiais_;
    QTabWidget* abas_{nullptr};

    QCheckBox* leitura_{nullptr};
    QCheckBox* arquivos_{nullptr};
    QCheckBox* escrita_{nullptr};
    QCheckBox* rede_{nullptr};

    QVBoxLayout* linhasConexao_{nullptr};
    QWidget* conteudoConexao_{nullptr};
    QLabel* resultado_{nullptr};
    QTableWidget* atividade_{nullptr};
};

class PainelProgresso : public QWidget {
public:
    explicit PainelProgresso(QString banco, QWidget* pai = nullptr);

    // Relê o banco. Chamado ao aparecer e quando um agente gravou algo.
    void atualizar();

    // "Conectar um agente", no estado vazio.
    std::function<void()> aoPedirConexao;
    // Um ponto de foco foi resolvido ou apagado aqui: o plano muda.
    std::function<void()> aoMudar;

protected:
    void showEvent(QShowEvent* e) override;

private:
    void mostrarFocos();

    QString banco_;
    QLabel* resumo_{nullptr};
    QWidget* vazio_{nullptr};
    QWidget* conteudo_{nullptr};
    QVBoxLayout* areaFocos_{nullptr};
    QWidget* caixaFocos_{nullptr};
    QTableWidget* desempenho_{nullptr};
    QTableWidget* historico_{nullptr};
};

} // namespace sigaa::ui
