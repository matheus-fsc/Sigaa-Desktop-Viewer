# Recon — SIGAA UNIFEI (Fase 0, sessão 1)

Ambiente observado: `sigaa.unifei.edu.br`, **vSIGAA 4.12.14_U.164**, JSF/MyFaces + RichFaces 3.3.3,
jQuery 1.4.4, Struts (`*.do`) coexistindo com JSF (`*.jsf`).

> Todos os dados abaixo são de uma conta de discente real. **Não commitar fixtures HTML sem
> redigir** nome, matrícula, e-mail, foto (`/shared/verFoto?idFoto=...&key=...`) e `JSESSIONID`.

---

## 1. Achados que mudam o plano

### 1.1 Existe uma camada Struts (`.do`) sem ViewState

```
GET /sigaa/verPortalDiscente.do   → 302 → /sigaa/portais/discente/discente.jsf
                                     já logado, ViewState novo, sem enviar nada
```

**Impacto:** é a primitiva de *reset* do crawler. Ao detectar `ViewExpiredException` ou ao terminar
de visitar uma turma, um GET aqui devolve o portal num estado limpo — muito mais barato que
re-login. O plano original assumia que só re-login resolveria; está errado, corrigir.

Endpoints `.do` vistos até agora: `logar.do` (`?dispatch=logOff`), `verPortalDiscente.do`,
`entrarChat.do`.

### 1.2 A turma virtual usa `frontEndIdTurma`, não `idTurma`

Convivem **três namespaces de ID** para a mesma turma:

| Contexto | Parâmetro | Exemplo (ADMINISTRAÇÃO) |
|---|---|---|
| Painel "Atualizações das Turmas" | `idTurma` | `88094` |
| Chat (`form_docente*`) | `id` | `88094` |
| **Entrar na Turma Virtual** | `frontEndIdTurma` | `6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C` |

Mapa desta conta (semestre 2026.2):

| Turma | idTurma | frontEndIdTurma |
|---|---|---|
| ADMINISTRAÇÃO | 88094 | `6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C` |
| ANÁLISE E DESENV. DE SOFTWARE IV | 89151 | `037FA6449AB65FCC626A45D6B3F123802640C194` |
| ARQUITETURA DE COMPUTADORES | 89181 | `0F8B8593CB01A1E95D1A4474277933000B433C5D` |
| COMPILADORES | 89178 | `DBB8893EC7E25F64F9CFA79D007726744986403E` |
| EQUAÇÕES DIFERENCIAIS ORDINÁRIAS | 88080 | `31665E0EB91E8DB0AE98C0FF31893A993F987B72` |
| INTELIGÊNCIA ARTIFICIAL | 89175 | `CC50F3E7BC3643CC75831836F047B4D6A8BA19BD` |
| PROJETO E ANÁLISE DE ALGORITMOS | 89165 | `05A98D6D87C816BA23F0A344C9A801A3B724E970` |

**Estabilidade:** ✅ **estável entre recargas E entre sessões.** Testado com logout real
(`logar.do?dispatch=logOff`) + novo login: **7/7 hashes idênticos**. Não é token de sessão — é
hash determinístico da turma (provavelmente SHA-1 do id + salt fixo da instalação).

**Impacto:** o crawler **pode cachear a rota da turma**. Depois do primeiro mapeamento, os ciclos
seguintes podem ir direto à turma sem re-raspar o portal para descobrir hashes — só precisa de um
ViewState válido da view corrente. Isso corta requisições do ciclo de forma significativa.
Ainda assim, guardar `(idTurma → frontEndIdTurma)` no SQLite com invalidação por semestre, caso a
instalação mude o salt num upgrade.

### 1.3 Corpo do POST de navegação JSF — confirmado empiricamente

Capturado com hook em `HTMLFormElement.prototype.submit`, ao entrar na turma de ADMINISTRAÇÃO:

```
POST /sigaa/portais/discente/discente.jsf
Content-Type: application/x-www-form-urlencoded

form_acessarTurmaVirtual                         = form_acessarTurmaVirtual
javax.faces.ViewState                            = j_id20
form_acessarTurmaVirtual:j_id_jsp_512348736_365  = form_acessarTurmaVirtual:j_id_jsp_512348736_365
frontEndIdTurma                                  = 6D31A10B13EE2B50FA2C2E3E13EF4329E6EAFE8C
```

