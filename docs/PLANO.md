# SIGAA Viewer — Plano Técnico

Cliente desktop multiplataforma (Windows / Linux / macOS) em C++ para o SIGAA da UNIFEI,
focado em **não perder prazos**: agrega "Minhas Atividades", "Atualizações das Turmas" e
os tópicos de aula das turmas virtuais, com notificações nativas e calendário de provas.

---

## 1. Decisão de stack

### Recomendação: **Qt 6 (Widgets) + lexbor + SQLite + libcurl (via QtNetwork)**

| Camada | Escolha | Por quê |
|---|---|---|
| UI | **Qt 6.7+ Widgets** | Único toolkit C++ que entrega, na mesma caixa: HTTP com cookie jar, tray icon + notificações nativas, SQLite, tabelas/calendário prontos, i18n e deploy nos 3 SOs. |
| HTTP/sessão | **QtNetwork** (`QNetworkAccessManager` + `QNetworkCookieJar`) | Redirects, cookies (`JSESSIONID`), POST `x-www-form-urlencoded` e proxy do sistema já resolvidos. |
| Login (fallback) | **QtWebEngineWidgets** (opcional, compile flag) | Se o login quebrar (captcha, JS, SSO), abre um browser embutido, o usuário loga, e você **colhe o cookie** e volta ao scraping puro por HTTP. É o seguro contra o SIGAA mudar. |
| Parser HTML | **lexbor** | HTML5 tolerante a lixo (o JSF gera HTML malformado) + seletores CSS. Alternativa: libxml2 + XPath. |
| Persistência | **SQLite** (via `QtSql` ou `sqlite3` direto) | Cache offline, diff/dedupe, estado de "lido". |
| Credenciais | **QtKeychain** | Credential Manager (Win) / Keychain (mac) / libsecret (Linux). Nunca em texto plano. |
| Build | **CMake + vcpkg** (+ CMakePresets) | Um `cmake --preset` por SO; CI matrix no GitHub Actions. |
| Testes | **Catch2** ou GoogleTest | Parser testado contra fixtures HTML salvos. |
| Log | **spdlog** | |

**Por que Qt e não as alternativas:**

- **Dear ImGui** — ótimo para tools/debug, péssimo para um leitor de textos longos: sem seleção de
  texto decente, sem acessibilidade, sem rich text, sem notificações. Descarte.
- **wxWidgets** — viável e mais leve (licença permissiva, `wxWebView` usa o webview do SO). Escolha
  esta **se** licença LGPL do Qt for problema ou se você quiser binário pequeno. Custo: você
  implementa HTTP (libcurl), cookie jar e agendamento na mão.
- **Slint** — bonito e moderno, mas ecossistema jovem e sem webview: você perde o plano B do login.
- **FLTK / GTK4** — FLTK é primitivo demais; GTK no Windows é sofrimento de packaging.

**Licença:** Qt 6 sob **LGPLv3** é ok para projeto open-source ou app distribuído com link dinâmico.
Se o objetivo for fechar o código e distribuir estático, isso muda a conta — decidir antes da Fase 1.

**Tamanho do binário:** Qt Widgets + windeployqt ≈ 40–60 MB. Com QtWebEngine sobe para ~180 MB.
Por isso o WebEngine deve ser **opcional via `-DSIGAA_ENABLE_WEBENGINE=ON`**, não obrigatório.

---

## 2. O problema real: o SIGAA é JSF 1.2 + RichFaces 3.3.3

Isso é a parte difícil do projeto — mais do que a UI. Evidências no HTML colado:
`/sigaa/a4j/g/3_3_3.Final...`, `javax.faces.ViewState`, `jsfcljs(...)`, `A4J.AJAX.Submit`.

### 2.1 Navegação não é REST

Um "link" como:

```html
<a onclick="jsfcljs(document.getElementById('formAtividades'),
   {'formAtividades:visualizarTarefaTurmaVirtual':'formAtividades:visualizarTarefaTurmaVirtual',
    'id':'130174755','idTurma':'89151'},'');return false">Pergunta 1 - Dev Mobile</a>
```

é, na prática:

```
POST /sigaa/portais/discente/discente.jsf
Content-Type: application/x-www-form-urlencoded

formAtividades=formAtividades
&javax.faces.ViewState=j_id1
&formAtividades:visualizarTarefaTurmaVirtual=formAtividades:visualizarTarefaTurmaVirtual
&id=130174755
&idTurma=89151
+ todos os outros inputs do <form>
```

Ou seja: **`jsfcljs(form, params, target)` = serializar o form inteiro + injetar `params` como
hidden inputs + submit.** Isso vira uma função de ~40 linhas no cliente:

```cpp
// core/jsf/JsfForm.h
struct JsfForm {
    QString action;                          // "/sigaa/portais/discente/discente.jsf"
    QMap<QString, QString> fields;           // todos os <input>/<select> do form
    QString viewState;                       // javax.faces.ViewState

    QByteArray encode(const QMap<QString,QString>& extra) const;
};
```

### 2.2 ViewState é server-side — a ordem importa

`value="j_id1"` / `"j_id4"` são *ponteiros* para estado guardado na `HttpSession`, não o estado
serializado. Consequências:

- Você **não pode** dar POST direto numa página arbitrária. Tem que **replicar a navegação**:
  portal → clicar na turma → turma virtual → voltar ao portal.
- O servidor guarda um número limitado de views por sessão (LRU). Navegar demais invalida views
  antigas → `ViewExpiredException`.
