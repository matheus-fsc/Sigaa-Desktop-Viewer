#pragma once
// "Entrar na turma", como no SIGAA web: os arquivos que o professor publicou,
// com download de verdade.
//
// Esta janela tem uma SESSÃO PRÓPRIA com o SIGAA, separada da sincronização.
// Não é desperdício: a navegação do SIGAA é uma máquina de estados no servidor
// (o ViewState vale para a view corrente), então um sync automático disparando
// no meio de um download invalidaria a view e o arquivo viria como página de
// erro. Duas sessões custam um login a mais e removem a classe inteira de bug.
//
// Uma operação de rede por vez, sempre fora da thread da GUI. Enquanto uma
// roda, os botões ficam desligados — é isso que torna seguro o `SessaoTurma`
// ser tocado por threads diferentes ao longo do tempo: nunca por duas ao mesmo
// tempo, e a entrega em fila do sinal é a barreira de sincronização.

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>
#include <vector>

#include "core/model/Models.h"

class QThread;

namespace Ui {
class JanelaTurma;
}

namespace sigaa {
namespace http { class SigaaSession; }
namespace sync { class SessaoTurma; }
}

namespace sigaa::ui {

// Raiz onde o material das turmas é salvo: Documentos/SIGAA.
//
// Vive aqui, e é usada também pela janela principal, porque o "Atualizar tudo"
// baixa para o MESMO lugar de onde esta janela lê. Duas definições que
// divergissem num acento fariam o sync encher uma pasta e a janela procurar
// noutra — o app mostraria "ainda não baixado" para arquivo que está no disco.
// O nome da subpasta de cada turma sai de `sync::pastaDaTurma`.
QString pastaBaseMateriais();

class JanelaTurma : public QDialog {
    Q_OBJECT

public:
    // ABRE OFFLINE. `topicos` e `arquivos` vêm do banco, que é onde os ciclos
    // de sincronização já depositaram a turma inteira — a janela pinta na hora
    // e não fala com o SIGAA.
    //
    // A sessão só nasce quando alguém pede um arquivo que não está no disco, ou
    // clica em Atualizar. Antes disso a janela custava um login e três
    // requisições toda vez que era aberta, para mostrar dados que já estavam
    // guardados; o "Entrando na turma…" era o app buscando o que já tinha.
    //
    // `login`/`senha` são cópias desta execução, guardadas para esse momento.
    // Não vão para o disco.
    JanelaTurma(Turma turma, std::vector<TopicoAula> topicos,
                std::vector<ArquivoTurma> arquivos, std::string login,
                std::string senha, QWidget* pai = nullptr);
    ~JanelaTurma() override;

protected:
    // Tanto accept() quanto reject() passam por aqui — inclusive o X da barra
    // de título e o Esc. É o único ponto que pega TODAS as formas de fechar.
    void done(int r) override;

private:
    // Login + entrar na turma + reler a aba Arquivos. Chamada pelo botão
    // Atualizar e, sob demanda, antes do primeiro download.
    void atualizarDoSigaa();

    // Garante uma sessão aberta nesta turma. Devolve false se não deu — e a
    // mensagem já foi para a barra de status.
    //
    // Existe porque baixar exige um POST na view corrente do SIGAA: não há
    // URL estável para o arquivo (RECON §1.6.1), então o download offline-first
    // acaba aqui, e só aqui.
    bool garantirSessao(std::string* erro);

    // "Abrir": o que já está no disco abre direto, sem tocar na rede. Baixar de
    // novo o que a pessoa já tem é gastar o tempo dela e a paciência do SIGAA
    // para produzir um arquivo idêntico.
    void abrirSelecionados();
    void rebaixarSelecionados();  // o botão para quem quer se certificar
    void baixarTudo();

    // O caminho de verdade dos botões. Decide sozinho entre reaproveitar a
    // sessão que já está aberta (poucos arquivos) e abrir canais paralelos
    // (muitos) — ver core/sync/Baixador.h.
    void baixarIds(const QStringList& ids, bool abrirOPrimeiro, bool forcar = false);

    // Abre com o leitor padrão o arquivo local de `id`. Falso se não houver.
    bool abrirLocal(const QString& id);

    void mostrarConteudo();
    void relerCacheOffline();

    // Ids selecionados na aba que está à frente. Selecionar uma AULA inteira
    // devolve os materiais baixáveis dela: é o gesto óbvio, e reclamar de
    // "seleção inválida" seria o app fingindo não entender.
    QStringList idsSelecionados() const;

    void ocupado(bool sim);
    void status(const QString& msg);

    // Pasta de destino: Documentos/SIGAA/<turma>. Fixa de propósito — um
    // seletor de pasta a cada arquivo transformaria "baixar tudo" em suplício,
    // e o botão "Abrir pasta" resolve o "onde foi parar".
    QString pastaDestino() const;

    std::unique_ptr<Ui::JanelaTurma> formulario_;

    Turma turma_;
    std::string login_;
    std::string senha_;

    // Vivem enquanto a janela viver: a sessão precisa sobreviver entre um
    // download e o próximo, senão cada arquivo custaria um login novo.
    std::unique_ptr<http::SigaaSession> sessao_;
    std::unique_ptr<sync::SessaoTurma> turmaRemota_;

    std::vector<ArquivoTurma> arquivos_;
    std::vector<TopicoAula> topicos_;

    // idArquivo -> caminho local do que já está no disco, segundo o manifesto
    // da pasta da turma. Relido do disco a cada mudança — o aluno apaga
    // arquivo, e "offline ✓" mentiroso o faria fechar o app sem o material.
    //
    // A chave é o `id` do SIGAA, não o nome: o nome é texto livre do professor
    // e muda sem que o arquivo mude. Enquanto o id for o mesmo, é o mesmo
    // material, e o que está no disco serve.
    QHash<QString, QString> offline_;

    bool ocupado_{false};

    // A thread em andamento, ou nullptr.
    //
    // Ela ESCREVE em sessao_/turmaRemota_, que são membros desta janela. Se a
    // janela morresse antes dela, o corpo da thread continuaria mexendo em
    // memória liberada — e um PDF de 11 MB demora o bastante para alguém
    // clicar no X no meio. Por isso `done()` recusa fechar enquanto há
    // trabalho, e o destrutor espera como última linha de defesa.
    QThread* trabalho_{nullptr};
};

} // namespace sigaa::ui