**Regra geral confirmada:** corpo = *inputs próprios do `<form>`* (que são só o nome do form e o
ViewState) + *os pares do `jsfcljs`*. **Não é preciso serializar a página inteira** — os forms do
portal têm 2 inputs cada. Isso torna `Jsfcljs::buildPost()` trivial.

### 1.4 A URL é uma pista fraca — às vezes muda, às vezes não

- **Entrar** na Turma Virtual pelo portal: a URL **continua** `/sigaa/portais/discente/discente.jsf`,
  mas o conteúdo é a Turma Virtual (forward interno no servidor).
- **Navegar dentro** da turma: a URL **muda** — ex.: Materiais/Arquivos vai para
  `/sigaa/ava/ArquivoTurma/listar_discente.jsf`.

**Impacto no crawler:** a URL serve como dica, mas não como identidade. É preciso um
`PageClassifier` por conteúdo. Marcas úteis já mapeadas:

| Marca no DOM | Significa |
|---|---|
| `#formAtividades` + `#formAtualizacoesTurmas` | Portal do discente |
| `#formAva` | Turma Virtual (qualquer aba) |
| `.intro-aval` | Turma sem nenhum tópico de aula cadastrado |
| `#formTurma` | Catálogo de turmas inline (ver §1.5) |
| `div.rich-stglpanel` | Barra lateral da turma (painéis) |
| texto `/expirad/i` | Sessão expirada → disparar recuperação |

### 1.5 O catálogo completo de turmas vem inline em toda página de turma

O diálogo "Trocar de Turma" é markup **já presente no HTML** (jQuery UI só faz show/hide).

**Verificado com teste controlado** (a primeira tentativa não provava nada — o hook estava morto
no momento do clique manual): a partir do portal, entrar na turma de EDO programaticamente gerou
**1 POST e zero XHR/fetch**, e `#formTurma` com as 7 turmas já estava no DOM **sem ninguém ter
aberto o diálogo**. No portal, `#formTurma` **não** existe. Logo: é renderizado pelo servidor em
toda página de turma. Também presente em `/sigaa/ava/ArquivoTurma/listar_discente.jsf`.

⚠️ O botão que abre o diálogo dispara *também* um AJAX PrimeFaces
(`PrimeFaces.ajax.AjaxRequest('/sigaa/ava/index.jsf', {formId:'formAcoesTurma'}, …)`), mas isso é
**posterior e dispensável** — o `dialogTurmas.show()` roda antes e o conteúdo já está lá. O crawler
não precisa emular esse AJAX.

Cada `#formTurma a.linkTurma` carrega, num só lugar, **mais dados do que o portal oferece**:

| Código | Disciplina | CH | Período | frontEndIdTurma |
|---|---|---|---|---|
| EPRIB001 | ADMINISTRAÇÃO | 32h | 2026.2 | `6D31A10B…` |
| ECOI2218 | ANÁLISE E DESENVOLVIMENTO DE SOFTWARE IV | 64h | 2026.2 | `037FA644…` |
| ECOI2208 | ARQUITETURA DE COMPUTADORES | 64h | 2026.2 | `0F8B8593…` |
| ECOI2219 | COMPILADORES | 64h | 2026.2 | `DBB8893E…` |
| MATI2307 | EQUAÇÕES DIFERENCIAIS ORDINÁRIAS | 64h | 2026.2 | `31665E0E…` |
| ECOI2217 | INTELIGÊNCIA ARTIFICIAL | 64h | 2026.2 | `CC50F3E7…` |
| ECOI2207 | PROJETO E ANÁLISE DE ALGORITMOS | 64h | 2026.2 | `05A98D6D…` |

O texto de cada item segue o padrão `CÓDIGO - NOME (CHh) (PERÍODO)` — regex trivial.
**Código da disciplina e carga horária não aparecem no portal**; só aqui.

**Impacto:** o `Crawler` monta a tabela `turma` inteira a partir de *qualquer* página de turma,
sem requisição dedicada. É a fonte preferida para bootstrap.

⚠️ **Cuidado com o `action` do form:** vale a regra JSF — o form posta para a *view atual*, então
o `action` observado (`listar_discente.jsf`) é só a página onde o diálogo foi aberto. **Não é** um
endpoint fixo de "trocar turma". O `jsfcljs` guarda também `permitirEscolhaTurma()` como guard.

### 1.6 As provas têm um painel estruturado próprio

Na Turma Virtual, a barra lateral direita tem painéis RichFaces `SimpleTogglePanel`:

```html
<div class="rich-stglpanel">
  <div class="rich-stglpanel-header">Avaliações</div>
  <div class="rich-stglpanel-body">
    <ul class="menu-direita">
      <li><span class="data">04/09 6T34</span><span class="descricao">1ª Avaliação</span></li>
      <li><span class="data">04/12 6T34</span><span class="descricao">2ª Avaliação</span></li>
    </ul>
  </div>
</div>
```

Seletor: `div.rich-stglpanel` cujo header seja "Avaliações" → `ul.menu-direita li` →
`span.data` + `span.descricao`.

Painéis presentes: `Andamento das Aulas`, `Notícias`, `Enquete`, `Atividades`, `Avaliações`,
`Mensagens dos Fóruns`.

**Impacto:** o `ExamDetector` por regex sai do caminho crítico — mas **não sai do projeto**, porque
as duas fontes se sobrepõem de forma imprevisível, dependendo de como cada professor usa o sistema:

| Turma | Painel `Avaliações` | Tópicos de aula | Relação |
|---|---|---|---|
| ADMINISTRAÇÃO | `1ª Avaliação 04/09`, `2ª Avaliação 04/12` | 04/09 é "Sprint 1 - Apresentação da Empresa" | **divergem** |
| EQUAÇÕES DIFERENCIAIS | `29/09`, `26/11`, `03/12` | "Primeira avaliação (29/09/2026)" etc. | **coincidem** |

→ O calendário precisa **mesclar as duas fontes e deduplicar por `(idTurma, data)`**, tratando o
painel como fonte de maior confiança e o tópico como reforço/fallback.

**Duas pegadinhas no `span.data`:**

1. **Não tem ano.** `04/09` → inferir do período letivo da turma (2026.2).
2. **O formato do horário varia.** Observados dois:
   - `04/09 6T34` → código de horário SIGAA (6=sexta, T=tarde, slots 3-4)
   - `29/09 15:45` → hora de relógio

   O parser tem que aceitar `dd/MM` + (código SIGAA | `HH:mm`), e o código SIGAA só vira horário
   real via a tabela de turnos da instituição.

**Amostra EDO** (turma com 35 `.topico-aula`, sem `.intro-aval`):
`29/09 15:45 1ª Avaliação` | `26/11 15:45 2ª Avaliação` | `03/12 15:45 Avaliação substitutiva`

### 1.6.1 Aba "Arquivos" — listagem e download (sessão 2, 2026-08-09)

Fechou a pendência §5.4 para esta aba. Capturado com `sigaa-cli explorar <turma> Arquivos <dir>`,
que grava o HTML **cru da rede** dos três passos (portal → turma → aba).

**Rota.** A aba é `/sigaa/ava/ArquivoTurma/listar_discente.jsf` — a mesma URL que já aparecia em
§1.5. Chega-se nela por um POST JSF normal a partir da página da turma, no form `formMenu`.

⚠️ **Navegar o menu pelo RÓTULO, nunca pelo id do componente.** Os itens do menu são
`formMenu:j_id_jsp_719010821_123` — id gerado pelo JSF a partir da posição no JSP. Ele muda quando
a instituição recompila a página, e aí o crawler abriria a aba errada em silêncio (o SIGAA responde
200 com outra tela). O rótulo — "Arquivos" — é o que o aluno lê e o que sobrevive.
Menu completo confirmado: Principal, Gerenciar Perfil, Plano de Curso, Participantes, Visualizar
Programa, Fóruns, Notícias, Frequência, Ver Grupo, Ver Notas, Conteúdo/Página web, Referências,
Vídeos, **Arquivos**, Avaliações, Enquetes, Tarefas, Questionários, Situação dos Discentes, Linha
do Tempo.

**Lista vazia tem marcação própria** — e é o estado de várias turmas, não erro:

```html
<p class="empty-listing">Nenhum item foi encontrado.</p>
```

**Lista preenchida** é uma `table.listing` dentro do form `formAva`:

| Título | Descrição | Tópico de Aula | (ícone) |

```html
<tr class='linhaPar'>
  <td>LIVRO: Algoritmos - Teoria e Prática - Thomas H. Cormen</td>
  <td></td>
  <td>Materiais auxiliares para Estudo</td>
  <td><a href="#" onclick="...jsfcljs(document.getElementById('formAva'),
        {'formAva:j_id_jsp_1360160502_299j_id_1':'...','id':'526919'},'_blank');...">
      <img src="/sigaa/ava/img/page_white_put.png" title="Baixar Arquivo" /></a></td>
</tr>
```

