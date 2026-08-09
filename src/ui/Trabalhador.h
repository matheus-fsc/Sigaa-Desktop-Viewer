#pragma once
// Roda um ciclo de sync fora da thread da interface.
//
// `servico::executar` bloqueia: um ciclo com --turmas leva ~25 s por causa do
// intervalo mínimo entre requisições (que existe por etiqueta, não por acaso —
// ver docs/PLANO.md §2.4). Chamar isso na thread da GUI congelaria a janela por
// meio minuto e o Windows a pintaria de "não está respondendo".
//
// O `Resultado` NÃO viaja por signal: fica no objeto e é lido depois de
// `concluido()`. Assim não é preciso registrar meta-tipo para uma struct que
// carrega um Snapshot inteiro, e não há cópia.

#include <QString>
#include <QThread>

#include "core/servico/Servico.h"

namespace sigaa::ui {

class Trabalhador : public QThread {
    Q_OBJECT

public:
    explicit Trabalhador(servico::Opcoes op, QObject* pai = nullptr);

    // Válido apenas depois de `concluido()`. A entrega em fila do signal
    // sincroniza a escrita feita em run() com a leitura na thread da GUI.
    const servico::Resultado& resultado() const { return resultado_; }

signals:
    void passo(const QString& msg);
    void aviso(const QString& msg);
    void concluido();

protected:
    void run() override;

private:
    servico::Opcoes op_;
    servico::Resultado resultado_;
};

} // namespace sigaa::ui
