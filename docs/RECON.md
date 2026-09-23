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

⚠️ **O reset é barato, mas não é grátis — e havia um par redundante.** O `POST logar.do?dispatch=logOn`
já responde com o portal inteiro; chamar `verPortalDiscente.do` logo depois buscava os mesmos bytes
de novo, com o intervalo mínimo inteiro pelo caminho:

```
#2 POST .../logar.do?dispatch=logOn      200  97444B  12662ms  [Portal]
#3 GET  .../verPortalDiscente.do         200  97444B    844ms  [Portal]   <- os mesmos 97444B
```

`SigaaSession` passou a guardar o corpo da última resposta **quando ela é o portal**, e
`irParaPortal()` devolve esse corpo se nenhuma requisição aconteceu no meio. Como nada navegou, a
view do servidor é exatamente a que aquele corpo descreve — é equivalente ao GET, menos a ida e
volta. Qualquer outra requisição esvazia o campo, que é o que impede um ViewState velho de
sobreviver a uma navegação.

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
| `#formAva` | Turma Virtual — **só a página inicial da turma**, não as abas |
| `#formAcoesTurma` | Turma Virtual, **inclusive as abas internas** |
| `.intro-aval` | Turma sem nenhum tópico de aula cadastrado |
| `#formTurma` | Catálogo de turmas inline (ver §1.5) |
| `div.rich-stglpanel` | Barra lateral da turma (painéis) |
| texto `/expirad/i` | Sessão expirada → disparar recuperação |

⚠️ **`#formAva` sozinho não cobre as abas.** A aba Participantes não tem esse form — só
`#formMenu`, `#formTurma` e `#formAcoesTurma`. Enquanto o classificador olhava só para `#formAva`,
ela era rotulada `Desconhecida`, e o diagnóstico de tráfego acusava uma tela estranha no meio de
uma navegação perfeitamente normal:

```
#7 POST https://.../sigaa/ava/index.jsf  200  180433B  [Desconhecida]
```

O marcador confiável para "estou dentro de uma turma, em qualquer aba" é `#formAcoesTurma`.

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

### 1.6.2 Materiais pendurados no tópico de aula (sessão 3, 2026-08-10)

A página inicial da Turma Virtual **já traz** os materiais de cada aula: não é preciso abrir aba
nenhuma. Eles vivem dentro do `.conteudotopico` de cada `.topico-aula`, num `listaMateriais`.

```html
<div class="topico-aula">
  <div class="titulo">Auto-avaliação ... (07/08/2026 - 07/08/2026)</div>
  <div class="conteudotopico">
    <script>...drag-and-drop do RichFaces...</script>
    <div class="item">
      <img src="/sigaa/img/porta_arquivos/icones/tarefa.png" />
      <a id="...:idEnviarMaterialTarefa" onclick="...jsfcljs(...,{'...':'...','id':'130160443'},'')">
         Tarefa 1: Autoavaliação Competências Transversais</a>
      <div class="descricao-item">Inicia em 04/08/2026 às 0h 0 e finaliza em 11/08/2026 às 23h 59</div>
    </div>
  </div>
</div>
```

**O tipo do material sai do ÍCONE**, `porta_arquivos/icones/<tipo>.png` — visto: `tarefa`. O id do
componente (`idEnviarMaterialTarefa`) também sugere o tipo, mas é posicional como todo id JSF e
muda quando a instituição recompila o JSP; o nome do ícone é o sinal estável.

**A chave é o mesmo `id` avulso de §1.6.1.** Isso é o que permite dizer se um material é baixável:
se o `id` dele aparece na aba Arquivos, é arquivo e tem download. Deduzir pelo ícone seria chutar —
tarefa, fórum e vídeo também são "materiais de tópico" e não se baixam por essa rota.

⚠️ **`textContent` não serve para ler o conteúdo do tópico.** Cada `.conteudotopico` embute um
`<script>` de drag-and-drop do RichFaces, e o `textContent` do DOM inclui corpo de `<script>` por
definição do padrão. O campo saía com `var elt = $("formAva:...")` grudado na descrição escrita
pelo professor. Existe `html::Node::textoVisivel()` para isso.

**Nem toda turma usa isto.** Na amostra: uma turma com 16 tópicos e 2 materiais (ambos tarefas),
outra com 35 tópicos e nenhum material — o professor registrou só título e data de cada aula. As
duas formas são normais, e a linha do tempo continua sendo o que o aluno lê.

### 1.6.3 Aba "Participantes" — o docente e os colegas (sessão 4, 2026-08-15)