- **Toda requisição deve ser serializada.** O SIGAA detecta navegação concorrente ("Sistema detectou
  navegação em múltiplas abas") e invalida a view. Nada de paralelizar o crawl.

Portanto o núcleo do scraper é uma **máquina de estados single-threaded com fila**:

```
SigaaSession
  ├── QQueue<Request>            // uma requisição por vez, serializada
  ├── NavigationState            // onde estamos no SIGAA agora
  ├── CookieJar (JSESSIONID)
  └── retry/backoff + detecção de "sessão expirou" → re-login → replay
```

### 2.3 Sessão de 30 min

O timeout é o da `HttpSession` do Tomcat, renovado a cada request que toca a sessão.

Duas abordagens, e a recomendação é **fazer as duas**:

1. **Keep-alive leve** — GET no portal a cada ~15 min. Barato, mas *pode* mexer no ViewState
   corrente, então faça só quando a fila estiver ociosa.
2. **Re-login automático** (o que realmente resolve) — ao detectar página de sessão expirada,
   refazer o login com credencial do keychain e **replayar** a navegação até o ponto onde parou.
   Isso é mais robusto que lutar contra o timeout, e é obrigatório de qualquer forma porque o
   SIGAA derruba sessão por outros motivos (deploy, novo login em outro lugar, manutenção).

> Sobre "por que desloga": é quase certo que seja segurança, não carga — aluno logando em PC de
> laboratório/biblioteca. Timeout curto é a mitigação padrão. Não dá para desligar do lado cliente;
> só dá para reconectar bem.

### 2.4 Etiqueta (importante)

São **os seus próprios dados**, o que torna isso legítimo. Ainda assim, o cliente deve ser educado,
para não parecer ataque e para não te derrubarem:

- Poll padrão de **15–30 min**, nunca menos que 5.
- **Uma requisição por vez**, com jitter e backoff exponencial em erro.
- `User-Agent` identificável (`sigaa-viewer/0.1 (+github.com/...)`).
- Respeitar 429/503 e página de manutenção.
- Credenciais só no keychain do SO, nunca em disco/JSON/log.
- Sanitizar logs (nunca logar cookie, senha ou HTML completo em produção).

### 2.5 Antes de tudo: procure uma API

**Fase 0 obrigatória.** Muitos SIGAAs expõem um backend REST para o app oficial (`/sigaa/mobile/`,
`/sigaa/api/`, endpoints usados pelo "SIGAA Mobile" da UFRN). Se a UNIFEI tiver isso habilitado,
todo o problema de ViewState desaparece. Vale 1 dia investigando com o DevTools/HAR antes de
escrever qualquer parser.

---

## 3. O que extrair (contratos de dados)

Do HTML que você já mapeou:

### 3.1 Atividades — `#formAtividades table tbody tr`

```cpp
struct Atividade {
    QString  turmaNome;      // "ANÁLISE E DESENVOLVIMENTO DE SOFTWARE IV"
    QString  periodo;        // "2026.2"  (linha de cabeçalho com background #C8D5EC)
    QString  titulo;         // "Pergunta 1 - Dev Mobile"
    QString  tipo;           // "Tarefa" | "Prova" | ...  (do <strong>)
    QDateTime prazo;         // "11/08/2026 23:59"
    Status   status;         // do <img>: prova_semana.png | check.png | prova.png
    QString  atividadeId;    // "130174755"  <- chave estável (do jsfcljs)
    QString  idTurma;        // "89151"      <- chave estável
};
```

**Chave primária: `(atividadeId, idTurma)`** — vem dos parâmetros do `jsfcljs`, é estável entre
recargas. Isso é ouro para o diff.

Mapa de ícones (`<img title=...>`): `prova_semana.png` → "na semana", `check.png` → "concluída",
e os demais a descobrir observando ao longo do semestre.

### 3.2 Atualizações — `#formAtualizacoesTurmas .rotator table`

```cpp
struct Atualizacao {
    QDate    data;        // "07/08/2026"
    QString  turmaNome;
    QString  idTurma;     // do jsfcljs
    QString  texto;       // "Nova tarefa: ...", "Avaliação marcada para o dia 03/12/2026",
                          // "Novo Arquivo: ...", "Novo Tópico de Aula: ..."
    QString  hash;        // sha1(data|idTurma|texto) — não há ID; a chave é o hash
};
```

Nota: o rotator só mostra **8 itens**. Se você só ler o portal, perde atualizações mais antigas.
Por isso o poll precisa rodar com frequência suficiente e persistir tudo que já viu.

### 3.3 Tópicos de aula — `#formAva .topico-aula .titulo`

```cpp
struct TopicoAula {
    QString  titulo;      // "Primeira avaliação"
    QDate    inicio;      // "(29/09/2026 - 29/09/2026)"
    QDate    fim;
    QString  conteudo;    // .conteudotopico (HTML → texto)
    QString  idTurma;
};
```

O formato é sempre `Título (dd/MM/yyyy - dd/MM/yyyy)` — regex direto no `.titulo`.

**Caso vazio:** quando o professor não registrou nada, vem o bloco `.intro-aval` ("O Sistema
detectou que até agora seu professor não criou nenhum tópico de aula"). Detectar isso e marcar a
turma como "sem ementa" — não é erro.

### 3.4 Detecção de provas — regex primeiro, IA depois

Do exemplo de EDO, os tópicos já dizem tudo:

```
"Primeira avaliação (29/09/2026)"
"Segunda avaliação (26/11/2026)"
"Avaliação substitutiva (03/12/2026)"
"Aula de dúvidas (24/09/2026)"   <- útil: marcador de revisão pré-prova
```

Um classificador por regex resolve ~90% dos casos:

```
(?i)\b(avalia[çc][ãa]o|prova|exame|teste|substitutiva|2ª\s*chamada|seminário|apresenta[çc][ãa]o|entrega)\b
```

**Você está certo: IA não é necessária para achar as provas.** O valor real da IA está em outro
lugar (ver §5).

---

## 4. Arquitetura

```
sigaa-viewer/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ src/
│  ├─ core/                  # ZERO dependência de UI — testável headless
│  │  ├─ http/
│  │  │  ├─ SigaaSession.{h,cpp}     // fila serializada, cookies, retry, keep-alive
│  │  │  └─ RateLimiter.{h,cpp}
│  │  ├─ auth/
│  │  │  ├─ LoginFlow.{h,cpp}        // login, vínculo, termo de uso, expirado
│  │  │  └─ CredentialStore.{h,cpp}  // QtKeychain
│  │  ├─ jsf/
│  │  │  ├─ JsfForm.{h,cpp}          // parse do <form> → fields + viewstate
│  │  │  └─ Jsfcljs.{h,cpp}          // emula jsfcljs(): monta o POST
│  │  ├─ parse/
│  │  │  ├─ PortalParser.{h,cpp}     // atividades + atualizações
│  │  │  ├─ TurmaParser.{h,cpp}      // tópicos de aula, materiais
│  │  │  └─ Html.{h,cpp}             // wrapper fino sobre lexbor
│  │  ├─ model/                      // structs puras da §3
│  │  ├─ store/
│  │  │  ├─ Database.{h,cpp}         // SQLite + migrations
│  │  │  └─ DiffEngine.{h,cpp}       // snapshot antigo vs novo → eventos
│  │  ├─ sync/
│  │  │  ├─ Scheduler.{h,cpp}        // poll com jitter/backoff
│  │  │  └─ Crawler.{h,cpp}          // portal → turmas, respeitando ViewState
│  │  ├─ calendar/
│  │  │  ├─ ExamDetector.{h,cpp}     // regex
│  │  │  └─ IcsExporter.{h,cpp}      // .ics — vitória barata e enorme
│  │  └─ ai/
│  │     └─ AiEnricher.{h,cpp}       // opcional, atrás de interface
│  ├─ ui/
│  │  ├─ MainWindow, DashboardView, TimelineView, CalendarView,
│  │     TurmaDetailView, SettingsDialog, TrayIcon
│  └─ app/main.cpp
└─ tests/
   ├─ fixtures/               # ← CRÍTICO
   │  ├─ portal_com_atividades.html
   │  ├─ portal_sem_atividades.html
   │  ├─ turma_com_topicos.html
   │  ├─ turma_sem_topicos.html      (o .intro-aval)
   │  ├─ sessao_expirada.html
   │  └─ login_ok.html / login_falha.html
   └─ *_test.cpp
```

**Regra de ouro do projeto: `core/` não conhece Qt Widgets.** Isso te dá um binário CLI de teste
(`sigaa-cli --json`) que roda em CI e desacopla o risco (scraping) do trabalho fácil (UI).

**Segunda regra: salve fixtures HTML de tudo.** O SIGAA vai mudar. Testar contra o site ao vivo é
lento, frágil e mal-educado. Todo parser nasce de um fixture.

### Diff engine (é isso que gera as notificações)

```sql
CREATE TABLE atividade (
  atividade_id TEXT, id_turma TEXT, titulo TEXT, prazo INTEGER,
  status TEXT, first_seen INTEGER, last_seen INTEGER,
  read_at INTEGER, dismissed INTEGER DEFAULT 0,
  PRIMARY KEY (atividade_id, id_turma)
);
CREATE TABLE atualizacao (
  hash TEXT PRIMARY KEY, data INTEGER, id_turma TEXT, texto TEXT,
  first_seen INTEGER, read_at INTEGER
);
```

Eventos emitidos: `NovaAtividade`, `PrazoMudou`, `AtividadeConcluida`, `NovaAtualizacao`,
`ProvaDetectada`, `PrazoProximo(24h/72h)`. A UI e as notificações consomem esses eventos.

---

## 5. Onde a IA vale a pena (e onde não)

**Não vale:** achar provas. Regex resolve, é determinístico, offline e grátis.

**Vale:**

1. **Plano de estudo a partir da ementa.** Dado o cronograma de EDO (34 tópicos) + data da 1ª
   avaliação (29/09), gerar: "cai da aula 1 à 15", tópicos por prioridade, cronograma reverso.
2. **Resumo/triagem de materiais.** Nomes como `Apostila_de_Analise_de_Algoritmos.pdf` não dizem
   nada. Classificar arquivos por tópico da ementa e dizer quais servem para qual prova.
3. **Linguagem natural sobre os dados.** "O que eu tenho pra entregar essa semana?", "quais provas
   em dezembro?"
4. **Normalizar texto sujo.** Enunciados de tarefa que os professores escrevem de qualquer jeito.

### Como integrar (C++ não tem SDK oficial da Anthropic)

Use **HTTP direto** para `POST https://api.anthropic.com/v1/messages` — é o caminho suportado para
linguagens sem SDK. Com QtNetwork você já tem tudo.

```cpp
// core/ai/AiEnricher.cpp — esqueleto
QJsonObject body {
    {"model", "claude-opus-5"},
    {"max_tokens", 16000},
    {"thinking", QJsonObject{{"type","adaptive"}}},
    {"output_config", QJsonObject{{"effort","high"}}},
    {"messages", QJsonArray{ QJsonObject{{"role","user"},{"content", prompt}} }}
};
req.setRawHeader("x-api-key", apiKey);              // do keychain, nunca hardcoded
req.setRawHeader("anthropic-version", "2023-06-01");
req.setRawHeader("Content-Type", "application/json");
```

Pontos de design:

- **Mande texto parseado, não HTML.** 34 tópicos limpos = ~1 KB. O HTML bruto = 200 KB. Barato e
  melhor resultado.
- **Structured outputs** (`output_config.format` com JSON Schema) para o calendário de provas —
  garante JSON válido, sem parsing frágil.
- **Prompt caching** na ementa da turma (`cache_control: {type:"ephemeral"}`) se você fizer várias
  perguntas sobre a mesma disciplina.
- **Sempre opcional e atrás de interface** (`IAiProvider`). O app tem que funcionar 100% sem chave
  de API. Chave fica no keychain, configurável nas Settings.
- **Sempre checar `stop_reason == "refusal"` antes de ler `content`.**
- Modelo: `claude-opus-5` é o default (US$5/US$25 por Mtok). Como o volume aqui é ínfimo (algumas
  chamadas por semana, ~2 KB de input), o custo é desprezível. Se quiser mais barato,
  `claude-sonnet-5` ou `claude-haiku-4-5` são opções — mas é sua decisão, não a minha.

---

## 6. Roadmap por fases

| Fase | Entrega | Esforço | Risco |
|---|---|---|---|
| **0. Recon** | HAR completo do login + navegação. Investigar API mobile. Salvar todos os fixtures HTML. Documentar o fluxo de login (vínculo? termo de uso? captcha?). | 1–2 dias | — |
| **1. Núcleo headless** | `sigaa-cli`: login → GET portal → parse atividades + atualizações → JSON no stdout. Sem UI, sem DB. | 1 semana | **ALTO** — é aqui que o projeto vive ou morre |
| **2. Persistência + diff** | SQLite, diff engine, scheduler, re-login automático, tray icon + notificações. Ainda sem janela. | 1 semana | Médio |
| **3. UI Qt** | MainWindow, Dashboard (o que vence agora), Timeline (atualizações), Settings. | 1–2 semanas | Baixo |
| **4. Turmas + calendário** | Crawl das turmas virtuais, parser de tópicos, ExamDetector, CalendarView, **export .ics**. | 1 semana | Médio (ViewState) |
| **5. IA** | AiEnricher, plano de estudo, triagem de materiais, Q&A. | 3–5 dias | Baixo |
| **6. Packaging** | GH Actions matrix, NSIS/MSI, .app+dmg, AppImage. Auto-update opcional. | 3–5 dias | Médio (assinatura de código) |

**Vá fase por fase e não pule a 1.** Se a Fase 1 não fechar em ~1 semana, o problema é o SIGAA e
não a sua UI — melhor descobrir isso antes de escrever 5.000 linhas de Qt.

---

## 7. Ganhos rápidos com ROI desproporcional

1. **Export .ics** (Fase 4). ~200 linhas. Coloca todas as provas e prazos no Google Calendar /
   Outlook / calendário do celular. Provavelmente a feature mais útil do app inteiro.
2. **Notificação D-3 / D-1 / D-0** para cada prazo, não só "chegou coisa nova".
3. **Modo tray-only** — o app fica minimizado e só aparece quando tem novidade. É assim que ele
   vai ser usado 95% do tempo.
4. **Contagem regressiva** já vem pronta do SIGAA (`(3 dias)`), mas calcular localmente é mais
   confiável e funciona offline.

---

## 8. Decisões pendentes (responder antes da Fase 1)

1. **Licença/distribuição:** open-source (LGPL do Qt é tranquilo) ou fechado? Muda a escolha
   Qt vs wxWidgets.
2. **QtWebEngine no bundle?** +120 MB, mas é o seguro contra o login quebrar. Sugestão: build flag,
   `OFF` por padrão, `ON` num "installer completo".
3. **Multi-usuário?** Se for só você, o modelo de dados simplifica muito (sem tabela de usuário).
4. **Existe API mobile na UNIFEI?** Resposta da Fase 0 — pode mudar 70% deste plano.