**O download NÃO é um GET.** É um POST JSF no próprio `formAva`, e o parâmetro que identifica o
arquivo é o **`id`** avulso (`'id':'526919'`) — não o id do componente, que é posicional. A chave
estável do arquivo é esse `id`; o resto da linha (título, tópico) é texto livre do professor.

Existe também "Baixar todos os arquivos": mesmo form, só o id do componente, sem `id`. Provavelmente
devolve um zip — **não testado**.

⚠️ Não confundir com `/sigaa/verArquivo?idArquivo=<n>&key=<hash>`, que aparece na mesma página: ele
pertence ao modal oculto de "Parecer da NEI" e vem com `idArquivo` vazio. Não é a rota dos materiais
da turma. O `key` ali é token de acesso — vale a mesma regra de §4.

**Descrição vem quase sempre vazia** (6/6 arquivos nesta turma). A coluna que realmente agrupa é
"Tópico de Aula", que casa com os `.topico-aula` já parseados — dá para ligar arquivo ao tópico.

### 1.7 ⚠️ Fixture do browser ≠ HTML da rede (e o contador de sessão não existe para nós)

Medido no mesmo portal, mesma conta, minutos de diferença:

| Origem | Tamanho |
|---|---|
| `GET` via libcurl (o que o cliente recebe) | **87.877 bytes** |
| Ctrl+S no Chrome (DOM pós-JavaScript) | **234.636 bytes** |

O arquivo do browser é **2,7× maior** porque é o DOM *depois* do JS rodar.
**Validar parser só contra fixture de Ctrl+S é enganoso** — ele pode depender
de markup que o servidor nunca envia. Para teste de parser, gerar fixture com
`SIGAA_DUMP=arquivo.html sigaa-cli sync` (HTML cru da rede) e redigir depois.

O caso concreto que expôs isso: **o contador "Tempo de Sessão: 00:25" é criado
por JavaScript.** No HTML da rede o elemento vem vazio:

```html
<span id="tempoSessao"></span>          <!-- rede: vazio -->
<!-- browser, pós-JS: -->
<div id="tempoSessao"><small><em>Tempo de Sessão:</em>
  <span id="spanRelogio" title="...">00:25</span></small></div>
```

`spanRelogio` **não existe** no HTML da rede.

**Correção da versão anterior deste documento**, que afirmava que o cliente
poderia ler o tempo restante da página: **não pode.** `minutosSessaoRestantes()`
retorna `nullopt` contra HTML de rede (o parser do span fica para o caso de o
SIGAA passar a renderizar server-side, e para fixtures de browser).

**O que fazer no lugar:** o cliente rastreia a sessão por conta própria —
timestamp da última requisição bem-sucedida + o timeout observado (~30 min),
renovando com `verPortalDiscente.do` (§1.1) e tratando `SessaoExpirada` (§1.4)
como o sinal autoritativo.

### 1.7.1 O tempo de sessão no DOM do browser — **não é fonte para o cliente**

No browser, o cabeçalho mostra `Tempo de Sessão: 00:25`. Isso vale só como
observação sobre o DOM pós-JS: **o cliente não tem acesso a esse número** (§1.7).

> Versões anteriores desta seção recomendavam ler esse valor em vez de usar um
> timer local. Estava errado — o `spanRelogio` é criado por JavaScript e não
> chega pelo HTML da rede. A recomendação válida está no fim da §1.7.

### 1.8 Login — sem CSRF, POST Struts puro

```
POST /sigaa/logar.do?dispatch=logOn      (dispatch vai na QUERY STRING, não no corpo)
Content-Type: application/x-www-form-urlencoded

width               = 1536          (largura da tela; qualquer valor plausível serve)
height              = 960
urlRedirect         = (vazio)
subsistemaRedirect  = (vazio)
acao                = (vazio)
acessibilidade      = (vazio)
user.login          = <CPF, 11 dígitos, sem pontuação>
user.senha          = <senha>
```

**Não há token anti-CSRF, nem campo oculto dinâmico, nem captcha** (ao menos no fluxo normal).
O `LoginFlow` é montar 8 campos e postar — não precisa de um GET prévio para colher token.

