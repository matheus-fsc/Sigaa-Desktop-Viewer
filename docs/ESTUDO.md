# Aba Estudo

O retrato da aba **Estudo** como ela está hoje, para quem vai redesenhá-la.
Descreve o que existe e o motivo de cada escolha. Não propõe nada.

| Código | O que tem |
|---|---|
| `src/ui/Planejamento.*` | a aba (menu e páginas), Planejamento, Horas e dificuldade |
| `src/ui/Progresso.*` | Progresso |
| `src/ui/Agentes.*` | Agentes de IA |
| `src/ui/Controles.*` | interruptor, seletor segmentado, passo − / +, link, pílula |
| `src/core/planejamento/` | a conta do plano |
| `src/mcp/Propostas.*` | as regras das propostas do agente (modos, teto, aceitar, desfazer) |

A UI só lê o banco, pede o plano e mostra. Referência visual: telas E1–E15 do
projeto de design "SIGAA Viewer · versão web" (`Telas.dc.html`, `Estudo.dc.html`).

## 1. O esqueleto

Uma aba no topo da janela principal, ao lado de Agenda, Turmas etc. O título
da aba muda para **"Estudo (1h30 hoje)"** quando há sessão planejada para hoje.

```
┌─ Estudo ───────────────────────────────────────────────────────────────────┐
│┌──────────────────┐│                                                       │
││ ▌Planejamento    ││   <página escolhida>                                  │
││  Progresso    (2)││                                                       │
││  Horas e dificul.││   margens: 32px esq · 24px topo · 24px dir · 16px base │
││  Agentes de IA   ││                                                       │
│└──────────────────┘│                                                       │
│  menu 220px fixo   filete 1px            QStackedWidget (estica)           │
└────────────────────────────────────────────────────────────────────────────┘
```

| Peça | Como é |
|---|---|
| Menu lateral (`#menuEstudo`) | `QListWidget` de 220px, fundo `surface`, padding 16px/10px. Itens com padding 9px/12px e raio 8px. Texto `text-2`; hover `surface-2`; selecionado: fundo `accent-soft`, texto `accent`. |
| Contador | No item Progresso, uma pílula `warn-soft`/`warn` com o número de propostas que esperam resposta. É pintada por um delegate (`ItemDoMenu`) e lida pelo leitor de tela ("Progresso, 2 propostas esperando você"). |
| Filete | `QFrame#fileteVertical`, 1px, cor `line`, com 16px de margem em cima e embaixo. |
| Páginas | Na ordem do menu: Planejamento, Progresso, Horas e dificuldade, Agentes de IA. Abre em Planejamento. |

Todas as páginas começam do mesmo jeito: um **título** (papel Subtítulo,
negrito), uma **nota** cinza de uma ou duas linhas e o conteúdo logo abaixo.
Os cabeçalhos de seção vêm em CAIXA ALTA, papel Legenda, cor `text-3`. As
listas ficam dentro de um **cartão** (`surface`, borda `line`, raio 10px), uma
linha por item separada por filete (`QFrame[classe="linha"]`).

**Navegação entre páginas:**
- "Ver planejamento" (painel de provas) → Planejamento.
- Opções → Agentes, e "Conectar um agente" (estado vazio do Progresso) → Agentes › Conectar.
- "Alterar o que ele pode mudar" (Progresso) → Agentes › Permissões.
- "Ver proposta" (Horas e dificuldade) → Progresso.

**As páginas se mantêm em dia.** Quando uma muda algo que outra mostra
(proposta aceita, modo trocado, Desfazer), a aba relê as outras páginas,
replaneja se o plano já existe, avisa a Agenda e atualiza o contador
(`PainelEstudo::sincronizar`).

Diferença do design: este é o app de **desktop** (Qt). A faixa de pílulas que
substitui o menu no celular (E13–E15) não se aplica aqui.

## 2. Planejamento

Segue o protótipo web (`Area de Estudo.dc.html`, telas E1–E6). A página rola
inteira, numa grade de duas colunas: a esquerda estica e a direita tem 380px.

