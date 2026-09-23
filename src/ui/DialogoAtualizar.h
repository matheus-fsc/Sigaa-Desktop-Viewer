#pragma once
// Escolher O QUE sincronizar antes de sincronizar.
//
// POR QUE ISTO EXISTE: o gargalo do app não é processar HTML, é esperar o
// SIGAA responder. Cada turma custa de 2 a 4 requisições de ~1,5 s, e o botão
// "Atualizar tudo" pagava todas elas sempre — 40 segundos para responder
// "faltei na aula de hoje?", que é uma pergunta sobre UMA turma.
//
// O diálogo troca um clique a mais por dezenas de segundos a menos. O caminho
// rápido é o duplo clique numa turma: escolhe a turma e confirma de uma vez,
// sem passar pelos controles.
//
// O PORTAL É SEMPRE LIDO, e não há caixa para desligá-lo: é UMA requisição, e
// é de onde saem os prazos e as notícias. Deixar o aluno desmarcá-lo economiza
// 1,5 s e cria o estado "atualizei e não vi que tinha entrega amanhã".

#include <QDialog>
#include <set>
#include <string>
#include <vector>

#include "core/model/Models.h"

class QCheckBox;
class QLabel;
class QDialogButtonBox;
class QListWidget;

namespace sigaa::ui {

class DialogoAtualizar : public QDialog {
    Q_OBJECT
public:
    struct Escolha {
        // Vazio com `entrarNasTurmas` = todas. Ver OpcoesColeta::apenasTurmas.
        std::set<std::string> turmas;
        bool entrarNasTurmas{false};
        bool arquivos{true};
        bool frequencia{true};
        bool baixarMateriais{true};
    };

    // `ultima` repõe a escolha anterior: quem atualiza a mesma turma toda
    // manhã não deve reconfigurar o diálogo todo dia.
    DialogoAtualizar(const std::vector<Turma>& turmas, const Escolha& ultima,
                     QWidget* pai = nullptr);

    Escolha escolha() const;

private:
    void atualizarResumo();

    std::vector<Turma> turmas_;

    QListWidget* lista_{nullptr};
    QCheckBox* arquivos_{nullptr};
    QCheckBox* frequencia_{nullptr};
    QCheckBox* baixar_{nullptr};
    QCheckBox* soPortal_{nullptr};
    QLabel* resumo_{nullptr};
    QDialogButtonBox* botoes_{nullptr};
};

} // namespace sigaa::ui
