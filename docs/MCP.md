# Agentes de IA — servidor MCP e conector

> Plano de arquitetura, **implementado no branch `mcp`** (fases 0 a 4). O que
> mudou em relação ao plano, e o que ficou de fora, está em §13.

## 1. Objetivo

O aluno quer estudar com um agente (Claude, Codex, Gemini/Antigravity, Cursor…)
sem arrastar PDF, copiar tópico de aula e colar notícia do professor. O app já
tem tudo isso no disco — o banco SQLite com turmas, tópicos, provas, notícias,
frequência, e a pasta `~/Documentos/SIGAA/<Turma>/` com os PDFs e o `turma.md`.

Três entregas:

1. **Ler** — o agente consulta os dados do aluno por um servidor MCP local.
2. **Conectar** — um comando (e um botão na UI) registra o servidor no agente
   que o aluno usa, sem editar JSON/TOML na mão.
3. **Devolver** — o agente grava de volta o que observou: tempo de estudo,
   desempenho em exercícios, pontos em que o aluno precisa focar. O app mostra.

O que **não** é objetivo: o agente agir no SIGAA (postar, enviar tarefa,
mudar nada lá), nem o app virar um cliente de IA.

## 2. Decisões

### D1. MCP sobre stdio, servido pelo próprio `sigaa-cli`

`sigaa-cli mcp` lê JSON-RPC 2.0 do stdin e responde no stdout, uma mensagem por
linha. É o transporte que **todos** os agentes de terminal e de IDE suportam
(Claude Code, Claude Desktop, Codex CLI, Gemini CLI, Antigravity, Cursor,
VS Code), não abre porta nenhuma e morre junto com o agente.

Alternativas descartadas:

| Alternativa | Por que não |
|---|---|
| Servidor em Python/TypeScript lendo o SQLite | Exige runtime que o aluno não tem; duplicaria as regras do core (provas efetivas com ajustes, matéria entre provas, `turma.md`) e elas divergiriam. |
| HTTP local (Streamable HTTP) | Porta aberta é superfície de ataque (qualquer processo, qualquer aba do navegador via DNS rebinding) e pede autenticação. Só vale para clientes web — ver Fase 5. |
| Executável separado `sigaa-mcp` | Mais um binário para empacotar, assinar e o atualizador trocar. Um subcomando custa nada e herda `--url`, cofre e log. |

O protocolo é implementado à mão em C++ sobre o `nlohmann::json` que o
projeto já usa: `initialize`, `tools/*`, `resources/*`, `prompts/*`, `ping`.
São poucas centenas de linhas; não há SDK C++ oficial, e um de terceiros seria
uma dependência maior que o código que ele economiza.

Versão do protocolo: anunciar **2025-06-18** (que traz `structuredContent`,
`outputSchema` e `resource_link`) e aceitar negociar 2025-03-26 e 2024-11-05
com clientes antigos.

**Regra de ouro do stdio: nada além de protocolo no stdout.** spdlog, avisos do
core e o `--log-http` vão para o stderr. Um `std::cout` esquecido corrompe a
sessão do agente sem mensagem de erro útil — vira teste (§10).

### D2. O servidor vive em `src/mcp/`, sem Qt, sobre o core

```
src/mcp/
  Protocolo.{h,cpp}    JSON-RPC: ler linha, despachar, responder, erros
  Servidor.{h,cpp}     initialize, capabilities, consentimento, auditoria
  Leitura.{h,cpp}      ferramentas e recursos de leitura
  Escrita.{h,cpp}      ferramentas de devolução (Fase 3)
  Prompts.{h,cpp}      prompts prontos
  Instalar.{h,cpp}     `sigaa-cli mcp instalar <cliente>` (Fase 2)
```

Biblioteca `sigaa_mcp` (estática), ligada ao `sigaa-cli` e aos testes. A UI
**não** liga com ela; só chama `Instalar` para o botão "Conectar agente".

O que hoje mora na UI e o servidor precisa passa para o core **antes**:

- `materiaDaProva` (`src/ui/Modelos.cpp`) → `core/avaliacao/` — é a resposta
  para "o que cai na prova 2", a pergunta mais valiosa que o agente pode fazer.
- O local da pasta de materiais (`pastaBaseMateriais`, hoje via
  `QStandardPaths`) → `core/util/` com a mesma regra por plataforma.

### D3. Onde estão os dados: o caminho vai no registro do servidor