```
Planejamento                          ◷ Próxima prova em 5 dias  ! 1 entrega atrasada  Esta semana: 3h50 feitas de 5h
Semana de 05/10 a 11/10 · hoje, ter 06/10
┌ Onde o semestre aperta ─────────────── ! 05/10: plano 1h acima do livre ┐ ┌ AGENTE DE IA ─────────────┐
│ [■ Compiladores] [■ IA] [■ EDO] …   ── livre ▨ acima ■ feito ◆ prova ◇ │ │ Claude Code    ● conectado │
│  20h ┤      ┌─!─┐                                                        │ │ Última atividade · hoje…   │
│  10h ┤      │   │        ┌───┐                                          │ │ claude-code registrou …    │
│      ┤ ▇   ─▨▨▨─  ▆      │▆▆▆│   (barras empilhadas por matéria)        │ │ ✓ Ler dados do app         │
│   0h ┼─────────────────────────                                         │ │ ✕ Buscar no SIGAA desligado│
│        ◆      ◆ ◇   (provas no dia)  ▪▪▪▪▪▪▪ (calor por dia)            │ │ Alterar permissões         │
│      28/09  05/10  12/10 …                                              │ └────────────────────────────┘
│             esta semana                                                 │
└──────────────────────────────────────────────────────────────────────────┘
┌ O que fazer   Hoje, esta semana e até a próxima prova ┐ ┌ Preparo para as provas · próximas 4 ┐
│ ▾ Atrasado                              2 entregas     │ │ ◆ Prova 1 · Compiladores   ◷ em 7 dias │
│   ↥ Lista 3 — Entrega · IA · prazo …  [! Atrasado · 3 dias] │ │ ▬▬▬▬░░░ 3h10 de 12h · 26%       │
│ ▾ Hoje · ter 06/10          3 itens · 2h30 de estudo │ │ ✓ inclui 1h50 via agente de IA   │
│   ☐ ■ Revisar FIRST e FOLLOW — Ponto de foco … ●●○  │ │ [Análise léxica] [◎ Análise LL(1)] │
│   ☐ ■ Compiladores — Sessão de estudo · para a P1  1h │ │ ▸ Estudar com IA                │
│ ▾ Esta semana · qua a dom             5h de estudo │ ├ Pontos de foco ─────────────────┤
│   ? Avaliação 2 · EDO — Confirmar data [Confirmar][Corrigir] │ ■ FIRST e FOLLOW   ●●○ dificuldade │
│   sex 09/10  [■ AC · 2ª Avaliação 1h] [■ IA · …]  1h │ │ … [✓ Resolvido]                  │
│ ▾ Até a próxima prova                 qua 14/10   │ ├ Desempenho por tópico ──────────┤
│   ◆ Avaliação 2 · FE — Prova · qua 14/10   ◷ em 8 dias │ ■ Análise léxica  ✓ 8/10 · 80%   │
└────────────────────────────────────────────────────────┘ │ ▬▬▬▬▬▬▬▬|▬|░ (marcas em 50 e 70%) │
                                                           └──────────────────────────────────┘
```