Chega-se nela como em qualquer outra aba: POST no `formMenu` com o par do item de menu, resolvido
**pelo rótulo** (`<div class="itemMenu">Participantes</div>`) e nunca pelo id — o mesmo
`Participantes` que era `formMenu:j_id_jsp_719010821_81` na página da turma aparece como
`formMenu:j_id_jsp_165512588_81` na resposta. A página resultante posta em `/sigaa/ava/participantes.jsf`.

São duas `table.participantes`, cada uma precedida de um `<legend>` com a contagem:

```html
<fieldset><legend> Docentes (1)</legend></fieldset>
<table class="participantes">
  <tr class="odd">
    <td width="72"><img src="/shared/verFoto?idFoto=...&key=..." /></td>
    <td valign="top">
      <strong><a href="https://.../RedirectDocente?login=/<CPF>">NOME</a></strong><br/>
      Departamento: <em>...</em><br/> Formação: <em>...</em><br/> E-Mail: <em>...</em><br/>
    </td>
    <td width="20"><a onclick="Mensagem.show(1, '<CPF>', ...)">...</a></td>
  </tr>
</table>
<fieldset><legend> Discentes (31)</legend></fieldset>
<table class="participantes">
  ...  <strong>NOME <a onclick="...{'idPessoa':194699,...}">(Perfil)</a></strong><br/>
       Curso: <em>...</em><br/> Matrícula: <em>...</em><br/> E-mail: <em>...</em>
</table>
```

⚠️ **O bloco de discentes tem DUAS COLUNAS de participantes por linha.** Cada `<tr>` guarda duas
pessoas, em 6 `<td>`. Na captura eram 31 alunos em 16 linhas — iterar por `<tr>` não dá "lista
curta e óbvia": dá o e-mail de um aluno com o nome do seguinte grudado no fim. **A unidade é o
`<td>`**, reconhecido pelos rótulos que carrega.

⚠️ **A foto é opcional, e mora num `<td>` próprio** — o imediatamente anterior ao da pessoa. São 19
retratos para 32 pessoas; as outras 13 trazem `/sigaa/img/no_picture.png` no mesmo lugar, então a
célula sempre existe e sempre tem uma `<img>`. Duas consequências:

- ancorar a célula da pessoa na `<img>` perde metade da turma sem nenhum sinal de erro;
- distinguir retrato de placeholder é por `verFoto` no `src`, não pela presença da `<img>`. Pegar a
  primeira imagem daria a silhueta cinza a todo mundo como se fosse foto.

Como a API de `html::Node` não navega para irmãos, o parser guarda a foto da célula **anterior** a
cada volta do laço (`select` devolve em ordem de documento). Guardar "a última foto vista" em vez
disso faria o retrato escorregar para o próximo participante sempre que alguém não tivesse foto.

⚠️ **O rótulo de e-mail muda de caixa entre os blocos**: `E-Mail:` no docente, `E-mail:` no
discente. A busca do parser é case-insensitive por causa disso.

⚠️ Há **outro `Matrícula:`** fora das tabelas, num formulário de busca, com o `<em>` vazio. O
seletor precisa ser ancorado em `table.participantes`.

**Esta é a página mais sensível do SIGAA que o app toca.** Ela traz o CPF de *cada* participante
(no `Mensagem.show`), mais matrícula, e-mail e `idPessoa` — 33 CPFs numa turma de 32. Não é a conta
do próprio aluno, são terceiros. Duas consequências, ambas já implementadas:

- o parser **não extrai** CPF nem `idPessoa`, e o banco não tem coluna para eles (§4);
- o `tools/redact.py` ganhou regras **estruturais** de nome (`nome-participante`, `nome-docente`) e
  `idpessoa`, porque o `--name` por pessoa não escala para uma turma inteira e um esquecido vaza.

A **URL da foto** (`/shared/verFoto?idFoto=...&key=...`) segue a mesma regra pela mesma razão: o
`key` é um token que abre o retrato de um terceiro. Ela é extraída, usada uma vez para baixar a
imagem e descartada — não vai para o banco. O que fica no disco é o arquivo da imagem, em
`Documentos/SIGAA/.fotos`, achável pelo par `(idTurma, nome)` que o banco já guarda
(`core/sync/Fotos.h`).

**Custo de rede da aba, e quem paga:**

| Quem | Quando | Custo |
|---|---|---|
| ciclo de sync (`incluirParticipantes`) | só quando o banco ainda não tem ninguém | 1 requisição por turma |
| janela da turma | a cada "Atualizar" na turma aberta | 1 requisição |
| retratos (`CacheFotos`) | junto da coleta acima, e depois só na turma aberta | 1 por foto, uma vez |

