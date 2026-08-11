# SIGAA-Desktop-Viewer

Cliente desktop multiplataforma para o **SIGAA da UNIFEI** — agrega prazos,
atualizações das turmas e calendário de provas num só lugar, com notificações
nativas.

### [Leia a Wiki](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki)

**Quer contribuir?** A wiki tem tudo que você precisa para começar:
[Arquitetura](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Arquitetura) ·
[Referência da API](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Referencia-da-API) ·
[Configuração e Build](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Configuracao-e-Build) ·
[Protocolo SIGAA](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Protocolo-SIGAA) ·
[Guia de contribuição](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Contribuindo) ·
[Roadmap](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Roadmap)

Boa porta de entrada: o [Roadmap](https://github.com/matheus-fsc/Sigaa-Desktop-Viewer/wiki/Roadmap)
lista parsers que ainda não existem (Notas, Faltas), trabalho bem delimitado e
que não exige conhecer Qt. A interface é opcional no build.

> **Escopo atual: UNIFEI.** O SIGAA é um sistema compartilhado por dezenas de
> universidades federais, mas cada instituição opera uma instância própria com
> URLs, rotas JSF e, em muitos casos, customizações de layout diferentes. Toda a
> engenharia reversa documentada em `docs/RECON.md` foi feita sobre o front-end
> da UNIFEI (`sigaa.unifei.edu.br`). Outras instâncias **provavelmente** vão
> precisar de ajustes nos endpoints, nos nomes de formulário e nos seletores
> HTML — veja *Adaptando para outra universidade* abaixo.
>
> O **endereço já é escolhível** na primeira tela (e por `--url` no CLI), e o
> catálogo embutido tem só a UNIFEI, marcada como verificada. Isso é de
> propósito: uma lista de vinte universidades onde dezenove falham no primeiro
> login é pior do que uma lista de uma e um campo de endereço, porque no
> primeiro caso o usuário culpa a própria senha — e tenta de novo, que é o
> caminho para o SIGAA bloquear a conta dele.

**Estado: Fase 3 (interface).** O núcleo headless e o modo agendado estão prontos;
a janela Qt mostra prazos, provas e atualizações e dispara sync em segundo plano.

- [`docs/PLANO.md`](docs/PLANO.md) — plano técnico e roadmap
- [`docs/RECON.md`](docs/RECON.md) — engenharia reversa do SIGAA (leitura obrigatória antes de mexer no scraper)

---

## Adaptando para outra universidade

Comece pelo mais barato: **aponte o app para o seu SIGAA e veja o que quebra.**

```sh
sigaa-cli --url sigaa.suafaculdade.edu.br doctor    # confirma rede/TLS e o alvo
sigaa-cli --url sigaa.suafaculdade.edu.br sync
```

Na interface, o endereço é o primeiro campo da tela de login ("Outra instituição
→ informar endereço"), e fica guardado para as próximas aberturas. O host também
entra na chave do cofre de credenciais, então cada instituição tem a sua senha
guardada em separado — trocar de faculdade não faz o app tentar a senha de uma
na outra, o que gastaria tentativa e aproxima o bloqueio da conta.

Se o login funcionar mas a leitura falhar (é o desfecho provável), aí sim vale o
recon:

1. **Faça o recon da sua instância.** Abra o DevTools, logue no seu SIGAA e
   compare os requests com o que está documentado em `docs/RECON.md`. As
   diferenças costumam estar nos caminhos (`/sigaa/verPortalDiscente.do` vs.
   `/sigaa/portais/discente/discente.jsf`, por exemplo), nos nomes dos campos
   JSF e, às vezes, nos seletores CSS do HTML de resposta.

2. **Ajuste os arquivos certos.** A arquitetura separa deliberadamente *o que
   se pede* de *como se interpreta a resposta*:
   - `src/core/config/Instituicao` — o catálogo. Acrescentar a sua é uma linha,
     depois de conferir contra o site; marque `verificada=true` só aí, e anote
     em `docs/RECON.md` qual versão do SIGAA você testou.
   - `src/core/http/SigaaSession` — rotas de login e navegação.
   - `src/core/jsf/JsfForm` — campos e `javax.faces.ViewState`.
   - `src/core/parse/PortalParser` e `TurmaParser` — seletores CSS/XPath.
   - As fixtures em `tests/fixtures/` precisarão de HTML da sua instância
     (redatado com `tools/redact.py`).

3. **Use um agente de IA se quiser ir rápido.** O repositório inclui um grafo
   de conhecimento gerado pelo [graphify](https://github.com/nicholasgasior/graphify)
   (`graphify-out/`). Aponte qualquer agente de código (Copilot, Claude, Gemini,
   etc.) para esse grafo e ele consegue navegar a arquitetura, entender as
   dependências entre módulos e propor as mudanças necessárias com contexto real
   — sem precisar ler arquivo por arquivo.

Contribuições que generalizem as rotas (e.g. um `SigaaProfile` por instituição)
são muito bem-vindas. Abra uma issue descrevendo quais endpoints diferem na sua
universidade e podemos desenhar juntos a abstração.

---

## Build

Requisitos: **CMake ≥ 3.24**, um compilador C++20 (MSVC 2022+, GCC 12+, Clang 15+) e **vcpkg**.

```sh
# vcpkg, se ainda não tiver
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh        # Windows: .\vcpkg\bootstrap-vcpkg.bat
export VCPKG_ROOT=$PWD/vcpkg      # Windows: setx VCPKG_ROOT "%CD%\vcpkg"

# configurar + compilar (troque `windows` por `linux` / `macos`)
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

As dependências vêm do manifesto `vcpkg.json` — não é preciso instalar nada à mão.

### Onde fica o release

**`dist/`.** Um comando, um caminho:

```powershell
pwsh -File tools/empacotar.ps1
# -> dist/SIGAA-Desktop-Viewer-v<versão>-windows-x64/  (e o .zip ao lado)
```

O script compila em Release, **roda os testes antes de empacotar** e monta a
pasta com os dois executáveis, as DLLs do Qt e do vcpkg, os plugins que o
`windeployqt` escolheu, o README e o `.env.example`. É o que o usuário final
recebe: descompactar e abrir o `sigaa-ui.exe`, sem Qt instalado.

Nada dentro de `build/` é o release, e vale saber por quê antes de procurar lá:

| Caminho | O que é |
|---|---|
| `build/<preset>/Release/` | binários otimizados, **sem** as DLLs de terceiros arrumadas para distribuir |
| `build/<preset>/Debug/` | binários de depuração; exigem `Qt6*d.dll` e não rodam fora da máquina de build |
| `build/<preset>/*.dir/` | andaime do gerador do Visual Studio — `ALL_BUILD`, `ZERO_CHECK`, `Continuous`, `Nightly`, `RUN_TESTS`. Cada um com quatro subpastas **vazias** (uma por configuração). Não é código, não é saída, é ruído do MSBuild |

Duas coisas que o script se recusa a fazer, e o motivo: empacotar com teste
falhando, e copiar `sigaa-viewer.db` ou `relatorio.*`. Esses dois aparecem
dentro do diretório de build sempre que alguém roda o app pela IDE — e contêm
os dados reais de quem compilou (ver *Dados pessoais* abaixo). Se algum escapar
para a pasta montada, o script apaga o pacote e falha alto.

---

## Arquitetura em uma frase

`src/core/` **não conhece nenhum framework de UI.** É C++20 puro + libcurl + lexbor + SQLite, então
roda em CI sem display e é testável contra fixtures. A UI é Qt, mas o Qt só aparece
em `src/ui/` — quatro arquivos. Trocar de toolkit custa esses quatro, e nada mais.

```
src/core/parse/Html          wrapper sobre lexbor (o único arquivo que inclui lexbor)
src/core/jsf/JsfForm         emula jsfcljs() — monta o POST que o browser mandaria
src/core/http/SigaaSession   cookies em memória, fila serializada, retry, login
src/core/parse/PortalParser  turmas, atividades, atualizações
src/core/store/Database      SQLite: último snapshot conhecido
src/core/sync/DiffEngine     compara snapshots → eventos
src/core/report/HtmlReport   relatório autocontido
src/core/servico/Servico     um ciclo inteiro: login → coleta → diff → relatório
src/core/config/DotEnv       .env, compartilhado por CLI e UI
src/platform/Notify          notificação nativa (Shell_NotifyIcon/notify-send/osascript)
src/app/                     sigaa-cli
src/ui/                      sigaa-ui (Qt Widgets) — a única pasta que conhece Qt
```

### `core/servico` existe para o CLI e a UI não divergirem

A sequência login → coleta → diff → gravação → relatório → `.ics` morava dentro do
`cmdSync()` do CLI. Com duas interfaces, isso viraria duas cópias da mesma
sequência apodrecendo em ritmos diferentes — e a regra que mais importa (*coleta
suspeita não grava*) ficaria escrita duas vezes.

O serviço **não** pergunta credenciais (prompt é decisão de interface; a UI vai ler
do keychain) e **não** notifica (`plat::notificar` é código de plataforma, e `core/`
não conhece plataforma). Ele devolve o aviso pronto e quem chama decide se aquilo
vira toast do agendador ou balão de bandeja da janela.

### Duas decisões do DiffEngine que não são óbvias

**Primeira execução não é novidade.** Sem isso, o primeiro `sync` dispararia
uma notificação por item já existente — dezenas de alertas que ensinam o
usuário a ignorá-los.

**Coleta vazia é suspeita, não "nada mudou".** Se antes havia turmas e agora
vieram zero, o cenário provável é o parser ter quebrado (o SIGAA mudou o HTML),
não o aluno ter se desmatriculado. Silenciar isso é o **pior modo de falha do
app**: o usuário confia que não há prazos e perde a entrega. Nesse caso o
`sync` emite `ColetaSuspeita`, **não grava** (para não destruir a base de
comparação boa) e sai com código 1.

### Invariantes do SigaaSession

Não são preferências — são consequências do recon, e quebrá-las tem custo real:

| Invariante | Por quê |
|---|---|
| Uma requisição por vez | O SIGAA invalida a view ao detectar navegação concorrente |
| Intervalo mínimo (1,5 s por padrão) | Não parecer ataque; é conta de aluno, não robô |
| Limite rígido de tentativas de login | Senha errada em loop **bloqueia a conta do usuário** |
| `JSESSIONID` só em memória | `CURLOPT_COOKIEFILE ""` — o cookie nunca toca o disco |

### Convenção de testes

Nomes de `TEST_CASE` em **ASCII puro**. O CTest repassa o nome como filtro ao
binário do Catch2 e, no Windows, o codepage do console corrompe acentos — o
teste some com "No tests ran" em vez de falhar, que é pior que falhar.

---

## sigaa-cli

Hoje opera **offline**, sobre HTML salvo. Isso é proposital: valida o motor JSF contra páginas
reais antes de existir código de rede, que é a parte cara de depurar.

```sh
sigaa-cli forms portal.html                  # lista os <form> e seus campos
sigaa-cli links portal.html                  # lista os "cliques" jsfcljs
sigaa-cli post  portal.html ADMINISTRA       # monta o corpo do POST daquele link
```

O `post` deve reproduzir exatamente o corpo capturado do browser em `docs/RECON.md` §1.3.

O `sync` fala com o servidor:

```sh
sigaa-cli doctor                      # libcurl, backend TLS e backend de notificação
sigaa-cli doctor https://exemplo.com  # + teste de conectividade
sigaa-cli doctor --notificar          # dispara uma notificação de teste
sigaa-cli sync [saida.html]           # login, portal, relatório + .ics
sigaa-cli sync --turmas               # + visita cada turma (provas e tópicos)
sigaa-cli sync --quiet                # modo agendado (ver abaixo)
sigaa-cli turma <arquivo.html>        # inspeciona uma página de turma virtual
```

`--turmas` custa 1 + 2 requisições por turma (entrar e voltar). Com 7 turmas e
o intervalo mínimo de 1,5 s, o ciclo leva ~25 s. Sem ele, o portal sozinho já
responde "o que vence essa semana".

O `.ics` sai ao lado do relatório e importa em Google Calendar / Outlook /
calendário do celular. Avaliações vindas de heurística sobre o título do tópico
são marcadas como *inferido — confirme com o professor*; as do painel formal do
professor, não.

`SIGAA_DUMP=arquivo.html` grava o HTML cru da rede (portal e primeira turma) —
é a forma **correta** de gerar fixture de parser. Ctrl+S no navegador salva o
DOM pós-JavaScript, que é 2,7× maior e tem markup que o servidor nunca envia.

## Sincronização automática

**Nada deste app roda com o programa fechado.** Não há serviço, nem tarefa
agendada instalada, nem processo de fundo. Fechou a janela, acabou.

Com a janela aberta e o botão **Automático** ligado (padrão):

| Ciclo | Quando | Custo | O que traz |
|---|---|---|---|
| portal | a cada 20 min | 1 requisição | atividades e avisos |
| turmas | a cada 6 h | ~22 requisições | provas, aulas e material publicado |

O ciclo de turmas **também baixa o material que falta**, para
`Documentos/SIGAA/<turma>/`. É o que faz a janela da turma abrir offline depois:
quando você entra nela, tudo já está no disco. O cache é por `idArquivo`, então
a primeira execução baixa o semestre inteiro e as seguintes baixam só o PDF da
aula da semana — e turma sem nada pendente não custa requisição nenhuma, porque
a verificação é local.

No CLI isso é opt-in (`--materiais <dir>`): uma tarefa agendada que enche o
disco sem perguntar é diferente de um app que você abriu para ver a turma.

O ciclo de turmas entra em cada turma e, dentro dela, abre a aba **Arquivos**.
É o que faz o app perceber que o professor subiu o PDF da aula de hoje — a
novidade mais frequente do semestre, e a única que o portal nunca mostra: as
"Atualizações das Turmas" trazem um texto solto ("Novo Arquivo: …"), sem id,
sem título confiável e sem o material. O evento aparece como *material novo* na
lista de novidades e alimenta a contagem de arquivos na aba Hoje. Custa uma
requisição por turma; `--sem-arquivos` desliga no CLI.

As frequências são diferentes de propósito. Prazo de atividade muda a qualquer
hora e sai do portal com **uma** requisição; entrar nas 7 turmas custa ~22
requisições e meio minuto, e prova quase nunca muda. Rodar o ciclo caro a cada
20 min seriam centenas de visitas diárias ao servidor da universidade para
descobrir quase sempre a mesma coisa (ver *Etiqueta*).

Isso só é seguro porque o banco faz *upsert*: um ciclo só-portal **não apaga**
as provas, as aulas nem os arquivos já conhecidos, e o DiffEngine ignora essas
três categorias quando a coleta veio sem turmas. Sem isso a aba Hoje ficaria
vazia a cada 20 minutos, e a primeira coleta completa seguinte anunciaria o
acervo inteiro de cada turma como "material novo". Travado em
`tests/database_test.cpp` e `tests/diff_engine_test.cpp`.

**O preço dessa escolha, explícito:** com o app fechado você não recebe aviso
nenhum. Se ficar dias sem abrir, um prazo pode vencer sem ninguém te cutucar. O
`.ics` existe justamente para cobrir esse buraco — as datas ficam no calendário
do celular, que você olha mesmo sem abrir isto aqui.

### Rodar em segundo plano (opcional, desligado)

`tools/agendar.ps1 -Instalar` registra duas tarefas no Agendador do Windows que
sincronizam com o app fechado. **Não vem instalado**, e tem um efeito colateral
conhecido: `sigaa-cli` é um binário de console, então cada execução pisca uma
janela preta na tela. `-Hidden` no agendador esconde a *tarefa na lista*, não a
janela; e quando o nosso código roda para escondê-la, ela já apareceu. Consertar
exigiria um segundo binário compilado no subsistema Windows (`/SUBSYSTEM:WINDOWS`),
que é o que se faz de verdade — só não foi feito porque a decisão foi não rodar
com o app fechado.

`-Status`, `-Agora` e `-Remover` completam o script.

### `--quiet`

O modo que o agendador usa. Ele **nunca pergunta nada** — um prompt de senha
numa janela oculta travaria a tarefa para sempre. Se as credenciais não
estiverem no ambiente ou no `.env`, ele falha alto e registra no log.

Também esconde o console, mas só quando o processo é dono dele: rodando
`--quiet` dentro de um terminal aberto, a janela é do seu shell, e escondê-la
faria o terminal sumir do nada.

Log em `sigaa-viewer.log` (`--log <arquivo>` muda o destino).

### Notificação nativa

Ligada por padrão em `--quiet`, desligada no modo interativo — quando você está
olhando o terminal, o toast é redundante. Force com `--notificar` ou
`--sem-notificar`.

Backends: `Shell_NotifyIcon` no Windows, `notify-send` no Linux, `osascript` no
macOS. `sigaa-cli doctor --notificar` testa o caminho inteiro.

Duas decisões que importam mais que a implementação:

- **No máximo um aviso por ciclo.** Um toast por evento parece mais informativo,
  mas oito numa manhã ensinam a fechar tudo sem ler — e aí o aviso que importava
  se perde junto. O ciclo agrega tudo num aviso só, com os mais urgentes no topo
  (prova remarcada > prazo alterado > prova nova > atividade nova > ...).
- **Silêncio é resposta válida**, com uma exceção: coleta suspeita avisa. Aí o
  perigo é o inverso — o usuário concluir que não há prazos porque o parser
  quebrou.

Limitação honesta: um processo que roda e morre não mantém a notificação viva na
Central de Ações do Windows — o balão morre junto com o ícone de bandeja. O
registro durável são o relatório e o `.ics`, que ficam no disco. Notificação
persistente exige processo residente, e isso é assunto da Fase 3.

---

## Interface gráfica

**Qt 6 Widgets**, em `src/ui/`. Qt não vem do vcpkg — compilar do fonte leva horas.
O caminho desatendido, sem instalador gráfico nem conta Qt:

```sh
pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:/Qt
```

Depois aponte o CMake para lá. O lugar certo desse caminho de máquina é o
`CMakeUserPresets.json`, que está no `.gitignore`:

```jsonc
// CMakeUserPresets.json
{ "version": 6,
  "configurePresets": [{
    "name": "windows-qt", "inherits": "windows",
    "binaryDir": "${sourceDir}/build/windows",
    "cacheVariables": { "CMAKE_PREFIX_PATH": "C:/Qt/6.8.3/msvc2022_64" }
  }] }
```

```sh
cmake --preset windows-qt && cmake --build build/windows --config Debug
```

**A UI é opcional.** Sem Qt, o CMake avisa e segue: `sigaa-cli` e os testes
continuam compilando. Quem só quer mexer no parser não precisa baixar 1,5 GB.
`-DSIGAA_UI=OFF` silencia o aviso.

### O que a janela faz

Quatro abas, na ordem das perguntas que o aluno faz: **Hoje**, **Provas**,
**Turmas** e **Atualizações**.

**Hoje** é a tela inicial: em cima, as aulas de hoje e de amanhã — o tópico que
o professor registrou, a turma e quantos arquivos ele pendurou naquela aula;
embaixo, a lista de prazos, com "em 2 dias" em laranja e atrasado em vermelho.
Duplo clique numa aula entra na turma.

Os prazos desceram para o rodapé de propósito. Prazo é o que *vence*; aula é o
que *acontece hoje* — e era justamente essa a pergunta que o app não respondia,
apesar de já ter o dado no banco. O divisor é arrastável, então quem prefere a
lista de prazos grande a puxa para cima.

Amanhã aparece junto de hoje porque quem abre o app às 22h está se preparando
para o dia seguinte: uma tela que diz "nenhuma aula hoje" àquela hora está certa
e é inútil. E "ainda não coletei as aulas" é uma mensagem diferente de "não há
aula hoje" — a primeira é a tela admitindo que não sabe, e confundir as duas
faria o aluno concluir que está livre.

Duas decisões que valem mais que o layout:

**A janela abre com dados, sempre.** O último snapshot vem do SQLite antes de
qualquer requisição. Sem internet, ou com o SIGAA fora do ar, os prazos já
conhecidos aparecem. Sincronizar é ação explícita, nunca pré-requisito para ver
a tela — e o banco é o mesmo do agendador, então a janela já abre sabendo o que
o sync das 07:30 encontrou.

**Depois de sincronizar, a tela recarrega do banco — não do que a coleta trouxe.**
`Atualizar` é só-portal e volta sem avaliação nenhuma; pintar a tela com esse
snapshot apagaria as provas conhecidas e diria ao aluno que ele não tem prova
marcada. É o mesmo modo de falha que o banco (upsert, nunca `DELETE`) e o
DiffEngine já evitam, e a tela obedece à mesma regra. Travado em
`tests/database_test.cpp`.

A coluna **Origem** da aba Provas distingue o que veio do painel do professor do
que foi deduzido por regex sobre título de tópico — este último aparece como
*inferido — confirme*. As duas fontes divergem por professor (`docs/RECON.md` §1.6),
e esconder a diferença faria o aluno estudar para a data errada sem desconfiar.

### Provas: lista e calendário

A aba é um painel, não uma tabela. Três cartões no topo (próxima prova, quantas
nos próximos 30 dias, quantas ainda dependem de confirmação), calendário do mês
à esquerda e a lista completa à direita.

Existem as duas vistas porque são duas perguntas diferentes. A lista responde
"qual é a próxima?". O calendário responde "como está a minha semana?" — três
provas em quatro dias só aparecem como aglomeração quando se vê o mês; na lista
são três linhas iguais a quaisquer outras.

No calendário, **ponto cheio** é prova cadastrada pelo professor e **ponto
vazado** é data deduzida de tópico de aula. A mesma distinção da coluna Origem,
sobrevivendo até o desenho. A cor é a urgência: cinza para o que já passou,
laranja para os próximos 7 dias.

Clicar num dia filtra a lista; clicar de novo desfaz. Clicar num dia **sem** prova
não esvazia a tabela — diz "18 de agosto não tem prova" e mantém tudo à vista,
porque uma tabela em branco depois de um clique parece defeito, não resposta.

### Turmas: entrar e baixar o material

A aba **Turmas** lista as disciplinas com horário, local e período. Duplo clique
(ou *Entrar na turma*) abre a turma como no SIGAA web, em duas abas.

**A janela abre offline, pintada.** As aulas e os arquivos vêm do banco, onde o
último ciclo com turmas já os depositou — nada de rede, nada de espera. Antes
ela fazia login e três requisições toda vez que era aberta para mostrar dados
que já estavam guardados; o "Entrando na turma…" era o app buscando o que já
tinha. A sessão com o SIGAA só nasce quando alguém pede um arquivo que não está
no disco, ou clica em **Atualizar** — que relê a turma na hora, para o caso de o
professor ter publicado algo nos últimos minutos.

Sem internet, a janela continua abrindo e o material baixado continua abrindo
junto. Se o *Atualizar* falhar, ele diz o que falhou e mantém na tela o que está
guardado, em vez de esvaziar a janela.

As duas abas:

**Aulas** é a linha do tempo que o professor registrou — uma árvore de tópicos,
cada um com o intervalo de datas e os materiais pendurados nele. É a tela por
onde se entra na Turma Virtual do SIGAA, e é a pergunta que o aluno realmente
tem: uma lista plana de PDFs responde *o que existe*; a árvore responde *o que
caiu na aula do dia 18*. Os arquivos da aba Arquivos entram na aula certa porque
a coluna "Tópico de Aula" casa com o título do tópico (`docs/RECON.md` §1.6.1);
o que não casar com aula nenhuma vai para um grupo no fim, porque sumir seria
pior que desarrumado.

Material em cinza é o que existe na aula mas não se baixa por aqui — tarefa,
fórum, vídeo. Um item só é oferecido para download quando o `id` dele aparece na
aba Arquivos; deduzir pelo ícone seria chute, e daria erro na cara do aluno.

**Arquivos** é a lista completa do que o professor publicou, com download de
verdade para `Documentos/SIGAA/<turma>/`.

O nome do arquivo salvo vem do `Content-Disposition`, não do título da tabela: o
título é texto livre do professor ("Notas de Aula - Aula 01") e não tem extensão,
então salvar com ele daria um arquivo que o Windows não sabe abrir. Arquivo que
já existe ganha sufixo `(2)` em vez de sobrescrever — o SIGAA reaproveita nome
entre semestres.

#### Baixar tudo, e ficar offline

*Abrir* faz a coisa óbvia: se o material já está no seu computador, abre direto,
sem tocar na rede; se não está, baixa antes. Baixar de novo o que a pessoa já
tem produziria um arquivo idêntico e a faria esperar por isso.

*Baixar tudo* pega o material que ainda não está no disco. O que já está não vai
para a rede de novo: um manifesto (`.sigaa-offline.tsv`) mora na pasta da turma
e o ✓ da árvore sai dele. O manifesto fica na pasta, e não no banco, porque a
pasta é o que o aluno mexe — ele copia para o pendrive, renomeia, apaga. Por isso
toda leitura confere se o arquivo ainda existe: "offline ✓" mentiroso faria a
pessoa fechar o app achando que tem o material.

A chave do cache é o `id` do SIGAA, não o nome do arquivo. O nome é texto livre
do professor e muda sem que o conteúdo mude — usá-lo faria cada renomeação
disparar um download inútil. A brecha é o caso oposto: se o professor trocar o
conteúdo mantendo o mesmo id, o app não percebe. Daí o botão **Baixar de novo**,
que ignora o cache para a seleção. Um botão resolve isso; desligar o cache para
todo mundo faria cada abertura puxar a turma inteira de novo.

**Paralelismo, e onde ele para.** O invariante nº 1 do `SigaaSession` continua
valendo: **uma requisição por vez dentro de uma sessão** — o SIGAA guarda a view
no servidor e duas navegações simultâneas no mesmo `JSESSIONID` a invalidam, o
que faria o segundo download chegar como página de erro salva com extensão
`.pdf`. O que dá para paralelizar é a *sessão*: cada canal do
`core/sync/Baixador` tem o próprio login e a própria view. O preço é um login por
canal, então o app usa 1 canal até 2 arquivos, 2 até 6 e no máximo 3 — para dois
PDFs de 200 KB abrir sessão extra é perda pura, e o teto existe porque são logins
simultâneos na conta do aluno.

Três coisas que essa janela faz de propósito:

**Sessão própria com o SIGAA, separada do sync.** Custa um login a mais e remove
uma classe inteira de bug: a navegação do SIGAA é uma máquina de estados no
servidor, e um sync automático disparando no meio de um download invalidaria a
view — o "PDF" chegaria como página de erro.

**Recusa fechar durante um download.** A thread escreve em membros da janela;
deixar a janela morrer antes dela seria use-after-free, e um PDF de 11 MB dá
tempo de sobra para alguém clicar no X.

**"O professor não publicou nada" é diferente de "não consegui ler".** A primeira
é informação, a segunda é bug nosso. Confundir as duas ensinaria o aluno a parar
de procurar o material no site.

O rodapé lista as outras abas que a turma tem no SIGAA (Notas, Frequência,
Tarefas, Fóruns…) e diz que este app ainda não as abre — melhor do que deixar o
aluno concluir que a turma só tem arquivos. O protocolo está em `docs/RECON.md`
§1.6.1 (aba Arquivos) e §1.6.2 (materiais do tópico); as demais abas continuam
por mapear.

Pelo terminal, sem UI:

```sh
sigaa-cli arquivos "PROJETO"              # lista, com o id de cada arquivo
sigaa-cli baixar   "PROJETO" 1538653      # baixa para ./materiais
sigaa-cli explorar "PROJETO" Arquivos rec # recon: grava o HTML cru de cada passo
```

`--url <endereço>` e `--instituicao <id>` valem para qualquer subcomando e podem
vir antes ou depois dele; `SIGAA_URL` no ambiente ou no `.env` faz o mesmo.

### Mexer no visual

O layout está em `src/ui/forms/*.ui`, editável no **Qt Designer** (não precisa
do Qt Creator inteiro):

```sh
C:/Qt/6.8.3/msvc2022_64/bin/designer.exe src/ui/forms/JanelaPrincipal.ui
```

O `AUTOUIC` do CMake roda o `uic` no build e gera `ui_JanelaPrincipal.h`; o
código consome pelo membro `formulario_`. Salvar no Designer e recompilar basta.

O que **não** está no `.ui`, e por quê:

| Fica em código | Motivo |
|---|---|
| Widgets da barra de status | O Designer não sabe povoar uma `QStatusBar` |
| Olho de revelar senha | Vive *dentro* do `QLineEdit` (`addAction`) |
| Texto do cofre, tooltip de período | Depende de `plat::` e das constantes em runtime |
| Atalho de `Atualizar` | `QKeySequence::Refresh` é F5 no Windows e Cmd+R no macOS; o `.ui` cravaria F5 nos três |

Cores e espaçamento vêm de `src/ui/recursos/estilo.qss`, carregado do `.qrc` em
`main.cpp`. **Regra do arquivo: nenhuma cor literal** — tudo sai de
`palette(...)`, senão o app fica com texto branco sobre branco assim que o
Windows entra no tema escuro. As únicas cores literais do app são as semânticas
(`atrasado`, `urgente`, `inferido`, `apagado`) em `Modelos.h`: significam algo
que a paleta do sistema não tem papel para expressar, e ficam num só lugar
porque a tabela de provas e o calendário precisam concordar. Os ícones são SVG monocromáticos repintados em
runtime com a cor do tema (`Icones.cpp`), e `JanelaPrincipal::changeEvent`
retinge quando o tema muda com o app aberto.

O ícone do `.exe` é outra coisa: sai de `src/ui/recursos/app.ico` via
`sigaa-ui.rc`, porque o Explorer lê o executável antes de existir `QApplication`
para abrir o `.qrc`. Regravar com `python tools/gerar_icone.py` — e o desenho
está duplicado em `app.svg`, então mexeu num, mexa no outro.

### O que ainda não tem

Preferências (o intervalo de 20 min é fixo), minimizar para a bandeja ao fechar,
e cofre no macOS. Das abas da Turma Virtual só **Arquivos** está implementada —
Notas, Frequência, Tarefas, Fóruns e Notícias continuam sem parser (a janela da
turma lista os nomes delas para deixar isso explícito). Materiais de tópico que
não são arquivo aparecem na árvore de aulas, mas em cinza: sabemos listá-los,
não abri-los. O download passa inteiro pela memória antes de ir ao disco, o que
serve para PDF de aula mas incharia com um vídeo de 1 GB — e com N canais são N
arquivos na memória ao mesmo tempo, que é mais uma razão para o teto ser 3.
Fora a UNIFEI, nenhuma instituição foi testada contra o site real. A aba Hoje
depende de um ciclo com turmas ter rodado ao menos uma vez: antes disso ela diz
que não sabe, em vez de afirmar que não há aula. O ícone da bandeja já existe e mostra o aviso agregado quando
a janela não está em foco. As strings estão fixas em português, sem `tr()` nem
`.ts` — o app tem um público de uma universidade brasileira e traduzir custaria
tocar cada linha da UI para nada.

---

### Credenciais

Precedência: **variável de ambiente → cofre do SO → `.env` → prompt**.

```sh
sigaa-cli login     # guarda no cofre (valida no SIGAA antes de gravar)
sigaa-cli logout    # remove
sigaa-cli doctor    # mostra o cofre e de onde as credenciais estão vindo
```

Na interface, o mesmo pela tela de entrada e pelo menu **Conta**.

#### Até onde o cofre protege

Backends: Gerenciador de Credenciais (Windows, DPAPI), libsecret via
`secret-tool` (Linux). No macOS **não está implementado** — o caminho por linha
de comando (`security -w <senha>`) colocaria a senha em `argv`, e o caminho certo
(Security.framework) não foi escrito ainda; o app avisa e cai para o `.env`.

**Protege** a senha de vazar em arquivo: backup, pasta sincronizada em nuvem,
`git add -A` distraído, outra conta de usuário na mesma máquina. É contra isso
que o `.env` é frágil.

**Não protege** contra programa malicioso rodando na *sua* conta — ele pede a
decifragem ao sistema exatamente como o app pede. Não há truque que evite isso:
qualquer chave que o app use sozinho, sem você digitar nada, outro programa seu
também usa. Cifrar com chave embutida no binário seria pior, porque a chave
viajaria junto com o `.exe`. O remédio para esse cenário é senha-mestra a cada
abertura, e a troca foi feita de propósito.

O CPF vai **dentro** do blob cifrado, não no campo de usuário: `cmdkey /list`
mostra o usuário em claro, e CPF é dado pessoal (`docs/RECON.md` §4).

#### Nunca por argumento

**A senha nunca é aceita por argumento de linha de comando.** `argv` fica no
histórico do shell e é legível por qualquer processo (`ps`, `Get-Process`,
`/proc/<pid>/cmdline`). O prompt lê sem eco (RAII restaura o terminal mesmo se
uma exceção subir), e a senha é zerada da memória após o login. Pelo mesmo
motivo o backend Linux escreve o segredo no **stdin** do `secret-tool`.

`sigaa-cli login` e a tela de entrada **verificam no SIGAA antes de gravar**.
Guardar senha errada não fica parado: o ciclo automático a tentaria de novo e de
novo, e o SIGAA **bloqueia a conta** depois de algumas tentativas — o usuário
descobriria o bloqueio sem nenhuma pista de que veio daqui. O login tem limite
de 3 tentativas pelo mesmo motivo.

Um `.env` continua aceito (legado, CI). Está no `.gitignore`, e a UI oferece
migrá-lo para o cofre na primeira abertura.

---

## ⚠️ Dados pessoais nos fixtures

Todo HTML do portal contém, em texto puro: **nome completo, CPF, matrícula, e-mail, `idusuario`,
`JSESSIONID`** e o `key` da foto de perfil (detalhes em `docs/RECON.md` §4).

**Nunca commite HTML bruto do SIGAA.** O `.gitignore` bloqueia `tests/fixtures/raw/` e `*.har`.

```sh
python tools/redact.py bruto.html -o tests/fixtures/portal.html --name "SEU NOME COMPLETO"
python tools/redact.py --check tests/fixtures/*.html      # sai 1 se sobrou PII
```

A redação é **idempotente** — reaplicar não altera nada —, o que é exatamente o que permite ao
`--check` funcionar como hook de pre-commit.

---

## Etiqueta com o servidor

São os seus próprios dados, mas o cliente precisa se comportar:

- poll de 15–30 min, **nunca** abaixo de 5
- **uma requisição por vez** — o SIGAA invalida a view se detectar navegação concorrente
- backoff exponencial com jitter; respeitar 429/503 e página de manutenção
- `User-Agent` identificável
- **limite rígido de tentativas de login** — senha errada em loop bloqueia a conta
- credenciais só no keychain do SO; nunca em disco ou log