**Cores por matéria.** São os tokens `m1`…`m8`. Os quatro primeiros vêm do
protótipo; os outros quatro seguem o mesmo tom, para quem tem mais de quatro
turmas. Nenhum repete laranja, vermelho ou verde, que já significam crítico,
atrasado e feito. A cor vem da ordem da turma (`tema::cor::materia(i)`). O
nome em caixa alta do SIGAA é mostrado como título ("Equações Diferenciais
Ordinárias"); nos chips e na legenda vai uma sigla ("EDO", "ADS IV").

**Gráfico** (`ui/GraficoCarga.*`, desenhado com QPainter):
- Uma coluna por semana: a semana passada (se teve algo) e as do plano em
  diante. Cada coluna tem no mínimo 44px, com escala única de 10h em 10h.
- A barra empilha o planejado por matéria. A linha tracejada é o tempo livre.
  O que passa dele fica hachurado, com filete laranja e "+1h" em cima.
- O feito é verde: embaixo da pilha na semana atual; na semana passada é a
  barra inteira, com o planejado em contorno tracejado.
- A semana crítica (80%+ do livre, ou 3+ provas) tem fundo `warn-soft` e "!
  crítica". A semana atual tem moldura `line-strong`; a escolhida, moldura
  `accent`.
- Embaixo de cada coluna: os losangos das provas no dia exato (vazados quando
  a data foi deduzida), a faixa de calor com sete fatias (o estudo de cada
  dia; um ponto marca hoje) e o rótulo "dd/MM" ("esta semana" na atual).
- Passar o mouse mostra um resumo (livre, planejado, excesso, feito, por
  matéria, provas). Clicar escolhe a semana, e o "O que fazer" passa a mostrar
  só ela. Clicar numa matéria da legenda isola a matéria no gráfico.
- No canto, os alertas: até 3 semanas críticas e um resumo das provas que não
  cabem no tempo livre. Clicar num alerta escolhe a semana.

**Agente de IA.** Conectado: nome, "● conectado", a última atividade em uma
frase, a lista de permissões ✓/✕ e "Alterar permissões" (leva a Agentes ›
Permissões). Sem agente: o convite e o botão "Conectar um agente".

**O que fazer.** Grupos que abrem e fecham (o estado vale enquanto a página
estiver aberta):

| Grupo | O que tem |
|---|---|
| Atrasado (fundo `danger-soft`) | entregas pendentes vencidas há até 14 dias, com "! Atrasado · N dias" |
| Hoje | até 2 pontos de foco de nível 2 ou 3 com prova da turma em até 21 dias (marcar = resolver), entregas que vencem hoje e as sessões do dia (caixa de marcar) |
| Esta semana | entregas até domingo, as 3 datas deduzidas mais próximas com **Confirmar** e **Corrigir** (as mesmas ações da aba Provas) e os dias com as sessões em chips |
| Até a próxima prova | os dias depois desta semana até a véspera (no máximo 2 semanas) e a prova |

Com uma semana escolhida no gráfico, a lista vira a visão da semana: faixa
`accent-soft` com o período, "! crítica" e "Voltar para esta semana"; o
resumo livre/planejado/feito; as provas; e uma linha por matéria com as horas
e a prova-alvo.

**Preparo para as provas** (as próximas 4): losango, prova e sigla, data,
"◷ em N dias" (laranja até 7 dias), "✓ você corrigiu · SIGAA diz …" quando o
aluno mudou a data, e a barra feito ÷ necessário. Feito = sessões feitas +
estudo registrado por agentes; necessário = feito + o que falta no plano +
déficit. Vêm também "✓ inclui Xh via agente de IA", os tópicos da matéria (o
que tem ponto de foco aparece com "◎" em laranja) e "▸ Estudar com IA", que
abre três pedidos prontos para copiar.

**Pontos de foco** e **Desempenho por tópico**: o mesmo dado do Progresso, em
formato de lista. Os focos têm "✓ Resolvido" com Desfazer. O desempenho mostra
uma barra com marcas em 50% e 70%.

Marcar uma sessão grava na hora e redesenha a página, mas não replaneja: o
plano se ajusta da próxima vez que a página aparece. O plano nasce quando o
aluno abre a página pela primeira vez (opt-in).

## 3. Progresso

É o que os agentes de IA devolveram e o que eles querem mudar no plano. A
página rola inteira.

### Estado vazio (nada registrado e nenhuma proposta)
Um `recado` explicando a página e o botão primário **Conectar um agente**.

### Com dados
```
Progresso
5h10 de estudo com agentes nos últimos 7 dias (IA 2h, …) · 3 ponto(s) de foco aberto(s)

PROPOSTAS DO AGENTE  ● 2 esperando você  Você aprova: horas, dificuldade · aplica e avisa: …  Alterar o que ele pode mudar
┌──────────────────────────────────────────────────────────────────────────┐
│ DIFICULDADE                                         [Aceitar] [Ajustar] Recusar │ ← cartaoForte
│ Dificuldade de Compiladores: Média → Difícil                             │
│ 40% em Análise LL(1)… (motivo do agente)                                 │
│ Efeito: +2h30 de estudo até a Prova 1 (qui 08/10)                        │
│ proposta por claude-code em 06/10 14:43                                  │
│ ┌ Ajustar para [Fácil|Média|Difícil] · depois clique em Aceitar ┐        │ ← faixa, ao Ajustar
└──────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────┐
│ SESSÃO EXTRA          ✓ Aplicada pelo agente, como você permitiu  Desfazer │ ← cartaoApagado
└──────────────────────────────────────────────────────────────────────────┘
N proposta(s) escondida(s), porque você não permite esse tipo de mudança.

MÉTRICAS
┌ SEGUIU O PLANO · 7 DIAS ┐┌ ACERTO NAS QUESTÕES ┐┌ PRÓXIMA PROVA          ┐
│ 72%                     ││ 59%                 ││ em 5 dias              │
│ 5h10 de 7h10 planejadas ││ 19 de 32, 5 tópicos ││ Aval. 1 de IA · ter 06 │
│ ▬▬▬▬▬▬▬▬░░░             ││ ↑ 8 pts desde 30/09 ││ Falta estudar 2h de IA │
└─────────────────────────┘└─────────────────────┘└────────────────────────┘
Matéria │ Dificuldade (↑ agente) │ Estudado · 7 dias │ Falta até a prova │ Acerto │ Tendência

PONTOS DE FOCO
│ ●●● crítico │ Laplace — EDO / motivo / marcado por codex em … │ [✓ Já domino] Apagar │
│ ✓ Marcado como dominado: … O plano vai se ajustar.                 Desfazer │

DESEMPENHO POR TÓPICO (2/5)      HISTÓRICO (3/5)
Tópico │ Turma │ Acertos │ ✕ 40%  Quando │ O quê │ Turma │ Detalhe │ Agente
                                 [Apagar o selecionado] [Apagar tudo de um agente…]
                                 ┌ Apaga estudo, desempenho e foco registrados por: ┐ ← perigo
                                 │ [claude-code · 6 registro(s)] [codex · 3 …]       │
```

**Propostas.** Há quatro tipos: horas por dia, dificuldade, sessão extra e
ponto de foco. Quais aparecem e como:

| Estado | Cartão | Ações |
|---|---|---|
| pendente | `cartaoForte` (borda `line-strong`) | **Aceitar** (primário; desabilitado se passa do teto), **Ajustar** (abre a faixa com o seletor ou o passo), **Recusar** (discreto) |
| aceita / aplicada | `cartaoApagado` (`surface-2`) | "✓ Aceita por você" ou "✓ Aplicada pelo agente, como você permitiu" em `ok`, e **Desfazer** |
| recusada / desfeita | `cartaoApagado` | "Recusada por você" ou "Desfeita por você", e **Reabrir** |

- Primeiro vêm as pendentes; depois as respondidas nos últimos 7 dias (até 6).
  As mais antigas ficam só no Histórico.
- A linha de **efeito** é calculada no app, não vem do agente. Dificuldade:
  quanto muda o estudo até a próxima prova. Horas: quanto muda o tempo de
  estudo do dia, já sem as aulas. Sessão: antes de qual prova. Foco: +30 min ×
  nível.
- Uma proposta de horas acima do teto mostra "! Passa do seu teto de 8 h por
  dia…" em `warn`.
- O título, o motivo e a origem são texto do agente e aparecem como texto puro.

**Métricas.**
- *Seguiu o plano*: sessões feitas ÷ sessões que já deviam ter acontecido
  nos últimos 7 dias. As pendentes de hoje não contam contra.
- *Acerto*: soma de todo o desempenho. A tendência compara com o acerto de
  antes desta semana.
- *Próxima prova*: a do dia mais próximo, e quanto ainda falta dela no plano.
- *Por matéria*: dificuldade (com "↑ agente" em `accent` se foi ele quem
  mudou), estudado contra planejado, quanto falta até a prova, acerto
  (✕/!/✓ e cor) e tendência (↑/↓ em pontos).

**Focos.** "✓ Já domino" e "Apagar" podem ser desfeitos enquanto o aluno
estiver na página: o cartão vira uma linha cinza com **Desfazer**.

**Histórico.** Junta estudo, desempenho, foco e mudança (proposta
respondida), do mais novo para o mais velho. A coluna Turma tem largura fixa
de 130px e corta o texto com reticências (o nome inteiro aparece na dica); a
coluna Detalhe fica com a sobra. "Apagar tudo de um agente…" abre uma faixa
`perigo` com um botão por agente e pede confirmação.

## 4. Horas e dificuldade

São os ajustes que alimentam o plano. **Não tem botão Salvar**: cada mudança
grava sozinha, 400ms depois da última. No canto, o estado diz "Grava sozinho
ao mudar", depois "Gravando…" e por fim "✓ Gravado" (em `ok`).

```
Horas e dificuldade                                        ✓ Gravado
Quanto tempo você tem para a faculdade em cada dia, aulas e estudo juntos…

┌ Tempo disponível por dia ─────────────────────────────────────────┐ máx 760px
│ Segunda  [ − 6,0 h + ]  − 2h45 de aula (3 horários) = 3h15 para estudar │
│ Sábado   [ − 6,0 h + ]  6h para estudar          (↑ pelo agente) Desfazer │
│ …                        ! as aulas já passam desse tempo  (em warn)    │
│ Na semana: 40h disponíveis − 18h20 de aula = 21h40 para estudar  ← rodapé│
└───────────────────────────────────────────────────────────────────┘
┌ Dificuldade de cada matéria ──────────────────────────────────────┐
│ Matéria difícil recebe 50% mais tempo de estudo; fácil, 30% menos. │
│ COMPILADORES                 [Fácil|Média|Difícil]  tempo padrão   │
│ 1 ponto de foco aberto · acerto 65%  ● Agente sugere Difícil  Ver proposta │
│ (↑ mudado pelo agente) Desfazer                                    │
└───────────────────────────────────────────────────────────────────┘
```

- **Passo − / +**: meia hora por clique, de 0 a 16 h. O valor fica grande no
  meio.
- A conta "− aula = para estudar" fica à vista para o desconto não parecer
  mágica. Cada horário da grade conta 55 min.
- **Seletor segmentado** de dificuldade, com o efeito ao lado (+50% / −30% /
  tempo padrão). Média é o padrão e só o que difere dela é guardado.
- **"↑ pelo agente"** (pílula `accent-soft`) aparece quando o valor de agora é
  o que o agente pôs. Se o aluno mexer no valor, a marca some. **Desfazer**
  volta ao valor de antes da mudança.
- "● Agente sugere X · Ver proposta" aparece quando há proposta de
  dificuldade pendente para a matéria.

## 5. Agentes de IA

Liga o app aos agentes pelo protocolo MCP (`docs/MCP.md`). Tem três abas:
**Permissões**, **Conectar** e **Atividade**.

### Permissões
Num cartão, quatro linhas com título, explicação e um **interruptor** (44×26,
`accent-fill` quando ligado):

| Interruptor | O que libera |
|---|---|
| Ler meus dados acadêmicos | Turmas, tópicos, provas, prazos, notícias e frequência. Desligado, os outros três ficam bloqueados e mostram "Precisa de “Ler meus dados acadêmicos”." |
| Ler os materiais baixados | O caminho e o conteúdo dos PDFs já baixados. |
| Registrar estudo no app | O agente grava tempo, desempenho e focos. Sem isto, o bloco abaixo fica bloqueado. |
| Buscar no SIGAA | O agente baixa um material ou atualiza uma turma, com limite de consultas. |

Abaixo, o estado ("✓ Gravado: ler os materiais baixados ligado.") e o cartão
**O que o agente pode mudar no seu plano**:

| Linha | Escolha |
|---|---|
| Tempo disponível por dia | Não pode · **Propõe** · Aplica e avisa |
| Dificuldade das matérias | Não pode · **Propõe** · Aplica e avisa |
| Sessões extras | Não pode · Propõe · **Aplica e avisa** |
| Pontos de foco | Não pode · Propõe · **Aplica e avisa** |
| Teto de horas por dia | 6 h · **8 h** · 10 h · 12 h |

(em negrito, os padrões). Ao trocar para "Aplica e avisa", as pendentes daquele
tipo são aplicadas na hora. Ao trocar para "Não pode", elas são escondidas, e o
estado diz quantas. Fecha com a nota "Toda mudança feita pelo agente fica no
Histórico…" e o `recado` de privacidade.

### Conectar
```
! “Ler meus dados acadêmicos” está desligado: …        [Ver permissões]   ← alertaFaixa, se desligado
Conectar escreve a entrada "sigaa" na configuração do agente (com uma cópia .bak)…
┌────────────────────────────────────────────────────────────────────┐
│ Claude Code    ● conectado              [Reconectar] Desconectar   │
│                último acesso hoje, 04:03                           │
│ Codex CLI      ○ instalado, não conectado           [Conectar]     │
│ Cursor         ○ não encontrado neste computador    [Conectar]     │
└────────────────────────────────────────────────────────────────────┘
[recado] Kit para IA, para quem usa chat na web
```
**Diferença do design:** no desktop, o app escreve a configuração do agente
direto no disco, então os estados "instalado" e "não encontrado" continuam
existindo. O fluxo de "copiar comando ou endereço" do design é da versão web,
e o endereço `sigaa-viewer.app/mcp` que aparece lá é inventado. O "último
acesso" sai da auditoria, casando o nome que o agente informa (`claude-ai`,
`codex-mcp-client`…) com o cliente.