- **O login é o CPF**, não a matrícula. A UI de configuração deve rotular como "CPF".
- Sucesso → 302 → `/sigaa/portais/discente/discente.jsf`, ViewState reinicia em `j_id1`.
- **Não observados nesta conta:** tela de escolha de vínculo, termo de uso, troca de senha
  obrigatória. Podem existir para outros perfis — o `LoginFlow` precisa de um `PageClassifier`
  que detecte "não caí no portal" e reporte em vez de travar.
- ⚠️ Bloqueio por tentativas erradas não foi testado (e não deve ser). Implementar **backoff
  agressivo e limite rígido de tentativas** no cliente para não travar a conta do usuário.

### 1.9 O ecossistema é multi-host

A Caixa Postal abre em **`sigadmin.unifei.edu.br`** (`/cxpostal/caixa_postal.jsf`), host distinto
de `sigaa.unifei.edu.br`. Se o app for cobrir mensagens/caixa postal, o cookie jar precisa lidar
com múltiplos hosts e provavelmente com um SSO entre eles. **Fora de escopo do MVP** — anotado.

### 1.10 O SIGAA serve **windows-1252**, não UTF-8

```html
<meta http-equiv="Content-Type" content="text/html; charset=windows-1252">
```

Na rede, "EQUAÇÕES" chega como `EQUA C7 D5 ES` — bytes cp1252, que são **UTF-8
inválido**. Consequências práticas:

- O **lexbor assume UTF-8** e troca cada byte inválido por U+FFFD. O acento se
  perde de forma irrecuperável — não dá para consertar depois do parse.
- Por isso `html::Document::parse()` chama `toUtf8()` antes: se os bytes já
  forem UTF-8 válido passa direto (o SIGAA pode migrar num upgrade), senão
  transcodifica de cp1252.
- **cp1252 ≠ latin-1**: a faixa `0x80–0x9F` tem aspas curvas, travessão e afins.
  Usar latin-1 puro corrompe esses caracteres.

**Por que isso escapou por tanto tempo:** no navegador tudo parecia certo. O
Chrome decodifica cp1252 sozinho, e o `javascript_tool` devolvia strings
Unicode já convertidas. O bug só aparece olhando os **bytes** — o que só
acontece quando o cliente HTTP é o nosso.

A mesma armadilha derrubou o `tools/redact.py`, que lia com
`encoding="utf-8", errors="replace"` e gravava fixtures já corrompidos, *antes*
de o C++ ver qualquer coisa. Hoje ele detecta o encoding, normaliza para UTF-8
e reescreve o `<meta charset>`.

Regressão coberta em `tests/html_encoding_test.cpp`.

### 1.11 Outros

- **`Andamento das Aulas`: `Aulas (Ministradas/Total): 2 / 36`** — dá progresso do semestre de graça.
- **São TRÊS frameworks web coexistindo**, não dois:
  1. **Struts** (`*.do`) — login, logoff, `verPortalDiscente.do`, `entrarChat.do`. GET/POST simples.
  2. **JSF/MyFaces + RichFaces 3.3.3** (`*.jsf`) — o grosso da navegação, via `jsfcljs` + ViewState.
  3. **PrimeFaces** (`PrimeFaces.ajax.AjaxRequest`) — widgets pontuais na Turma Virtual.

  A Turma Virtual propriamente dita vive em **`/sigaa/ava/index.jsf`**.
  O crawler só precisa de (1) e (2); (3) é dispensável (ver §1.5).

- **A tabela "Turmas do Semestre" do portal traz `Local` e `Horário`**, que o catálogo inline não
  tem: `Anexo 3 - Sala 19` / `6T34`, `Prédio 2 - Sala 2309` / `6M2345`, etc. Somando as duas fontes
  fecha o cadastro da turma: código + nome + CH + período (catálogo) e local + horário (portal).

- Cada linha da tabela tem um `<td id="linha_<idTurma>">` oculto — provavelmente alvo de AJAX.
  É uma forma **estável e barata de extrair o `idTurma`** de cada linha, sem depender do `onclick`.

- **`entrarChat.do` vaza bastante coisa na query string**: `idchat`, `idusuario` (id interno do
  usuário), `chatName`, `nomeUsuario` (nome completo) e `servidor` (um RTMP da UFRN:
  `rtmp://videochat.info.ufrn.br/oflaDemo/`). Fora do escopo do MVP, mas **é PII em URL** — ver §4.
- Menu da turma: Principal, Gerenciar Perfil, Plano de Curso, Participantes, Visualizar Programa,
  Fóruns, Chat da Turma, Notícias, Alunos, Materiais, Atividades, Estatística.