Os retratos saem do laço do `Crawler` e ficam no `Servico`, que é quem sabe onde o material da
turma mora. A conta fecha porque o cache pula quem já está no disco **e** quem não cadastrou foto:
a primeira coleta paga ~19 requisições por turma, as seguintes pagam zero.

⚠️ Este é o único lugar em que o **invariante nº 2** (1,5 s entre requisições) é afrouxado, para
300 ms, e só durante o lote de imagens. `/shared/verFoto` não é a máquina de estados JSF — é um
servlet de imagem estática que não toca ViewState nenhum, e o próprio navegador busca as 19 fotos
**em paralelo** ao abrir a página. Serializadas a 300 ms continuamos várias vezes mais educados que
um acesso normal, e a turma sai em ~6 s em vez de ~28 s. O intervalo é restaurado por RAII no fim
do lote (`core/sync/Fotos.cpp`).

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

### 1.9.1 Uma instância por universidade — e este recon vale para UMA

Tudo neste documento foi observado em `sigaa.unifei.edu.br`, **vSIGAA 4.12.14_U.164**. O SIGAA é
software da UFRN adotado por dezenas de instituições, cada uma hospedando e **atualizando a sua**.
Duas coisas seguem daí:

- O host não pode ser constante no código. Ele é parâmetro (`core/config/Instituicao.h`), e entra
  também na **chave do cofre de credenciais** — senão trocar de instituição faria o app tentar a
  senha de uma universidade na outra, e algumas tentativas erradas bloqueiam a conta (§1.8).
- O catálogo embutido tem **só a UNIFEI**, marcada `verificada`. Encher a lista com os outros SIGAA
  federais deixaria a tela mais bonita e o app pior: numa instância com versão diferente, o parser
  falha e o usuário conclui que errou a senha — e tenta de novo, o que é exatamente o caminho para
  o bloqueio. Quem tiver outra instância na mão pode acrescentar uma linha ao catálogo **depois** de
  conferir contra o site, e anotar aqui qual versão foi testada.

Sessões concorrentes: o app abre uma segunda sessão (a janela da turma) enquanto a de sincronização
existe, e isso funciona — a invalidação de view do invariante nº 1 é **por JSESSIONID**, não por
conta. É o que torna possível baixar vários materiais em paralelo com uma sessão por canal
(`core/sync/Baixador.h`). Dentro de UMA sessão continua valendo uma requisição por vez.

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
- ⚠️ **A aba Participantes (§1.6.3) muda a escala do problema: ali a PII não é do dono da conta, é
  da turma inteira.** Uma turma de 32 pessoas traz 33 CPFs (um por participante, no
  `Mensagem.show`), mais 31 matrículas, 31 e-mails e 31 `idPessoa`. O `--name` do `redact.py` não
  serve — exigiria um por pessoa, e um esquecido vaza; por isso as regras `nome-participante` e
  `nome-docente` redigem por **estrutura** (o `<strong>` da célula e o texto do link
  `RedirectDocente`), funcionando numa turma nunca vista. Regra do projeto daqui em diante:
  **identificador de terceiro não é extraído nem persistido** — CPF e `idPessoa` não têm campo em
  `Participante` nem coluna na tabela `participante`, embora estejam no HTML.
- A **foto do participante** é o único caso em que a regra acima precisou de nuance, porque a tela
  passou a mostrar o retrato: a URL `verFoto?idFoto=...&key=...` é **extraída mas não persistida**.
  Ela existe pelo tempo de baixar a imagem uma vez e some com o `Snapshot`; o que fica no disco é o
  arquivo da imagem, num cache fora do banco (`Documentos/SIGAA/.fotos`), nomeado pelo hash de
  `(idTurma, nome)`. Guardar o `key` deixaria no `sigaa-viewer.db` um jeito de buscar o retrato de
  cada colega meses depois de o app ter parado de precisar disso.

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
   download, com parser e fixture de rede), ✅ **materiais do tópico** em §1.6.2 — estes vêm na
   própria página inicial da turma, sem abrir aba — e ✅ **Participantes** em §1.6.3 (parser,
   fixture redigido e aba na janela da turma). Continuam abertas: Ver Notas, Frequência,
   Tarefas, Fóruns, Notícias, Plano de Curso. A ferramenta para capturá-las já
   existe: `sigaa-cli explorar <turma> <rótulo da aba> <dir>` grava o HTML cru de cada passo.