Hoje o banco é `./sigaa-viewer.db` relativo ao diretório de trabalho (no macOS,
com recuo para Application Support). O agente lança o servidor de uma pasta
qualquer — o servidor **não pode** adivinhar.

Ordem de resolução no `sigaa-cli mcp`:

1. `--banco <caminho>` e `--materiais <pasta>` nos argumentos;
2. `SIGAA_BANCO` / `SIGAA_MATERIAIS` no ambiente;
3. `./sigaa-viewer.db`, se existir;
4. falha com mensagem dizendo exatamente o que configurar.

Quem registra o servidor (o `instalar`, ou o botão na UI) **sabe** onde estão o
banco e a pasta — é o mesmo processo que os usa — e grava os caminhos absolutos
na configuração do agente. O aluno nunca digita caminho.

> Uma pasta de dados canônica por plataforma (como o macOS já faz) resolveria
> isso de vez para CLI, UI e MCP. É uma mudança maior, com migração do banco
> existente; fica registrada como melhoria e não bloqueia este plano.

### D4. Consentimento explícito, guardado no banco

Mandar dados para um agente na nuvem é mandar dados para fora do computador.
O servidor recusa tudo até o aluno ligar, e liga por categoria:

| Chave em `meta` | Padrão | Libera |
|---|---|---|
| `mcp.leitura` | desligado | turmas, tópicos, provas, prazos, notícias, frequência, plano |
| `mcp.arquivos` | desligado | caminho e conteúdo dos materiais baixados |
| `mcp.escrita` | desligado | as ferramentas de devolução (§5) |
| `mcp.rede` | desligado | ferramentas que falam com o SIGAA (Fase 4) |

No banco (tabela `meta`, que já existe), e não em `QSettings`: o servidor roda
dentro do `sigaa-cli`, que não lê as configurações da UI. Desligado, o servidor
continua respondendo `initialize` e lista as ferramentas, mas a chamada devolve
um erro legível — "Ative em Opções → Agentes de IA" — que o agente repassa.

### D5. Turmas por nome, não só por id

O aluno diz "compiladores", não "89151". Toda ferramenta aceita `turma` como id
**ou** trecho do nome, sem acento e sem caixa. Ambíguo ("algoritmos" casa com
duas) devolve erro listando as opções, para o agente perguntar.

### D6. Arquivos: caminho primeiro, conteúdo como recurso

Agentes de terminal (Claude Code, Codex, Gemini CLI) leem arquivo do disco com
as próprias ferramentas — e leem PDF melhor do que qualquer extração que o app
faria. Então `listar_arquivos` devolve o **caminho absoluto** de cada material
baixado, e um `resource_link` para `sigaa://arquivo/{id}`.

Para clientes sem acesso ao disco (Claude Desktop), `resources/read` desse URI
devolve o arquivo como `blob` base64, com teto de 10 MB. Extrair texto de PDF
no app (poppler) fica em aberto (§12) — é dependência nova para os três SOs.

## 3. Visão geral

```
 ┌──────────────┐  stdio (JSON-RPC)  ┌───────────────────────────────┐
 │ Agente       │ ─────────────────▶ │ sigaa-cli mcp                 │
 │ Claude/Codex │ ◀───────────────── │  Protocolo → Servidor         │
 │ Gemini/…     │                    │    ├─ consentimento (meta)    │
 └──────────────┘                    │    ├─ auditoria (mcp_acesso)  │
        │ lê PDFs pelo caminho       │    ├─ Leitura ──┐             │
        ▼                            │    ├─ Escrita ──┤             │
 ~/Documentos/SIGAA/<Turma>/         │    └─ Prompts   │             │
   turma.md, *.pdf                   └─────────────────┼─────────────┘
                                                       ▼
                                      core/ (Database, TurmaMd, Ajustes,
                                      materiaDaProva, planejamento)
                                                       │
                                     sigaa-viewer.db (WAL) ◀── sigaa-ui
```

UI e servidor abrem o **mesmo** banco ao mesmo tempo. O banco já está em WAL;
falta `busy_timeout` (§9).

## 4. Superfície de leitura

### Ferramentas

Toda resposta leva `structuredContent` (JSON com `outputSchema`) **e** um
`text` em Markdown curto, para clientes que só leem texto.

