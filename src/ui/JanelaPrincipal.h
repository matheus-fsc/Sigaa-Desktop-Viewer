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
class JanelaDiagnostico;

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

    // Rolagem horizontal (ou Shift+roda) sobre a árvore da agenda vira troca de
    // semana — é o gesto de "virar página" no trackpad, e quem tem um espera
    // que funcione antes de procurar o botão.
    bool eventFilter(QObject* alvo, QEvent* ev) override;

private:
    void montarAcoes();
    void montarStatus();
    void montarBandeja();
    void aplicarIcones();

    // Tráfego com o SIGAA, ao vivo. Fica atrás de uma ação em vez de uma aba
    // porque não é informação de rotina — é a resposta para "por que o SIGAA
    // me bloqueou?", e essa pergunta se faz uma vez por semestre.
    void abrirDiagnostico();

    // Recarrega a tela a partir do banco. Devolve false se o banco não abriu.
    bool recarregarDoBanco();
    void mostrar(const Snapshot& s);

    // --- a agenda (aba inicial) --------------------------------------------
    // As aulas da semana com o material de cada uma, acima da lista de prazos.
    // O prazo continua ali, mas deixou de ser a primeira coisa que a pessoa vê:
    // prazo é o que vence, aula é o que acontece hoje.
    //
    // Pagina de semana em semana em vez de rolar o semestre inteiro numa lista
    // só: a semana é a unidade em que o aluno pensa ("o que tem quarta?"), e uma
    // árvore com 120 dias abriria sempre no mesmo lugar errado — o topo, que é
    // fevereiro. A paginação também dá um lugar honesto para o fim dos dados:
    // o botão desliga na borda do que a coleta conhece.
    void montarAgenda();
    void montarBarraAgenda();
    // Ancora a agenda na semana que contém `dia` e redesenha.
    void irParaSemana(QDate dia);
    void deslocarAgenda(int semanas);

    // --- dashboard de provas ---------------------------------------------
    void montarProvas();
    void atualizarResumoProvas(const Snapshot& s);
    // Data inválida = sem filtro, lista o semestre inteiro.
    void filtrarProvasPorDia(QDate dia);

    // --- turmas ------------------------------------------------------------
    void montarTurmas();
    void abrirTurma();          // a partir da aba Turmas
    void abrirTurmaDaAgenda();  // a partir de uma aula da aba Agenda
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

    // Uma só, criada no primeiro Ctrl+D e mantida: duas registrariam dois
    // observadores no mesmo tráfego, e o histórico apareceria em dobro.
    JanelaDiagnostico* diagnostico_{nullptr};

    Trabalhador* trabalho_{nullptr};
    Snapshot snapshot_;
    QString relatorio_;   // caminho do último relatório gerado, ou vazio

    // Dia selecionado no calendário. Sobrevive ao sync de propósito: o filtro é
    // do usuário, e trocar o modelo por baixo dele não deveria devolvê-lo à
    // lista inteira sem ele pedir.
    QDate diaFiltrado_;

    // Segunda-feira da semana mostrada na agenda. Inválida = ainda não ancorada
    // (o primeiro `mostrar` a coloca na semana de hoje). Sobrevive ao sync pelo
    // mesmo motivo do filtro de provas: a semana é escolha do usuário, e o ciclo
    // automático de 20 minutos não pode arrastá-lo de volta para hoje enquanto
    // ele olha a semana que vem.
    QDate inicioAgenda_;

    // Acumulador da roda horizontal: um trackpad manda dezenas de eventos de
    // poucos graus, e virar a semana em cada um daria um borrão de meses.
    int rolagemAgenda_{0};
};

} // namespace sigaa::ui