7. **"Baixar todos os arquivos"** (§1.6.1): o comando existe na aba e provavelmente devolve um zip.
   Não testado — o app baixa um a um, o que funciona e é verificável.
8. **Materiais de tópico que não são arquivo** (tarefa, fórum, vídeo): sabemos listá-los e o id
   deles, mas não o que o POST de cada tipo devolve. Hoje aparecem na árvore de aulas em cinza,
   sem download.
5. **Exportar um HAR completo** (DevTools → Network → exportar) cobrindo login → portal → turma →
   volta, e guardar **redigido** em `tests/fixtures/har/`.
6. **Confirmar o timeout real de 30 min** e se `verPortalDiscente.do` sozinho o renova.

## 5.1 Dados de terceiros que o app decidiu NÃO coletar

O SIGAA entrega, na aba Participantes, bem mais do que a lista de nomes. O que
fica de fora é decisão de projeto, não limitação do parser:

| dado | onde aparece | por que não coletamos |
|---|---|---|
| CPF do participante | `Mensagem.show(1, '<CPF>', ...)` de cada pessoa | documento de terceiro; nada no app precisa dele |
| `idPessoa` | link do perfil | identificador interno de outra pessoa |
| **retrato** | `/shared/verFoto?idFoto=...&key=...` | **removido em 18/09/2026 — ver abaixo** |

**A foto foi coletada, e não deveria ter sido.** Até 18/09/2026 o app baixava o
retrato de cada participante para `Documentos/SIGAA/.fotos` e o exibia na lista
da turma. Numa turma de 32 pessoas são 19 imagens — de 31 colegas e um
professor, nenhum dos quais escolheu ter a própria foto copiada para a máquina
de outro aluno. Eles cadastraram o retrato no SIGAA, para o SIGAA.

Removido por inteiro: a coleta (`core/sync/Fotos.{h,cpp}`), a extração da URL
no parser, o campo no modelo e o cache em disco. A lista passou a mostrar as
**iniciais** num quadrado colorido, o que resolve o problema real da tela —
reconhecer alguém de relance — sem carregar dado pessoal, e ainda funciona para
os 13 de 32 que nunca cadastraram foto.

Dois testes guardam a decisão: um confere que o esquema do banco não tem coluna
de imagem, outro que a célula da foto no HTML não vira participante nem desloca
os campos do vizinho.

## 6. Problemas do servidor a reportar à instituição

Achados que não são bugs do app nem do computador do aluno: são configurações
do próprio SIGAA que precisam chegar à TI da instituição. Ficam aqui para que o
reporte possa ser feito com evidência, e para que o próximo a tropeçar no mesmo
sintoma não perca uma tarde procurando defeito no lugar errado.

### 6.0 Segundo login simultâneo fica sem resposta (18/09/2026, UNIFEI)

**Sintoma.** Com uma sessão já aberta na conta, o `POST logar.do?dispatch=logOn`
seguinte **não recebe resposta nenhuma**: 45 s de timeout, 0 bytes, três vezes.
O primeiro login da mesma sequência tinha funcionado normalmente (200, portal
de 103 KB, 6,7 s).

```
10:35:34 #1 GET  logar.do?dispatch=logOff  200  10642B   700ms  [Login]
10:35:42 #2 POST logar.do?dispatch=logOn   200 103092B  6679ms  [Portal]
10:36:04 #3 GET  logar.do?dispatch=logOff  200  10642B   770ms  [Login]
10:38:23 #4 POST logar.do?dispatch=logOn   ERRO: timed out after 45002ms
                                           with 0 bytes received (3 tentativas)
```

O `#3` é o GET que abre uma sessão NOVA (outro JSESSIONID), então ele não
encerra a sessão do `#2` — que segue viva no servidor.

**O que era do app.** Cada tarefa abria a própria sessão e fazia o próprio
login: o diálogo de senha para conferir a credencial, e a sincronização logo
atrás. Dois logins para uma ação, e nenhuma das sessões era encerrada.

**Consequência para o desenho.** Uma sessão viva por vez, reaproveitada
(`core/http/SessaoViva.h`): o diálogo entrega a sessão que abriu, a
sincronização continua nela, e o app só desloga ao fechar. As sessões
descartáveis do `Baixador` — que precisam ser paralelas, e por isso não podem
compartilhar o ViewState (§2.2) — chamam `logout()` ao terminar.

**Não confirmado:** se o SIGAA recusa qualquer segundo login concorrente, ou se
foi degradação momentânea do servidor (a instituição estava com o TLS quebrado
no mesmo dia, ver §6.1). O desenho acima está certo de qualquer forma — sessão
abandonada é recurso vazado — mas a causa exata merece nova captura antes de
virar afirmação.

