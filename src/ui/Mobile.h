#pragma once
// A aba Acesso mobile: liga o servidor que mostra as telas no celular pela
// VPN (src/web, docs/WEB.md), pareia o celular e diz como chegar até ele.
//
// O servidor roda DENTRO deste processo, numa thread do cpp-httplib, e vive
// enquanto o app estiver aberto (inclusive minimizado na bandeja). Quem quer
// sem janela usa `sigaa-cli web`.
//
// O ENDEREÇO É UMA LISTA, e não um campo livre: as interfaces deste
// computador, com a da VPN marcada. "Todas as interfaces" (0.0.0.0) não está
// nela — é o erro que põe o app no Wi-Fi da faculdade, e o servidor recusa
// mesmo que chegue por outro caminho (web/Pareamento.h).

#include <QPointer>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace sigaa::web {
class Servidor;
struct Acesso;
}

namespace sigaa::ui {

class PainelMobile : public QWidget {
public:
    PainelMobile(QString banco, QString materiais, QWidget* pai = nullptr);
    ~PainelMobile() override;

    // Chamado uma vez pela janela, depois de montada: liga o servidor se o
    // aluno marcou "ligar sozinho".
    void ligarSeAutomatico();

    bool noAr() const;

    // O estado mudou (ligou, desligou, caiu): a janela atualiza o nome da aba.
    std::function<void(bool noAr)> aoMudar;

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void ligar(bool silencioso = false);
    void salvarPin();
    void mostrarPin(const QString& recado = {}, bool erro = false);
    void listarAparelhos();
    // Um aparelho, ou todos com 0.
    void desconectar(std::int64_t id);
    void desligar();
    void listarEnderecos();
    void atualizarEstado(const QString& recado = {});
    void mostrarPareamento();
    void gerarNovoCodigo();
    void registrarAcesso(const web::Acesso& a);
    QString enderecoEscolhido() const;
    QString interfaceEscolhida() const;
    void salvar();

    QString banco_;
    QString materiais_;
    std::unique_ptr<web::Servidor> servidor_;

    QLabel* estado_{nullptr};
    QPushButton* botaoLigar_{nullptr};
    QCheckBox* automatico_{nullptr};
    QComboBox* endereco_{nullptr};
    QSpinBox* porta_{nullptr};
    QLabel* sobreEndereco_{nullptr};

    QLabel* qr_{nullptr};
    QLineEdit* link_{nullptr};
    QPushButton* botaoCopiar_{nullptr};
    QPushButton* botaoNovo_{nullptr};

    QLabel* enderecoCurto_{nullptr};
    QLineEdit* pin_{nullptr};
    QPushButton* botaoPin_{nullptr};
    QPushButton* botaoRemoverPin_{nullptr};
    QLabel* estadoPin_{nullptr};

    QLabel* resumoAparelhos_{nullptr};
    QTableWidget* aparelhos_{nullptr};
    QPushButton* botaoTodos_{nullptr};
    QTimer* relogioAparelhos_{nullptr};

    QTableWidget* acessos_{nullptr};

    // Ligar sozinho com a VPN ainda subindo: tenta de novo a cada 30 s, em
    // vez de desistir no boot e deixar o aluno descobrir no ônibus.
    QTimer* reTentar_{nullptr};
};

} // namespace sigaa::ui
