# Roadmap

Status atual do projeto e o que está em aberto.

## Implementado

- Suporte a múltiplas instituições. Só a UNIFEI está marcada como verificada;
  outras instâncias do SIGAA podem ser apontadas com `--url`, por conta e risco.
- Autenticação com cofre nativo do sistema (Credential Manager no Windows,
  Secret Service via libsecret no Linux), com login e senha num blob cifrado.
- Sincronização das turmas do período, atividades e atualizações do portal.
- Tópicos de aula, com o material publicado dentro de cada um.
- Provas: extraídas do painel de avaliações e inferidas do título dos tópicos de
  aula, com merge que sabe em qual fonte confiar.
- Download de materiais com cache por id do SIGAA e paralelismo por sessão
  (teto de 3 canais, cada um com login próprio).
- Notificações do sistema, agrupadas e priorizadas.
- Relatório HTML do snapshot e exportação da agenda de provas em iCal (`.ics`).
- Banco SQLite local, com migração de schema e UPSERT que nunca apaga o que a
  coleta parcial não trouxe.
- Motor de diff que detecta o que mudou entre sincronizações, incluindo prova
  remarcada e coleta suspeita.
- CLI completa:
  - Rede: `sync`, `login`, `logout`, `doctor`, `arquivos`, `baixar`, `explorar`.
  - Offline, sobre HTML salvo: `forms`, `links`, `post`, `parse`, `report`.
  - Opções globais de instituição: `--instituicao`, `--url`.
- Interface Qt 6 Widgets, opcional no build, com as abas **Hoje**, **Provas**,
  **Turmas** e **Atualizações**, mais a janela da turma com tópicos, arquivos e
  download sob demanda.
- Tema claro e escuro acompanhando o sistema, sem cor literal no stylesheet.
- Empacotamento em `dist/` pelo `tools/empacotar.ps1`, que roda os testes antes
  e recusa embarcar banco, relatório ou `.env` de quem compilou.

## Planejado

Boa parte disto é trabalho bem delimitado, e serve como porta de entrada.

- **Notas**: o parser não existe. É o item mais pedido.
- **Faltas e frequência**: idem, parser não escrito.
- **Tarefas e envios**: aba de upload de atividade.
- **Fóruns da turma**.
- **Notícias da instituição**.
- **Diálogo de preferências**: hoje o intervalo do ciclo é fixo no código, 20
  minutos para o portal e 6 horas para entrar nas turmas
  (`kMinutosPortal` e `kMinutosTurmas`, em `src/ui/JanelaPrincipal.cpp`).
- **Bandeja do sistema**: minimizar em vez de fechar.
- **Cofre no macOS**, com `Security.framework`. Existe preset de build para
  macOS, mas o cofre ainda não tem implementação lá.
- **Material de tópico que não é arquivo** (link externo, vídeo): hoje é
  ignorado. O certo é mostrar desabilitado ou abrir no navegador, em vez de
  oferecer um download que falha.
- **Download com streaming**: hoje o arquivo inteiro passa pela memória antes de
  ir para o disco. Um vídeo grande publicado pelo professor pode derrubar o app
  por falta de memória.
- **Verificar outras instituições**: acrescentar entrada ao catálogo sem
  conferir contra o site real é pior que não ter, porque o usuário culpa a
  própria senha quando o parser é que não entende a página.
- **Internacionalização**: as strings estão fixadas em português.
- **Atualização automática** do aplicativo.

Fora de escopo por decisão de projeto: **nada roda com o app fechado**. Sem
serviço, sem tarefa agendada por padrão. O ciclo periódico é um `QTimer` na
janela. O `tools/agendar.ps1` continua no repositório como opção desligada.

## Como pegar um item

1. Procure ou abra uma issue para o item.
2. Comente que você quer assumir, para ninguém duplicar trabalho.
3. Olhe as labels:
   - **`good first issue`**: normalmente parser (Notas, Faltas). Ótimo se você
     quer treinar C++ e HTML sem precisar conhecer Qt.
   - **`help wanted`**: integração com sistema operacional (cofre no macOS,
     bandeja) ou problema mais espinhoso.
4. Antes de escrever parser, leia `docs/RECON.md` e capture o HTML com
   `sigaa-cli explorar`. Detalhes em [[Protocolo-SIGAA]] e
   [[Referencia-da-API]].

Toda contribuição é bem-vinda.