### Atividade
Os 100 últimos acessos: **Quando** (hoje, 04:03) · **Agente** · **Pedido** ·
**Turma** · **Resultado**. O resultado é "✓ ok" ou "✕ recusado · motivo", em
`danger`; o motivo é a mensagem que o agente recebeu, e o texto inteiro fica
na dica.

## 6. Vocabulário visual

A fonte e as cores saem de `src/ui/Tema.*` e `src/ui/recursos/estilo.qss`. O
QSS não tem nenhuma cor literal: cita os tokens como `@nome`.

**Fonte**: Source Sans 3, ou a do sistema se ela faltar. Os tamanhos são
relativos à fonte do sistema: Título 1.55×, Subtítulo 1.12×, Corpo 1×,
Legenda 0.88×.

**Espaçamento**: grade de 4px, `esp(n) = 4n`.

| Token | Escuro | Claro | Uso na aba |
|---|---|---|---|
| `surface` | #222733 | #ffffff | menu lateral, cartões |
| `surface-2` | #2a303d | #f0f2f6 | recado, cartão respondido, faixa de ajuste, rodapé |
| `line` / `line-strong` | #333a48 / #465064 | #e0e4ea / #c8ced8 | filetes / cartão pendente, molduras dos controles |
| `text-2` / `text-3` | #b3bbc9 / #8e97a8 | #454d5c / #5d6677 | itens do menu / notas e seções |
| `accent` / `accent-soft` | #8aadf5 / azul 17% | #2a5cc4 / azul 10% | selecionado, links, "↑ pelo agente" |
| `accent-fill` | #3d6fd6 | #2f63cc | botão primário, interruptor ligado |
| `warn` / `warn-soft` | #eca85f / 16% | #955006 / 17% | semana crítica, contador, "esperando você", teto |
| `danger` / `danger-soft` | #f28f8f / 16% | #b0302d / 11% | foco crítico, acerto < 50%, recusado, apagar tudo |
| `ok` | #72d09a | #1b7141 | feito, aceita, gravado, acerto ≥ 70%, conectado |

