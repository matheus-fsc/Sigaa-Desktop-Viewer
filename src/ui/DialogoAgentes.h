#pragma once
// Opções > Agentes de IA (docs/MCP.md §6): o que os agentes podem fazer com
// os dados do aluno, a conexão com cada agente instalado, o que eles
// registraram de volta e o que leram.
//
// Tudo aqui grava no banco na hora, sem botão Salvar: um interruptor de
// privacidade que só vale depois de um clique a mais é um interruptor que o
// aluno acha que desligou e não desligou.

#include <QDialog>

class QCheckBox;
class QLabel;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

namespace sigaa::ui {

class DialogoAgentes : public QDialog {
    Q_OBJECT
public:
    // `banco` e `materiais`: os caminhos absolutos que vão no registro do
    // servidor em cada agente (docs/MCP.md, D3).
    DialogoAgentes(QString banco, QString materiais, QWidget* pai = nullptr);

private:
    QWidget* montarPermissoes();
    QWidget* montarConexoes();
    QWidget* montarRegistros();
    QWidget* montarAtividade();

    void carregarPermissoes();
    void gravarPermissao(const char* nome, bool sim);
    void atualizarConexoes();
    void atualizarRegistros();
    void atualizarAtividade();

    QString banco_;
    QString materiais_;

    QCheckBox* leitura_{nullptr};
    QCheckBox* arquivos_{nullptr};
    QCheckBox* escrita_{nullptr};
    QCheckBox* rede_{nullptr};

    QVBoxLayout* linhasConexao_{nullptr};
    QWidget* conteudoConexao_{nullptr};
    QLabel* resultado_{nullptr};

    QTableWidget* registros_{nullptr};
    QLabel* resumoRegistros_{nullptr};
    QTableWidget* atividade_{nullptr};
};

} // namespace sigaa::ui
