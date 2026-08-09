#include "ui/Trabalhador.h"

#include <utility>

namespace sigaa::ui {

Trabalhador::Trabalhador(servico::Opcoes op, QObject* pai)
    : QThread(pai), op_(std::move(op)) {}

void Trabalhador::run() {
    resultado_ = servico::executar(std::move(op_), [this](servico::Nivel n,
                                                          std::string_view m) {
        const QString msg = QString::fromUtf8(m.data(), static_cast<int>(m.size()));
        // Emitir de dentro da thread trabalhadora é seguro: a conexão com a
        // janela é em fila (threads diferentes), então o slot roda na GUI.
        if (n == servico::Nivel::Aviso) emit aviso(msg);
        else emit passo(msg);
    });
    emit concluido();
}

} // namespace sigaa::ui