**Classes do QSS** (propriedade `classe` ou `papel` no widget):

| Para | Classes |
|---|---|
| Texto | `nota`, `secao`, `recado`, `alerta`, `pilula` (+ `tom` = `acento`/`aviso`/`ok`) |
| Moldura | `cartao`, `cartaoForte`, `cartaoApagado`, `faixa`, `linha`, `rodape`, `perigo`, `alertaFaixa` |
| Botão | `primario`, `secundario`, `discreto`, `link`, `chipPerigo` |
| Controle (por nome de objeto) | `#segmentado`, `#passo` |

O tema claro ou escuro segue o sistema (`SIGAA_TEMA=claro|escuro` vence) e
troca com o app aberto.

## 7. Restrições para o redesenho

- **Qt Widgets, não web.** O QSS não faz grid, sombra, gradiente nem
  animação, e o tamanho de fonte fica no código. Um raio de borda maior que
  metade da altura é ignorado.
- **Nada do agente vira HTML.** Tópico, motivo e origem ficam em label de
  texto puro ou em célula de tabela.
- **Sem botão Salvar** em nenhuma página, e **tudo que é do agente pode ser
  desfeito**.
- **Densidade**: a janela costuma ter entre 1100 e 1600px. Com o menu de
  220px, sobram cerca de 850px úteis no pior caso. Os nomes de turma do SIGAA
  são longos e em caixa alta ("ANÁLISE E DESENVOLVIMENTO DE SOFTWARE IV").
- **Acessibilidade**: a fonte acompanha o sistema, e a cor nunca é o único
  sinal (✕/!/✓ no acerto, ●●○ e a palavra no nível, o rótulo "CRÍTICA").
