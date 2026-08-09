#pragma once
// Notificação nativa do sistema. Única parte do projeto que fala com o SO.
//
// LIMITAÇÃO HONESTA, válida em todos os backends: isto é um *empurrãozinho*,
// não o registro. Um processo de sync que roda e morre não consegue manter a
// notificação viva na Central de Ações do Windows — o balão morre junto com o
// ícone de bandeja, que morre junto com o processo. O registro durável são o
// relatório HTML e o .ics, que ficam no disco. Se a notificação for perdida
// porque o usuário estava longe do PC, nada se perdeu de fato.
//
// Notificação persistente exige processo residente (ícone de bandeja) — é
// assunto da Fase 3, junto da UI.

#include <string>

namespace sigaa::plat {

struct Notificacao {
    std::string titulo;      // UTF-8; será truncado ao limite do backend
    std::string corpo;       // UTF-8; '\n' separa linhas
    std::string aoClicar;    // caminho/URL aberto no clique; vazio = sem ação
    bool urgente{false};     // muda o ícone (aviso vs. informação)
};

// Best-effort: devolve false quando o backend não está disponível (SSH sem
// display, notify-send ausente...). Nunca lança, nunca trava o chamador.
bool notificar(const Notificacao& n, std::string* erro = nullptr);

// Nome do backend, para o comando `doctor`.
std::string backendNotificacao();

} // namespace sigaa::plat
