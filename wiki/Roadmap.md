# Roadmap do SIGAA Desktop Viewer

Este documento lista o status atual do projeto e mapeia o que planejamos implementar no futuro.

## Atualmente Implementado (✅)

Aqui está tudo que nosso cliente já suporta:

- Suporte a múltiplas instituições (UNIFEI verificado, outras instituições usando SIGAA básico não testadas)
- Autenticação e cofre de senhas local criptografado (Integrações com Credential Manager do Windows e Libsecret no Linux)
- Sincronização e listagem das turmas matriculadas no semestre
- Rastreamento e painel de Provas (extraídas do painel de avaliações e inferidas do HTML de tópicos de aula)
- Download paralelo de arquivos e materiais de aula (com cache)
- Sistema unificado de notificações na área de trabalho (Agrupadas e com níveis de prioridade)
- Exportação de agenda de exames no formato iCal `.ics`
- CLI completo (Comandos: `sync`, `login`, `logout`, `doctor`, `arquivos`, `baixar`)
- Interface Gráfica de Usuário (GUI) Qt6 incluindo abas:
  - **Hoje** (Visão do dia)
  - **Provas** (Próximas avaliações)
  - **Turmas** (Disciplinas e detalhes)
  - **Atualizações** (Notificações, notas novas, etc.)
- Temas Escuro e Claro no Qt.
- Cache offline veloz em banco SQLite.
- _Diff engine_: Mecanismo para detectar o que mudou entre sincronizações para emitir notificações granulares.

## Planejado / Ainda Não Implementado (🔲)

Ainda há muitas fronteiras para explorar. Eis a lista do que **não** temos e que pode ser adicionado em breve:

- Extração e aba de **Notas** (Parser não escrito ainda)
- Extração e aba de **Faltas/Presença** (Parser não escrito ainda)
- Aba de **Tarefas e Envios** (Upload de atividades)
- Aba de **Fóruns da Turma**
- Aba de **Notícias da Instituição**
- Diálogo de Preferências e Configurações no App (atualmente o intervalo de sync é cravado em 20 minutos no código)
- Minimizar o App para a bandeja de sistema (System Tray) ao fechar a janela
- Integração de Cofre no macOS (usando `Security.framework`)
- Materiais nos Tópicos de Aula que não são arquivos (Links externos, vídeos). Hoje são ignorados ou não baixam. (Devem ficar em "cinza" ou abrirem no navegador).
- **Downloads com Streaming:** Atualmente o aplicativo baixa e armazena todo o payload de rede na memória RAM antes de escrever no disco. Se o professor colocar um vídeo pesado, o aplicativo vai engolir a memória do computador até fechar (OOM).
- Testar e refinar a compatibilidade para instituições além da UNIFEI.
- Internacionalização (i18n): Todas as strings, atualmente, estão _hardcoded_ em Português-BR.
- Mecanismo de Atualização Automática (_Auto-update_).

## Como Contribuir para o Roadmap

> [!NOTE]
> Você quer matar um item dessa lista? Siga os passos:

1. Acesse a aba **Issues** deste repositório e crie uma ou encontre a já existente para sua feature.
2. Comente que você tem interesse em assumir o trabalho.
3. Fique de olho nas labels do Issue:
   - **`good first issue`**: Geralmente marca o trabalho de fazer parsers (ex: Aba de Notas, Faltas). É excelente se você só quer treinar C++, mexer com HTML, RegEx e não tem muito domínio do framework gráfico (Qt).
   - **`help wanted`**: Marcado para integrações difíceis no sistema operacional (ex: Keychain de macOS, System Tray) ou problemas cabeludos.
4. Consulte os documentos técnicos do protocolo para ajudar no desenvolvimento (`wiki/Protocolo-SIGAA.md`).

Toda e qualquer contribuição é muito bem vinda!
