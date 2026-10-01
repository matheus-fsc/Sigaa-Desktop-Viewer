#pragma once
// `sigaa-cli web`: o acesso mobile sem abrir janela (docs/WEB.md).
//
//   sigaa-cli web [--escutar <ip>[:<porta>]] [--banco <db>] [--materiais <dir>]
//   sigaa-cli web token [--novo]
//
// Para quem deixa o app num computador sem tela, ou prefere um serviço do
// systemd a manter a janela aberta. A janela e o CLI leem o mesmo token do
// banco: parear num serve para o outro.

namespace sigaa::web {

// argv[1] == "web". Devolve o código de saída do processo.
int comando(int argc, char** argv);

} // namespace sigaa::web
