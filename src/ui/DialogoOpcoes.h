#pragma once
// Configurações do app: a rotina automática e o canto do desenvolvedor.
//
// POR QUE A ROTINA PRECISA DE UM TETO, e não de um campo livre de minutos:
//
//   O SIGAA é um servidor de uma universidade, não uma API pública. Um app que
//   bate nele a cada minuto é indistinguível de um robô abusivo, e quem paga
//   pelo bloqueio é a CONTA DO ALUNO — não o programa. Pior: ele não tem como
//   saber que foi isso, porque o SIGAA não explica; ele vê o login parar de
//   funcionar na véspera da entrega.
//
//   Quem configura um intervalo não sabe disso, e não tem por que saber. Então
//   os intervalos são uma LISTA FECHADA, com o mínimo escolhido aqui e não lá.
//   É a mesma decisão do rate limit de 1,5 s entre requisições
//   (core/http/SigaaSession.h): o app protege a conta de quem o usa, inclusive
//   das escolhas dessa pessoa.
//
// O CICLO COMPLETO tem teto próprio e mais alto. Ele visita cada turma — 4
// requisições por turma, 28 numa grade de sete — e é a diferença entre "o app
// consulta o SIGAA" e "o app varre o SIGAA".

#include <QDialog>

#include "ui/DialogoAtualizar.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace sigaa::ui {

class DialogoOpcoes : public QDialog {
    Q_OBJECT
public:
    struct Config {
        bool automatico{true};
        int minutosPortal{20};     // o ciclo leve
        int minutosCompleto{360};  // o ciclo que entra nas turmas

        // O que o ciclo completo busca dentro de cada turma.
        bool arquivos{true};
        bool frequencia{true};
        bool baixarMateriais{true};

        // Procurar versão nova ao abrir o app. Só PROCURA — instalar continua
        // sendo um clique, sempre. Um programa que troca o próprio executável
        // sem ninguém mandar é um programa em que não se confia.
        bool verificarAtualizacao{true};
    };

    // Os intervalos que a interface oferece, em minutos. Fora desta lista o
    // app não vai — ver o cabeçalho sobre por que o teto não é do usuário.
    static const QList<int>& intervalosPortal();
    static const QList<int>& intervalosCompleto();

    DialogoOpcoes(const Config& atual, QWidget* pai = nullptr);

    Config config() const;

    // Mostra o resultado de uma procura. A janela principal faz o trabalho de
    // rede — este diálogo não sabe falar com o GitHub.
    void mostrarResultadoAtualizacao(const QString& texto, bool haNova);
    void procurandoAtualizacao();

Q_SIGNALS:
    // O canto do desenvolvedor. A janela principal é quem sabe abrir essas
    // telas; o diálogo só avisa que o botão foi clicado.
    void pediuDiagnostico();
    void pediuRelatorio();

    void pediuProcurarAtualizacao();
    void pediuInstalarAtualizacao();

private:
    void atualizarResumo();

    // Fixa no recado a altura do MAIOR texto que ele pode exibir, para que
    // mudar de texto não tire altura do formulário acima. Ver tema::reservarAltura.
    void reservarAlturaDoResumo();

    QCheckBox* automatico_{nullptr};
    QComboBox* intervaloPortal_{nullptr};
    QComboBox* intervaloCompleto_{nullptr};
    QCheckBox* arquivos_{nullptr};
    QCheckBox* frequencia_{nullptr};
    QCheckBox* baixar_{nullptr};
    QLabel* resumo_{nullptr};

    QCheckBox* verificarAtualizacao_{nullptr};
    QLabel* estadoAtualizacao_{nullptr};
    QPushButton* botaoProcurar_{nullptr};
    QPushButton* botaoInstalar_{nullptr};
};

} // namespace sigaa::ui