| Ferramenta | Argumentos | Devolve |
|---|---|---|
| `listar_turmas` | — | id, nome, código, horário, local, faltas/limite |
| `resumo_da_turma` | `turma` | o `turma.md` (já gerado por `core/report/TurmaMd`): tópicos, arquivos, provas, frequência, notícias |
| `topicos_de_aula` | `turma`, `desde?`, `ate?` | tópicos com data, título e conteúdo |
| `listar_provas` | `turma?`, `dias?` (padrão 60) | provas efetivas: data, hora, local, estado (do SIGAA, deduzida, corrigida, confirmada) |
| `materia_da_prova` | `turma`, `prova` | tópicos entre a prova anterior e esta + os arquivos ligados a eles |
| `listar_prazos` | `dias?` | atividades pendentes com prazo |
| `listar_arquivos` | `turma`, `topico?` | id, título, tópico, baixado?, caminho, `resource_link` |
| `noticias` | `turma`, `limite?` | notícias do professor, da mais nova |
| `frequencia` | `turma` | faltas, limite, dias com falta |
| `meu_progresso` | `turma?` | o que agentes já devolveram (§5): horas estudadas, desempenho por tópico, pontos de foco abertos |

`meu_progresso` é o que fecha o ciclo: numa conversa nova, o agente sabe o que
outra conversa (ou outro agente) já viu.

### Recursos

| URI | Conteúdo |
|---|---|
| `sigaa://turmas` | lista, JSON |
| `sigaa://turma/{id}/resumo.md` | o `turma.md` |
| `sigaa://turma/{id}/topicos` | JSON |
| `sigaa://arquivo/{id}` | o material, `blob` (só com `mcp.arquivos`) |

Os três primeiros também como *resource templates*, para clientes que deixam o
aluno anexar um recurso à conversa ("@sigaa compiladores").

### Prompts

Atalhos que o aluno escolhe no menu do agente:

| Prompt | Argumentos | Faz o agente… |
|---|---|---|
| `estudar_para_prova` | `turma`, `prova` | ler a matéria da prova e os PDFs, montar um roteiro, e registrar o estudo ao fim |
| `simulado` | `turma`, `prova`, `questoes?` | gerar questões da matéria, corrigir, e registrar desempenho por tópico |
| `revisao_da_semana` | — | olhar provas e prazos dos próximos 7 dias e propor o que estudar |
| `explicar_topico` | `turma`, `topico` | explicar a aula a partir do conteúdo do tópico e do material dele |

O texto de cada prompt diz **quais ferramentas chamar** e **quando devolver
dados**. É o que faz um agente qualquer seguir o mesmo fluxo sem o aluno saber
o nome das ferramentas.

## 5. Devolução (agente → app)

### Ferramentas

| Ferramenta | Argumentos | Grava |
|---|---|---|
| `registrar_estudo` | `turma`, `minutos`, `topicos[]?`, `prova?`, `quando?`, `observacao?` | uma sessão estudada |
| `registrar_desempenho` | `turma`, `topico`, `acertos`, `total`, `tipo` (`simulado`/`exercicio`/`revisao`), `quando?` | resultado num tópico |
| `marcar_foco` | `turma`, `topico`, `nivel` (1–3), `motivo` | ponto em que o aluno tem dificuldade |
| `resolver_foco` | `id`, `motivo?` | fecha um ponto de foco |

### Tabelas

```sql
-- Toda linha diz QUEM escreveu (clientInfo.name do initialize) e QUANDO.
-- O aluno vê e apaga qualquer uma na UI; nenhuma é apagada pelo sync.
CREATE TABLE registro_estudo (
  id INTEGER PRIMARY KEY, origem TEXT NOT NULL, criado_em INTEGER NOT NULL,
  id_turma TEXT NOT NULL, prova TEXT, quando TEXT NOT NULL,
  minutos INTEGER NOT NULL CHECK (minutos BETWEEN 1 AND 720),
  topicos TEXT,            -- JSON array
  observacao TEXT
);
CREATE TABLE desempenho (
  id INTEGER PRIMARY KEY, origem TEXT NOT NULL, criado_em INTEGER NOT NULL,
  id_turma TEXT NOT NULL, topico TEXT NOT NULL, tipo TEXT NOT NULL,
  acertos INTEGER NOT NULL, total INTEGER NOT NULL CHECK (total BETWEEN 1 AND 500),
  quando TEXT NOT NULL
);
CREATE TABLE ponto_foco (
  id INTEGER PRIMARY KEY, origem TEXT NOT NULL, criado_em INTEGER NOT NULL,
  atualizado_em INTEGER NOT NULL,
  id_turma TEXT NOT NULL, topico TEXT NOT NULL,
  nivel INTEGER NOT NULL CHECK (nivel BETWEEN 1 AND 3),
  motivo TEXT, resolvido INTEGER NOT NULL DEFAULT 0,
  UNIQUE (id_turma, topico, resolvido)
);
CREATE TABLE mcp_acesso (       -- auditoria, ver §7
  quando INTEGER NOT NULL, origem TEXT, ferramenta TEXT NOT NULL,
  turma TEXT, ok INTEGER NOT NULL
);
```

