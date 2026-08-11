#pragma once
// Janela principal: três abas sobre o mesmo Snapshot.
//
// A ordem das abas é a ordem das perguntas que o aluno faz, da mais urgente
// para a menos: "o que vence agora?", "quando é a prova?", "o que mudou?".
//
// A janela ABRE COM DADOS, sempre. O último snapshot vem do SQLite antes de
// qualquer requisição, então não existe tela vazia esperando a rede: mesmo sem
// internet, ou com o SIGAA fora do ar, os prazos que já conhecemos aparecem.
// Sincronizar é uma ação explícita por cima disso, nunca um pré-requisito.
//
// A aba de provas foge do formato tabela porque a pergunta é outra. Lista
// responde "qual é a próxima?"; calendário responde "como está a minha semana?".
// Três provas em quatro dias só aparecem como aglomeração no mês — na lista são
// três linhas iguais a quaisquer outras. As duas vistas dividem o mesmo modelo,
// e clicar num dia filtra a lista em vez de abrir outra tela.
//
// Estrutura visual (abas, tabelas, cartões, ações, barra de ferramentas) mora
// em forms/JanelaPrincipal.ui. Fica aqui só o que o Designer não alcança: os
// widgets da barra de status, o menu de conta com o nome do cofre, o ícone da
// bandeja, os textos que dependem das constantes de período e as fontes/cores
// derivadas da paleta viva.

#include <QDate>
#include <QMainWindow>
#include <QString>

#include <memory>

#include "core/model/Models.h"
#include "platform/Credenciais.h"

class QLabel;
class QProgressBar;
class QSystemTrayIcon;
class QTimer;

namespace Ui {
class JanelaPrincipal;
}

namespace sigaa::ui {

class Trabalhador;

class JanelaPrincipal : public QMainWindow {
    Q_OBJECT

public:
    explicit JanelaPrincipal(QWidget* pai = nullptr);
    ~JanelaPrincipal() override;   // fora de linha: Ui::JanelaPrincipal é incompleto

protected:
    // Retinge os ícones quando o usuário troca o tema do Windows. Sem isto o
    // desenho continua na cor de antes e some contra o novo fundo — ícone é
    // pixmap, não se atualiza sozinho como uma cor de paleta.
    void changeEvent(QEvent* ev) override;

private:
    void montarAcoes();
    void montarStatus();
    void montarBandeja();
    void aplicarIcones();

    // Recarrega a tela a partir do banco. Devolve false se o banco não abriu.
    bool recarregarDoBanco();
    void mostrar(const Snapshot& s);

    // --- o dia (aba inicial) -----------------------------------------------
    // As aulas de hoje e de amanhã com o material de cada uma, acima da lista
    // de prazos. O prazo continua ali, mas deixou de ser a primeira coisa que
    // a pessoa vê: prazo é o que vence, aula é o que acontece hoje.
    void montarDia(const Snapshot& s);

    // --- dashboard de provas ---------------------------------------------
    void montarProvas();
    void atualizarResumoProvas(const Snapshot& s);
    // Data inválida = sem filtro, lista o semestre inteiro.
    void filtrarProvasPorDia(QDate dia);

    // --- turmas ------------------------------------------------------------
    void montarTurmas();
    void abrirTurma();        // a partir da aba Turmas
    void abrirTurmaDoDia();   // a partir de uma aula da aba Hoje
    void abrirJanelaDaTurma(const Turma& turma);
    void sincronizar(bool comTurmas);
    void aoConcluir();

    // Ambiente > cofre > .env > diálogo. Devolve false se o usuário desistiu.
    bool obterCredenciais(std::string& login, std::string& senha);
    void aoAbrir();          // onboarding na primeira execução
    void trocarConta();
    void esquecerConta();

    // Sincronização periódica ENQUANTO A JANELA ESTÁ ABERTA.
    //
    // Não há mais tarefa agendada: nada deste app roda com o programa fechado.
    // O preço é explícito — app fechado, nenhum aviso. Em troca, não existe
    // processo de fundo aparecendo sozinho na máquina do usuário.
    void ligarAutomatico(bool sim);
    void agendarProxima();

    void ocupado(bool sim);
    void status(const QString& msg);

    std::unique_ptr<Ui::JanelaPrincipal> formulario_;

    // Widgets da barra de status: o Designer não sabe povoar uma QStatusBar.
    QLabel* rotulo_{nullptr};
    QProgressBar* barra_{nullptr};
    QSystemTrayIcon* bandeja_{nullptr};

    QTimer* relogioPortal_{nullptr};   // barato: 1 requisição
    QTimer* relogioTurmas_{nullptr};   // caro: ~15 requisições

    // Credenciais só desta execução, para quem entrou sem marcar "guardar".
    // Sem isto, cada sincronização abriria o diálogo de novo — e a que o
    // usuário acabou de digitar seria esquecida na hora.
    plat::Credenciais sessao_;

    Trabalhador* trabalho_{nullptr};
    Snapshot snapshot_;
    QString relatorio_;   // caminho do último relatório gerado, ou vazio

    // Dia selecionado no calendário. Sobrevive ao sync de propósito: o filtro é
    // do usuário, e trocar o modelo por baixo dele não deveria devolvê-lo à
    // lista inteira sem ele pedir.
    QDate diaFiltrado_;
};

} // namespace sigaa::ui
