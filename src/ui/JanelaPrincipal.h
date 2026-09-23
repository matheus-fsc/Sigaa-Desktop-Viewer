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
#include <optional>
#include <QMainWindow>
#include <QString>

#include <memory>

#include "core/atualizacao/Atualizador.h"
#include "core/avaliacao/Ajustes.h"
#include "core/http/SessaoViva.h"
#include "ui/DialogoAtualizar.h"
#include "ui/DialogoOpcoes.h"
#include "core/model/Models.h"
#include "platform/Credenciais.h"

class QLabel;
class QProgressBar;
class QSystemTrayIcon;
class QTimer;

namespace Ui {
class JanelaPrincipal;
}

class QAction;
class QToolButton;

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

    // Janela estreita esconde o RÓTULO dos botões da barra, não os botões.
    void resizeEvent(QResizeEvent* ev) override;

private:
    // Texto ao lado do ícone enquanto couber; só ícone quando não couber mais.
    //
    // O comportamento padrão do QToolBar é pior: ele empurra o que não coube
    // para um botão de transbordo que, com folha de estilo aplicada, sai sem
    // desenho nenhum — os botões "Diagnóstico" e "Conta" simplesmente sumiam
    // numa janela de 700 px, sem nada indicando que existiam.
    void ajustarBarraAoEspaco();

    // Largura que a barra pede COM os rótulos, medida uma vez enquanto ela
    // ainda está nesse estado. Medir a cada resize devolveria a largura do
    // estado corrente e a barra ficaria presa em "só ícone" depois da primeira
    // vez que encolhesse.
    int larguraBarraComTexto_{0};

    // Acabamento das listas (densidade, distintivos). Roda antes das montagens
    // por aba, para que elas encontrem a view já com a altura de linha final —
    // resizeColumnsToContents medindo antes mediria a linha errada.
    void montarListas();

    // Restaura a aba em que o aluno estava e passa a guardar a escolha.
    void montarAbaLembrada();

    // "Atualizar": abre o diálogo de escolha e sincroniza o que foi marcado.
    void escolherEAtualizar();

    // Opções: a rotina automática e as ferramentas de desenvolvedor.
    void abrirOpcoes();
    DialogoOpcoes::Config configAtual() const;
    void aplicarConfig(const DialogoOpcoes::Config& c);

    // Atualização do próprio app, a partir das releases do GitHub.
    //
    // `silencioso` é a verificação da abertura: sem diálogo, sem alarme, e sem
    // dizer nada quando não há novidade nem quando o GitHub está fora do ar.
    void procurarAtualizacao(DialogoOpcoes* dlg, bool silencioso);
    void instalarAtualizacao(DialogoOpcoes* dlg);

    // --- correções de data de prova ----------------------------------------
    void montarBotoesProva();
    void recalcularProvas();          // ajustes_ + snapshot_ -> provas_
    void recarregarAjustes();         // do banco
    bool gravarAjuste(const avaliacao::Ajuste& a, avaliacao::TipoMudanca tipo,
                      const std::string& de);

    // A prova selecionada na tabela, ou nullopt. Sai da CHAVE guardada na
    // linha, nunca do índice: a tabela é ordenável e a linha 3 de agora não é
    // a linha 3 de depois de um clique no cabeçalho.
    std::optional<avaliacao::Efetiva> provaSelecionada() const;

    void corrigirProva();
    void confirmarProva();
    void criarProva();
    void desfazerCorrecao();
    void verHistorico();
    void avisarConflitos(const std::vector<avaliacao::Conflito>& cs);
    void atualizarBotoesProva();

    // Esconde os botões que agem sobre a prova selecionada quando a janela não
    // os comporta. Eles continuam no menu do botão direito.
    void ajustarBarraProvasAoEspaco();
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
    void sincronizar(const DialogoAtualizar::Escolha& escolha);
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

    // As correções do aluno, e a lista de provas que resulta de aplicá-las.
    //
    // `provas_` é derivada: só existe para não recalcular `avaliacao::efetivas`
    // em cada uma das quatro telas que precisam dela (tabela, calendário,
    // cartões, contador da aba) e correr o risco de uma delas ficar para trás.
    // Quem muda `ajustes_` chama `recalcularProvas()` e pronto.
    std::vector<avaliacao::Ajuste> ajustes_;
    std::vector<avaliacao::Efetiva> provas_;

    // A última escolha do diálogo, reposta na próxima abertura: quem atualiza
    // a mesma turma toda manhã não deve reconfigurar o diálogo todo dia.
    DialogoAtualizar::Escolha ultimaEscolha_;

    // UMA sessão para o app inteiro, reaproveitada enquanto o SIGAA a aceitar.
    //
    // Login custa 6 a 8 s e é a única operação que o SIGAA às vezes recusa
    // quando já há outra sessão aberta na conta. Antes cada tarefa abria a
    // sua: o diálogo de senha, a sincronização, a janela de turma. Agora o
    // diálogo entrega a que abriu, e a sincronização continua de onde ela
    // parou. Ver core/http/SessaoViva.h.
    http::SessaoViva sessaoViva_;

    // A release encontrada na última procura, enquanto não for instalada.
    std::optional<atualizacao::Lancamento> lancamentoNovo_;

    // Ações das provas, criadas em C++ (ver montarBotoesProva). São QAction e
    // não botões porque cada uma aparece em DOIS lugares: na barra acima da
    // lista e no menu do botão direito sobre a prova.
    QAction* acNovaProva_{nullptr};
    QAction* acConfirmarProva_{nullptr};
    QAction* acCorrigirProva_{nullptr};
    QAction* acDesfazerProva_{nullptr};
    QAction* acHistoricoProva_{nullptr};

    // Largura que a barra de provas pede com todos os botões, medida uma vez.
    int larguraBarraProvas_{0};
    // O que a janela estreita esconde. Só os que exigem uma prova selecionada.
    std::vector<QWidget*> botoesProvaSecundarios_;

    // Quanto o painel do calendário ocupa ao lado da lista. Entra na conta de
    // `ajustarBarraProvasAoEspaco` porque a barra divide a largura com ele.
    static constexpr int kLarguraCalendario = 380;
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