`marcar_foco` num tópico já aberto **atualiza** o ponto (nível e motivo),
em vez de duplicar — dez conversas não viram dez cartões iguais.

### Validação

O servidor recusa, com mensagem que o agente entende: turma que não existe,
`minutos` fora de 1–720, `acertos > total`, data no futuro em `registrar_*`,
texto acima de 2 000 caracteres, mais de 60 escritas por minuto. Tópico é texto
livre (o agente pode nomear um subtópico), mas a resposta diz se ele casou com
um tópico de aula conhecido.

### Onde aparece

A aba Estudo ganha a página **Progresso**: horas por matéria e por semana,
desempenho por tópico, pontos de foco abertos (com origem e botão de apagar).
`registro_estudo` também desconta do tempo que o planejamento reserva para a
prova — estudo feito com o agente é estudo feito.

> Isso depende do redesenho da aba Estudo (§11, Fase 0): as páginas atuais
> estão confusas e não vão para o próximo deploy.

## 6. Conector

### `sigaa-cli mcp instalar <cliente>`

Escreve a entrada do servidor na configuração do agente, com o caminho
**absoluto** do executável e os `--banco`/`--materiais` resolvidos (D3):

| Cliente | Como |
|---|---|
| Claude Code | `claude mcp add --scope user sigaa -- <exe> mcp --banco … --materiais …` |
| Claude Desktop | `mcpServers.sigaa` em `claude_desktop_config.json` |
| Codex CLI | `[mcp_servers.sigaa]` em `~/.codex/config.toml` |
| Gemini CLI | `mcpServers.sigaa` em `~/.gemini/settings.json` |
| Antigravity | `mcpServers.sigaa` no `mcp_config.json` (*caminho a confirmar*) |
| Cursor / VS Code | `.cursor/mcp.json` / `mcp.json` do usuário |

Regras: lê o arquivo existente e só mexe na chave `sigaa` (nunca reescreve o
resto); guarda uma cópia `.bak` antes; `--imprimir` mostra o trecho sem gravar,
para quem prefere colar à mão; `remover <cliente>` desfaz.

**Caminho do executável no AppImage:** é `$APPIMAGE` (o arquivo `.AppImage`),
nunca `/proc/self/exe`, que aponta para o ponto de montagem temporário em
`/tmp/.mount_*` e some quando o app fecha. Depois de uma atualização que mude o
nome do arquivo, o app reinstala as entradas que encontrar.

### Na UI: Opções → Agentes de IA

- Os quatro interruptores de consentimento (D4), com uma frase sobre o que cada
  um expõe.
- "Conectar a…" com um botão por cliente detectado (o executável ou a pasta de
  config existe) e o resultado ("Conectado ao Claude Code. Reinicie o agente.").
- "Atividade recente": as últimas linhas de `mcp_acesso`.

### Sem MCP: o kit de estudo

Para quem usa um chat na web (ChatGPT, claude.ai, Gemini web), que não roda
servidor local: **Exportar kit de estudo** gera, para uma turma ou uma prova,
uma pasta (ou `.zip`) com `turma.md`, a matéria da prova, os PDFs dela e um
`LEIA-ME-AGENTE.md` com as instruções que os prompts de §4 dariam. Arrasta-se
**uma** pasta em vez de vinte arquivos. Sem devolução — esse é o preço de não
ter MCP.

## 7. Segurança e privacidade