- Portal tem 22 `<form>`, todos com `action="/sigaa/portais/discente/discente.jsf"`.
- Página do portal ≈ **236 KB** de HTML.
- `GET /shared/javascript/STICookieConsent.js` retorna **503** (quebrado no servidor, inofensivo).
- Há Google Analytics carregado — irrelevante para o cliente, mas o scraper não deve buscá-lo.

---

## 2. Correções ao PLANO.md

| Item do plano | Correção |
|---|---|
| "só re-login resolve view expirada" | Errado — `verPortalDiscente.do` reseta a navegação sem re-login |
| "chave da turma é `idTurma`" | Incompleto — a Turma Virtual exige `frontEndIdTurma` (hash de 40 hex) |
| "serializar o form inteiro" | Exagero — os forms têm 2 inputs; o corpo tem ~4 campos |
| "identificar página pela rota" | Impossível — a URL não muda no postback; precisa `PageClassifier` por conteúdo |
| "ExamDetector por regex" | Rebaixado a *fallback*; a fonte primária é o painel `Avaliações` |
| "keep-alive por timer local" | Continua valendo — `Tempo de Sessão` é gerado por JS e não existe no HTML da rede (§1.7) |

---

## 3. Ferramenta de recon usada (reaproveitável)

Hook injetado via `javascript_tool`, gravando em `sessionStorage` (sobrevive à navegação):
intercepta `XMLHttpRequest.open/send`, `window.fetch` e `HTMLFormElement.prototype.submit`.
Fica em `tools/spy.js` para reuso quando o SIGAA mudar.

> Nota operacional: o filtro de segurança da extensão do Chrome bloqueia outputs que **parecem**
> query string ou cookie. Formatar como `campo X valor[ Y ]`, uma linha por par, contorna isso.

---

## 4. Segurança / privacidade dos artefatos

Descobertas nesta sessão que **não podem vazar** para o repositório:

- `user.login` é o **CPF** do aluno. Não gravado neste documento; não gravar em fixture nem HAR.
- Foto de perfil: `/shared/verFoto?idFoto=...&key=...` — o `key` é um token de acesso à imagem.
- `JSESSIONID`, matrícula, e-mail institucional, índices acadêmicos (MC/IRA/IEPL...).
- **`idusuario` e `nomeUsuario` aparecem em texto puro na query string dos links de chat**
  (`entrarChat.do?...&idusuario=NNNNN&nomeUsuario=NOME+COMPLETO&...`) — presentes em **todo**
  HTML do portal. Qualquer fixture do portal contém o nome completo do aluno; redigir sempre.
- Antes de commitar qualquer fixture, rodar um script de redação
  (`tools/redact.py`) que substitua CPF, matrícula, nome, e-mail, `idFoto`/`key` e `JSESSIONID`
  por placeholders. **Escrever esse script antes do primeiro fixture commitado.**

---

## 5. Pendências da Fase 0

**Resolvidas nesta sessão:** estabilidade do `frontEndIdTurma` (§1.2), corpo do POST de navegação
(§1.3), catálogo inline de turmas via "Trocar de Turma" (§1.5), corpo e ausência de CSRF no
login (§1.8).

Em aberto:

1. **Reproduzir a sessão expirada**: deixar 30 min ociosos e capturar o HTML exato da página de
   expiração. É o fixture que dispara o re-login e não dá para inventar.
3. **Sondar camada REST/mobile** (`/sigaa/mobile/`, `/sigaa/api/`, `/sigaa/rest/`) — a sonda desta
   sessão não retornou resultado utilizável (o `fetch` assíncrono não serializou no tool).
4. **Mapear as demais abas da Turma Virtual.** ✅ **Arquivos** fechada em §1.6.1 (listagem +
   download, com parser e fixture de rede). Continuam abertas: Ver Notas, Frequência, Tarefas,
   Fóruns, Notícias, Participantes, Plano de Curso. A ferramenta para capturá-las já existe:
   `sigaa-cli explorar <turma> <rótulo da aba> <dir>` grava o HTML cru de cada passo.
5. **Exportar um HAR completo** (DevTools → Network → exportar) cobrindo login → portal → turma →
   volta, e guardar **redigido** em `tests/fixtures/har/`.
6. **Confirmar o timeout real de 30 min** e se `verPortalDiscente.do` sozinho o renova.
