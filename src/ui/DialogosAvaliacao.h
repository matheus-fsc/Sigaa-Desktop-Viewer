#pragma once
// Os dois diálogos das correções de data de prova: editar/criar, e o histórico.
//
// SEM .ui, e não por pressa: os dois são formulários de meia dúzia de campos
// cujo conteúdo MUDA conforme o caso (criar uma prova pede turma e descrição;
// corrigir uma existente não, porque esses dois vêm do SIGAA e não se editam).
// No Designer isso viraria um formulário com campos escondidos por código —
// duas fontes de verdade sobre o mesmo layout. Em C++ há uma só.

#include <QDialog>
#include <functional>
#include <vector>

#include "core/avaliacao/Ajustes.h"
#include "core/frequencia/Presenca.h"
#include "core/model/Models.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;
class QTimeEdit;
class QVBoxLayout;
class QWidget;

namespace sigaa::ui {

// Corrigir a data de uma prova que o SIGAA conhece, ou cadastrar uma que ele
// não tem.
class DialogoAvaliacao : public QDialog {
    Q_OBJECT
public:
    // Corrigir: `atual` é a prova como está hoje.
    DialogoAvaliacao(const avaliacao::Efetiva& atual, QWidget* pai = nullptr);

    // Criar: precisa da lista de turmas, porque uma prova que o SIGAA não
    // conhece não tem de onde herdar a turma.
    explicit DialogoAvaliacao(const std::vector<Turma>& turmas, QWidget* pai = nullptr);

    // Válido depois de `exec() == Accepted`.
    avaliacao::Ajuste resultado() const;

private:
    void montar();

    bool criando_{false};
    std::vector<Turma> turmas_;
    avaliacao::Efetiva atual_;

    QComboBox* turma_{nullptr};       // só ao criar
    QLineEdit* descricao_{nullptr};   // só ao criar
    QDateEdit* data_{nullptr};
    QCheckBox* temHora_{nullptr};
    QTimeEdit* hora_{nullptr};
    QPlainTextEdit* nota_{nullptr};
};

// O histórico de mudanças de data — das duas origens, na mesma lista.
//
// Juntar "o professor remarcou" e "eu corrigi" numa linha do tempo só é o
// ponto: a pergunta que ele responde é "por que esta data mudou três vezes?",
// e ela não se responde olhando uma origem de cada vez.
// Uma linha do histórico, já traduzida para texto.
//
// O diálogo recebe ISTO e não o struct de domínio: há duas espécies de
// histórico (datas de prova e presença) que contam a mesma coisa de formas
// diferentes, e um diálogo por espécie seria a mesma tabela escrita duas
// vezes, envelhecendo em direções diferentes.
struct LinhaHistorico {
    QString quando;    // quando o app registrou
    QString onde;      // turma, prova, dia — o que a mudança afetou
    QString oQue;      // "você corrigiu", "o SIGAA substituiu a sua data"
    QString mudanca;   // "de X para Y"
    QString nota;
    bool alarme{false};   // pinta o "o quê" de vermelho
};

class DialogoHistorico : public QDialog {
    Q_OBJECT
public:
    // `titulo` distingue "Histórico de Prova 1" de "Histórico de alterações".
    DialogoHistorico(const std::vector<LinhaHistorico>& linhas, const QString& titulo,
                     const QString& vazio, QWidget* pai = nullptr);
};

// As datas deduzidas de tópico, para confirmar, corrigir ou descartar de uma
// vez — aberto pela pílula "○ N datas a confirmar" da aba Provas.
//
// POR QUE UM DIÁLOGO, e não só a tabela: na tabela as inferidas estão
// espalhadas entre as provas do professor, e confirmar oito datas era caçar
// oito linhas, selecionar e clicar oito vezes. Aqui elas vêm juntas, cada uma
// com as três respostas possíveis ao lado.
//
// O diálogo NÃO grava nada. Cada botão chama a ação que a janela principal
// entregou — a mesma que a barra e o menu da tabela usam, com histórico e
// tudo — e a janela devolve a lista atualizada por `mostrar`. Uma regra de
// gravação só, em vez de uma cópia aqui que envelheceria diferente.
class DialogoConfirmarDatas : public QDialog {
    Q_OBJECT
public:
    struct Acoes {
        std::function<void(const avaliacao::Efetiva&)> confirmar;
        std::function<void(const avaliacao::Efetiva&)> editar;
        std::function<void(const avaliacao::Efetiva&)> descartar;
        std::function<void(const avaliacao::Ajuste&)> restaurar;
    };

    explicit DialogoConfirmarDatas(Acoes acoes, QWidget* pai = nullptr);

    // Redesenha a lista. Pode ser chamada de dentro do clique de um botão da
    // própria lista: o conteúdo velho é destruído depois, não na hora.
    void mostrar(const std::vector<avaliacao::Efetiva>& pendentes,
                 const std::vector<avaliacao::Ajuste>& descartadas);

private:
    Acoes acoes_;
    QVBoxLayout* area_{nullptr};
    QWidget* conteudo_{nullptr};
};

// Converte para a tabela acima. Ficam aqui, e não no núcleo, porque traduzem
// enum em frase para humano — que é decisão de apresentação.
std::vector<LinhaHistorico> paraHistorico(const std::vector<avaliacao::Mudanca>& m);
std::vector<LinhaHistorico> paraHistorico(const std::vector<frequencia::Mudanca>& m);

} // namespace sigaa::ui