| Risco | Tratamento |
|---|---|
| Dados do aluno saem do computador | Consentimento por categoria (D4), desligado por padrão; README e a tela de Opções dizem que o provedor do agente recebe o que o agente lê. |
| Senha do SIGAA | Nunca exposta. O servidor não tem ferramenta que leia o cofre; a rede (Fase 4) usa o cofre por dentro, como o `sync`. |
| **Dados de terceiros** (participantes: colegas, e-mail, matrícula) | **Fora do MCP**, sem exceção. LGPD: o aluno pode consentir com os dados dele, não com os dos colegas. |
| Injeção de prompt vinda do SIGAA (notícia ou tópico com "ignore as instruções…") | O conteúdo do professor vai em campos de dado (`structuredContent`), nunca concatenado em instrução; a descrição das ferramentas avisa que é texto de terceiros. |
| Injeção no sentido contrário (agente grava HTML/script que a UI renderiza) | Tudo que vem do agente é exibido como texto puro (`Qt::PlainText`), nunca rich text. |
| Agente em laço gravando lixo | Limites de §5 e auditoria; a UI apaga por origem ("apagar tudo que o Codex gravou"). |
| Agente martelando o SIGAA | Rede só na Fase 4, desligada por padrão, com orçamento por sessão (ex.: 20 requisições) e intervalo mínimo — o mesmo cuidado de "o SIGAA bloqueia conta que consulta rápido demais" que a rotina automática já tem. |
| Porta aberta | Não há: stdio. |

## 8. Fluxos de exemplo

**"Me ajuda a estudar para a P2 de compiladores"** (Claude Code, com o prompt
`estudar_para_prova`):

1. `listar_provas(turma="compiladores")` → P2 em 21/10, data confirmada.
2. `materia_da_prova("compiladores", "P2")` → 6 tópicos desde a P1, 4 PDFs.
3. `meu_progresso("compiladores")` → foco aberto em "análise LL(1)".
4. Lê os PDFs pelos caminhos, explica, propõe exercícios.
5. `registrar_desempenho(…, topico="análise LL(1)", acertos=3, total=5)`.
6. `marcar_foco(…, topico="FIRST/FOLLOW", nivel=2, motivo="errou 2 de 3")`.
7. `registrar_estudo(…, minutos=70, topicos=[…], prova="P2")`.

Na aba Estudo → Progresso: 1h10 em Compiladores, LL(1) 60%, FIRST/FOLLOW em foco.

## 9. Concorrência com a UI

- `PRAGMA busy_timeout = 3000` ao abrir o banco, no core — hoje uma escrita do
  servidor durante um sync da UI devolveria `SQLITE_BUSY` na hora.
- Escritas do servidor são transações curtas, uma linha por chamada.
- A UI percebe dado novo sem recarregar tudo: `PRAGMA data_version` checado
  quando a janela ganha foco; mudou → recarrega a página Progresso.

## 10. Testes

- **Protocolo**: sequências JSON-RPC gravadas (initialize → tools/list →
  tools/call) contra um banco de fixture; a resposta comparada com JSON
  esperado. Inclui o teste de que **nada** além de JSON vai para o stdout.
- **Consentimento**: cada ferramenta com a categoria desligada devolve o erro
  certo e não toca no banco.
- **Validação**: cada limite de §5.
- **Instalar**: config existente com outros servidores sai intacta; `.bak`
  criado; `remover` desfaz.
- **Manual**: MCP Inspector e uma conversa real em Claude Code e Codex antes
  de cada release.

## 11. Fases

| Fase | Entrega | Pronto quando |
|---|---|---|
| **0. Preparação** | Aba Estudo fora do `main` (branch `estudo`); `materiaDaProva` e pasta de materiais no core; `busy_timeout`; consentimento em `meta` | testes atuais passam; UI igual |
| **1. Leitura** | `sigaa-cli mcp` com as ferramentas, recursos e prompts de §4, sem rede e sem escrita | Claude Code e Codex respondem "o que cai na P2 de X" usando só o servidor |
| **2. Conector** | `mcp instalar/remover`, Opções → Agentes de IA, kit de estudo | aluno conecta sem editar arquivo |
| **3. Devolução** | ferramentas e tabelas de §5, auditoria, página Progresso | o fluxo de §8 aparece na aba Estudo |
| **4. Rede (opt-in)** | `baixar_arquivo`, `atualizar_turma`, com orçamento | baixa o PDF que falta sem abrir a UI |
| 5. Web (futuro) | servidor HTTP remoto com OAuth, para chats na web | fora deste plano |

Cada fase é um release possível: a 1 sozinha já resolve "parar de arrastar
arquivo" para quem usa agente de terminal.

## 12. Decisões tomadas e em aberto