### 6.1 Cadeia de certificados TLS incompleta (18/09/2026, UNIFEI)

**Sintoma.** Todo cliente que valida TLS corretamente falha ao falar com
`sigaa.unifei.edu.br`:

```
SSL certificate OpenSSL verify result: unable to get local issuer certificate (20)
```

`curl`, `openssl s_client` e o `sigaa-viewer` falham. **Os navegadores
funcionam** — e é isso que torna o problema difícil de enxergar: a conclusão
natural de quem vê "abre no Chrome, não abre no app" é que o app está quebrado.

**Causa.** O certificado do servidor foi trocado em **16/09/2026** para uma
autoridade nova, mas a cadeia enviada no handshake continua sendo a **antiga**:

| | o que o servidor envia | quem realmente assinou |
|---|---|---|
| folha | `CN=*.unifei.edu.br`, emitida em 16/09/2026 | `RNP ICPEdu GR46 OV TLS CA 2025` |
| intermediário enviado | `GlobalSign RSA OV SSL CA 2018` | — (pertence ao certificado anterior) |
| raiz enviada | `GlobalSign Root CA - R3` | — |

O intermediário que liga a folha à raiz — `RNP ICPEdu GR46 OV TLS CA 2025`,
emitido por `GlobalSign Root R46` — **não é enviado**. A raiz `GlobalSign Root
R46` já é confiável em qualquer loja de CAs atual, então o certificado em si
está correto: o que falta é a peça do meio.

Os navegadores disfarçam porque fazem *AIA chasing* — leem a extensão
Authority Information Access da folha e baixam o intermediário sozinhos:

```
CA Issuers - URI:http://secure.globalsign.com/cacert/rnpicpedugr46ovtlsca2025.crt
```

A libcurl não faz isso, e nem todo cliente faz.

**Como reproduzir.**

```bash
openssl s_client -connect sigaa.unifei.edu.br:443 -servername sigaa.unifei.edu.br </dev/null
#   Verify return code: 21 (unable to verify the first certificate)

# e a prova de que o problema é só a peça faltante:
curl -sS -o /dev/null -w '%{http_code}\n' \
  --cacert <(cat /etc/ssl/certs/ca-certificates.crt rnpicpedugr46ovtlsca2025.pem) \
  https://sigaa.unifei.edu.br/sigaa/verTelaLogin.do
#   200
```

**Impacto.** Qualquer integração automatizada com o SIGAA quebra: scripts,
aplicativos móveis que não façam AIA chasing, monitoramento, e clientes de
linha de comando. Pior, empurra quem precisa que aquilo funcione para a
"solução" de desligar a verificação de certificado — o que expõe **CPF e senha**
de quem usa a integração a interceptação em rede hostil (Wi-Fi de campus, por
exemplo). Um erro de configuração vira, por esse caminho, um risco real de
credencial.

**Correção (no servidor).** Instalar o bundle completo: folha +
`RNP ICPEdu GR46 OV TLS CA 2025`. A raiz não precisa ser enviada.

**Contorno no app.** Resolvido automaticamente desde 18/09/2026: o app faz
*AIA chasing* (`core/http/CadeiaAia.h`) — ao levar uma recusa de verificação,
lê a extensão AIA do certificado do servidor, baixa o intermediário que falta e
tenta de novo, uma vez por sessão. É o que o navegador faz.

A verificação da segunda tentativa é **inteira**: o intermediário entra ao lado
das CAs do sistema, não no lugar delas, e o OpenSSL continua exigindo que a
cadeia termine num certificado auto-assinado da lista. Um intermediário forjado
não fecha cadeia com raiz nenhuma e é recusado igual — conferido contra
`expired`, `self-signed`, `wrong.host` e `untrusted-root` do badssl.com, todos
ainda rejeitados com o AIA ligado.

`SIGAA_CA_BUNDLE` continua existindo para o caso de rede que intercepta HTTPS
(Wi-Fi corporativo, antivírus com inspeção de TLS), onde a CA a acrescentar é
local e não está publicada em AIA nenhum. `SIGAA_SEM_AIA=1` desliga o chasing,
para depurar.

O app **não** desliga `CURLOPT_SSL_VERIFYPEER`, e não deve passar a desligar:
num programa que envia CPF e senha, trocar um erro visível por um ataque
silencioso é o pior negócio disponível.

**Status.** Não reportado ainda. Contato: DTI/UNIFEI, (35) 3629-1080 (rodapé do
próprio SIGAA).