| # | Questão | Decisão (01/10/2026) |
|---|---|---|
| 1 | Nomes das ferramentas | **Português** (`materia_da_prova`, `registrar_estudo`…), com descrições claras. |
| 2 | Extrair texto de PDF no app (poppler) | *Em aberto* — caminho + blob primeiro; decidir se a Fase 2 mostrar que não basta. |
| 3 | Devolução direta ou com aprovação | **Direta**, com a origem visível e apagar fácil. |
| 4 | Rede pelo agente | **Sim, Fase 4**, desligada por padrão e com orçamento por sessão. |
| 5 | Pasta de dados canônica (D3) | Caminhos no registro do servidor; pasta canônica depois. |

A aba Estudo (planejamento) saiu do `main` para o branch `estudo` e não vai no
próximo deploy: será redesenhada junto da página Progresso (Fase 3).

## 13. Estado da implementação (01/10/2026)

Fases 0 a 4 implementadas no branch `mcp`, com testes em `tests/mcp_test.cpp`,
`tests/instalar_test.cpp`, `tests/materia_test.cpp` e `tests/pastas_test.cpp`.

| Fase | Onde |
|---|---|
| 0 | `core/avaliacao/Materia` (a matéria da prova fora da UI), `core/util/Texto` e `Pastas`, `busy_timeout`, `Abertura::SoExistente`, `lerMeta/gravarMeta` |
| 1 | `src/mcp/Servidor`, `Leitura`, `Comando`; `sigaa-cli mcp` |
| 2 | `src/mcp/Instalar` e `Kit`; `sigaa-ui mcp` para o AppImage; Opções > Agentes de IA (`ui/DialogoAgentes`); Kit para IA na janela da turma |
| 3 | `core/estudo/Registros`, tabelas `registro_estudo`, `desempenho`, `ponto_foco`; `src/mcp/Escrita` |
| 4 | `src/mcp/Rede`: `baixar_arquivo` e `atualizar_turma` |

Diferenças em relação ao plano:

- **`ponto_foco` sem `UNIQUE`.** O mesmo tópico pode ser resolvido e voltar a
  ser foco; as linhas resolvidas são histórico. Quem evita duplicar o ponto
  aberto é `marcarFoco`, pela coluna `topico_chave` (o tópico sem acento e
  sem caixa).
- **Turma pela sigla** ("edo", "ia", "paa"), além de id, código e nome. Foi o
  primeiro tropeço no teste com dados de exemplo.
- **"Quiz" deixou de contar como matéria**, como já não contavam "prova",
  "avaliação" e "revisão".
- **Orçamento de rede:** 24 requisições por sessão do agente, 20 s entre duas
  operações, `baixar_arquivo` custa 6 e `atualizar_turma` 8. Arquivo já
  baixado não gasta nada.
- **A aba Estudo** (branch `estudo`, com o `mcp` juntado) ganhou duas páginas
  no menu lateral: **Progresso** (pontos de foco com *Já domino* e *Apagar*,
  desempenho por tópico, histórico) e **Agentes de IA** (permissões, conectar,
  atividade). Opções só leva até lá. O planejamento usa os dados do agente:
  `registro_estudo` desconta da prova citada (ou da próxima da turma), e cada
  ponto de foco aberto soma 30 min × nível à prova mais próxima e vira a dica
  `Foco` quando ela está a até 21 dias.
- **A janela recarrega pelo `meta`** (`mcp.alteracao`, gravado a cada
  ferramenta de escrita bem-sucedida), e não por `PRAGMA data_version`, que
  só faz sentido numa conexão que fica aberta, e a UI abre uma por operação.
- **O registro passa `--url`**, a instituição: a chave da senha no cofre sai
  do host, e o servidor iniciado pelo agente não sabe qual SIGAA o app usa.

Testado à mão (§10) com o Claude Code de verdade, por `--mcp-config` com o
banco de exemplo: listou a prova, explicou a data corrigida pelo aluno, deu a
matéria e registrou o estudo, tudo com a origem `claude-code` na auditoria.

Ainda em aberto:

- o caminho do `mcp_config.json` do **Antigravity** (usei
  `~/.gemini/antigravity/mcp_config.json`, a confirmar);
- **Codex e Gemini CLI** não foram testados com o agente de verdade, só o
  formato do arquivo;
- extração de texto de PDF no app (§12, item 2);
- a Fase 5 (servidor remoto para chats na web).

