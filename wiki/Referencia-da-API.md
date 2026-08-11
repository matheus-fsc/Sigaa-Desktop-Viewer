# Referência da API do core

Esta página documenta o que `src/core/` e `src/platform/` expõem, para quem quer
reaproveitar as peças em vez de reescrevê-las. Tudo aqui é livre de Qt e
compila no alvo `sigaa_core`, usado tanto pela CLI quanto pela interface.

Os includes são relativos à raiz de `src/`, como no projeto:

```cpp
#include "core/http/SigaaSession.h"
#include "core/parse/Html.h"
```

Para ligar seu código ao core no CMake:

```cmake
target_link_libraries(seu_alvo PRIVATE sigaa_core sigaa_platform)
```

## Índice

| Módulo | Header | Para quê |
| --- | --- | --- |
| [model](#model-o-vocabulario) | `core/model/Models.h` | Structs de domínio e `Snapshot` |
| [http](#http-sessao-com-o-sigaa) | `core/http/SigaaSession.h` | Login, GET/POST, detecção de página |
| [jsf](#jsf-formularios-e-comandos) | `core/jsf/JsfForm.h` | ViewState, corpo do POST, comandos por rótulo |
| [parse](#parse-html-e-extratores) | `core/parse/Html.h`, `PortalParser.h`, `TurmaParser.h` | Consulta CSS e extração de dados |
| [sync](#sync-coleta-diff-e-download) | `core/sync/*.h` | Crawler, DiffEngine, SessaoTurma, Baixador |
| [store](#store-persistencia) | `core/store/Database.h` | SQLite, migração, UPSERT |
| [servico](#servico-o-ciclo-inteiro) | `core/servico/Servico.h` | Orquestração ponta a ponta |
| [calendar](#calendar-aulas-e-ical) | `core/calendar/Calendario.h` | Aula do dia, merge de provas, `.ics` |
| [notify](#notify-politica-de-aviso) | `core/notify/Aviso.h` | Transformar eventos em uma notificação |
| [report](#report-relatorio-html) | `core/report/HtmlReport.h` | Relatório do snapshot |
| [config](#config-instituicao) | `core/config/Instituicao.h` | Catálogo e host selecionado |
| [util](#util-caminhos-utf-8) | `core/util/Caminho.h` | Caminhos UTF-8 seguros no Windows |
| [platform](#platform-cofre-e-notificacao) | `platform/Credenciais.h`, `platform/Notify.h` | Cofre do sistema e notificação nativa |

## model: o vocabulário

`namespace sigaa` | `core/model/Models.h`

Structs simples, sem métodos de negócio: `Turma`, `Atividade`, `Atualizacao`,
`TopicoAula`, `MaterialTopico`, `Avaliacao`, `ArquivoTurma` e o agregador
`Snapshot`. Os campos estão detalhados no diagrama de classes em
[[Arquitetura]].

```cpp
Snapshot s;                      // uma coleta
s.turmas, s.topicos, s.avaliacoes, s.arquivos, s.atividades, s.atualizacoes;
s.minutosSessaoRestantes;        // optional<int>, lido do header do SIGAA
```

`Avaliacao::fonte` diz de onde a prova veio (`PainelAvaliacoes`, alta confiança,
ou `TopicoAula`, inferida por regex no título). O merge usa isso para decidir em
quem confiar quando as duas fontes divergem.

## http: sessão com o SIGAA

`namespace sigaa::http` | `core/http/SigaaSession.h`

```cpp
class SigaaSession {
public:
    explicit SigaaSession(std::string baseUrl = {});   // vazio: usa config::selecionada()

    Response get(std::string_view path);
    Response postForm(std::string_view path, std::string_view urlEncodedBody);
    bool     login(std::string_view cpf, std::string_view senha, std::string* erro = nullptr);
    Response irParaPortal();
    bool     autenticado() const;

    static PageKind classify(std::string_view html);
    static std::optional<int> minutosSessaoRestantes(std::string_view html);

    void setIntervaloMinimo(std::chrono::milliseconds ms);   // throttle entre requisições
    void setUserAgent(std::string ua);
    void setMaxTentativasLogin(int n);
};
```

`Response` traz `status`, `finalUrl`, `body` (bytes crus, possivelmente
binários), `error`, `cabecalhos`, e três ajudantes que evitam bug clássico:

```cpp
bool        ok() const;             // sem erro de curl e status 2xx/3xx
bool        ehDownload() const;     // é arquivo mesmo, ou é HTML de sessão expirada?
std::string nomeSugerido() const;   // Content-Disposition, inclusive RFC 5987
```

`classify()` devolve `PageKind::Portal`, `TurmaVirtual`, `Login`,
`SessaoExpirada` ou `Desconhecida`. Use antes de parsear: o SIGAA responde
`200 OK` com página de erro, então status não basta.

> **Invariante número 1:** uma requisição por vez por instância. A view mora no
> servidor e duas navegações simultâneas no mesmo `JSESSIONID` invalidam o
> estado. Para paralelizar, abra **uma sessão por canal**, cada uma com o
> próprio login.

`infoBackend()` devolve a versão de libcurl/TLS, útil em diagnóstico.

## jsf: formulários e comandos

`namespace sigaa::jsf` | `core/jsf/JsfForm.h`

O motor que traduz "clicar num link" para um POST. É a peça mais reaproveitável
do projeto: serve para qualquer aplicação JSF/RichFaces, não só o SIGAA.

```cpp
using Params = std::vector<std::pair<std::string, std::string>>;

struct Form {
    std::string id, action, viewState;
    Params      fields;
    bool        valid() const;
    std::string buildPostBody(const Params& extra = {}) const;
};

struct Command { std::string formId; Params params; std::string label; };

std::optional<Form>    parseForm(const html::Document& doc, std::string_view formId);
std::optional<Form>    parseForm(const html::Node& formNode);
Params                 parseJsfcljsParams(std::string_view onclick);
std::string            parseJsfcljsFormId(std::string_view onclick);
std::vector<Command>   findCommands(const html::Document& doc);
std::optional<Command> findCommandByLabel(const html::Document& doc, std::string_view substring);
std::string            urlEncode(std::string_view s);
```

Uso típico, navegando por rótulo:

```cpp
auto cmd = sigaa::jsf::findCommandByLabel(doc, "Arquivos");
if (!cmd) return false;
auto form = sigaa::jsf::parseForm(doc, cmd->formId);
if (!form) return false;
auto resp = sessao.postForm(form->action, form->buildPostBody(cmd->params));
```

> Nunca navegue pelo id do componente (`formMenu:j_id_jsp_..._123`). Ele é
> posicional e muda quando a instituição recompila a página, e o SIGAA devolve
> 200 com a aba errada, sem erro nenhum.

## parse: HTML e extratores

`namespace sigaa::html` e `sigaa::parse` | `core/parse/`

### Html.h: consulta com seletor CSS

Fachada fina sobre lexbor, para não espalhar a API C do lexbor pelo projeto.

```cpp
class Document {
    bool parse(std::string_view utf8Html);
    std::vector<Node> select(std::string_view cssSelector) const;
    Node selectFirst(std::string_view cssSelector) const;
    Node root() const;
    std::string text() const;
};

class Node {
    std::string text() const;          // textContent cru, inclui <script>
    std::string rawText() const;
    std::string textoVisivel() const;  // sem <script>/<style>: use este na tela
    std::string attr(std::string_view name) const;
    bool hasAttr(std::string_view name) const;
    std::string tagName() const, innerHtml() const;
    std::vector<Node> select(std::string_view cssSelector) const;
    Node selectFirst(std::string_view cssSelector) const;
};

std::string collapseWhitespace(std::string_view s);
std::string trim(std::string_view s);
bool        isValidUtf8(std::string_view s);
std::string toUtf8(std::string_view html);   // windows-1252 para UTF-8; no-op se já é UTF-8
```

Duas armadilhas de verdade:

*   **`text()` inclui `<script>`.** Todo `.topico-aula` do SIGAA embute o JS de
    drag-and-drop do RichFaces, então `text()` devolve `var elt = $("formAva:...")`
    grudado no que o professor escreveu. Em qualquer campo que vá para a tela,
    use `textoVisivel()`.
*   **`Document::parse` espera UTF-8.** O SIGAA serve windows-1252. Passe o corpo
    da resposta por `toUtf8()` antes.

### PortalParser.h e TurmaParser.h

```cpp
// Portal do discente
std::vector<Turma>       parse::parseTurmas(const html::Document&);
std::vector<Atualizacao> parse::parseAtualizacoes(const html::Document&);
std::vector<Atividade>   parse::parseAtividades(const html::Document&);
Snapshot                 parse::parsePortal(const html::Document&);   // os três de uma vez

// Turma virtual
struct ConteudoTurma { std::vector<TopicoAula> topicos; std::vector<Avaliacao> avaliacoes; bool semTopicos; };
ConteudoTurma parse::parseTurmaVirtual(const html::Document&, const std::string& idTurma,
                                       const std::string& turmaNome, const std::string& periodo);

std::vector<MaterialTopico> parse::parseMateriais(const html::Node& conteudo);
bool parse::parseTituloTopico(std::string_view titulo, std::string& nome, DateTime& inicio, DateTime& fim);
bool parse::pareceAvaliacao(std::string_view titulo);
std::string parse::tipoDoIcone(std::string_view src);
```

`semTopicos` distingue "a turma não publicou nada" de "o parser não entendeu a
página". Não trate os dois como lista vazia.

## sync: coleta, diff e download

`namespace sigaa::sync` | `core/sync/`

### Crawler.h: um ciclo de coleta

```cpp
struct OpcoesColeta {
    bool incluirTurmas{false};      // entra em cada turma: lento, traz provas e material
    bool incluirArquivos{true};     // com incluirTurmas, abre a aba Arquivos
    std::function<void(const std::string&)> progresso;
};

struct ResultadoColeta {
    Snapshot snapshot; bool ok; std::string erro;
    int turmasVisitadas, turmasComFalha;
    std::string htmlPortal, htmlPrimeiraTurma;   // para SIGAA_DUMP e fixtures
};

ResultadoColeta coletar(http::SigaaSession& sessao, const OpcoesColeta& op = {});
```

### Materiais.h: dentro de uma turma

```cpp
class SessaoTurma {
public:
    explicit SessaoTurma(http::SigaaSession& sessao);
    bool entrar(const Turma& turma, std::string* erro = nullptr);
    bool abrirArquivos(std::string* erro = nullptr);

    const parse::ConteudoTurma&       conteudo() const;
    const std::vector<ArquivoTurma>&  arquivos() const;
    bool                              semArquivos() const;   // vazio CONFIRMADO
    const std::vector<std::string>&   menu() const;          // rótulos do menu da turma

    std::optional<std::string> baixar(const std::string& idArquivo,
                                      const std::string& diretorio,
                                      std::string* erro = nullptr);
};

bool abrirAbaPorRotulo(http::SigaaSession&, const html::Document& docTurma,
                       const std::string& rotulo, html::Document* saida,
                       std::string* erro = nullptr);
```

`baixar()` devolve o caminho gravado. Um material de tópico só é baixável se o
`id` dele aparecer na aba Arquivos: tarefa, fórum e vídeo também são `.item` com
ícone e `id`, e deduzir "é arquivo" pelo ícone dá botão que falha na cara do
usuário.

### Baixador.h: fila paralela com cache

```cpp
std::string pastaDaTurma(const std::string& base, const std::string& nomeTurma);
int         canaisRecomendados(int n);          // teto de 3

class CacheLocal {
    explicit CacheLocal(std::string diretorio);
    std::string caminho(const std::string& idArquivo) const;
    bool        temNoDisco(const std::string& idArquivo) const;
    void        registrar(const std::string& idArquivo, const std::string& caminho);
    int         quantosNoDisco() const;
};

class Baixador {
public:
    class Canal { virtual std::optional<std::string> baixar(const std::string& id,
                                                            const std::string& dir,
                                                            std::string* erro) = 0; };
    using AbrirCanal = std::function<std::unique_ptr<Canal>(std::string* erro)>;
    using Progresso  = std::function<void(int prontos, int total)>;

    Baixador(AbrirCanal abrir, std::string diretorio, int canais);
    void setAtrasoEntreCanais(std::chrono::milliseconds ms);
    void setForcar(bool sim);
    std::vector<ItemBaixado> baixar(const std::vector<PedidoDownload>&, const Progresso& = {});
};

Baixador::AbrirCanal canalSigaa(std::string baseUrl, std::string login, /* ... */);
```

O `Canal` é injetado justamente para testar sem rede: `tests/baixador_test.cpp`
passa um canal falso. `pastaDaTurma` tem **uma** definição no projeto de
propósito; se o sync e a janela calcularem o caminho por conta própria e
discordarem num acento, o app baixa tudo de novo, calado.

### DiffEngine.h: o que mudou

```cpp
enum class TipoEvento {
    TurmaNova, AtividadeNova, PrazoMudou, AtividadeConcluida, AtualizacaoNova,
    AvaliacaoNova, AvaliacaoRemarcada, MaterialNovo, ColetaSuspeita
};

struct Evento { TipoEvento tipo; std::string chave, titulo, detalhe, idTurma, turmaNome; };
struct ResultadoDiff { std::vector<Evento> eventos; bool primeiraExecucao; bool suspeito; };

ResultadoDiff diff(const Snapshot& anterior, const Snapshot& novo, bool primeiraExecucao);
```

Função pura: sem rede, sem banco, fácil de testar. `Evento::chave` é estável e
serve para deduplicar. Quando `suspeito` é `true`, **não** trate "sem eventos"
como "sem novidade".

## store: persistência

`namespace sigaa::store` | `core/store/Database.h`

```cpp
class Database {
public:
    explicit Database(const std::string& caminho = "sigaa-viewer.db");
    bool aberto() const;
    const std::string& erro() const;
    bool migrar();                                   // chame antes de tudo
    Snapshot carregarUltimo();
    bool gravar(const Snapshot& s, std::int64_t agora);
    int  ciclos();
    void registrarCiclo(std::int64_t agora);
};
```

`gravar()` faz UPSERT e **nunca apaga o que a coleta não trouxe**. Um sync só de
portal traz zero tópicos, zero avaliações e zero arquivos; um DELETE ali
esvaziaria a tela a cada 20 minutos e faria a coleta completa seguinte anunciar
o acervo inteiro como novidade. `carregarUltimo()` é quem devolve a foto
acumulada, e é dele que a interface deve ler.

## servico: o ciclo inteiro

`namespace sigaa::servico` | `core/servico/Servico.h`

```cpp
Resultado executar(Opcoes op, const Log& log = {});
```

Login, coleta, diff, gravação, relatório, `.ics` e o aviso pronto, numa chamada.
É o ponto de entrada que CLI e interface compartilham.

```cpp
struct Opcoes {
    std::string login, senha;
    bool incluirTurmas{false}, incluirArquivos{true};
    std::string caminhoRelatorio{"relatorio.html"}, caminhoIcs;
    std::string caminhoBanco{"sigaa-viewer.db"};
    std::string pastaMateriais;      // vazio: não baixa
    std::string dumpHtml;            // grava o HTML cru da coleta
};

enum class Falha { Nenhuma, Credenciais, Login, Coleta, ColetaSuspeita, Escrita };

struct Resultado {
    Falha falha; std::string erro;
    Snapshot snapshot; sync::ResultadoDiff diff;
    std::optional<notify::Aviso> aviso;
    bool bancoDisponivel, gravou;
    int turmasVisitadas, turmasComFalha, materiaisPendentes, materiaisBaixados;
    std::string relatorio, ics;
    bool ok() const;
};

using Log = std::function<void(Nivel, std::string_view)>;   // Nivel::Passo | Nivel::Aviso
```

Contrato importante: o serviço **não pergunta credenciais** (prompt é interface)
e **não notifica** (`plat::` é plataforma). Ele devolve `aviso` preenchido e
quem chamou decide. `ColetaSuspeita` significa que ele coletou mas não gravou,
para não destruir a base com um resultado duvidoso.

Depois de um sync sem `incluirTurmas`, releia do banco antes de pintar a tela:
`Resultado::snapshot` é a coleta, não a foto acumulada.

## calendar: aulas e iCal

`namespace sigaa::calendario` | `core/calendar/Calendario.h`

```cpp
std::vector<Avaliacao> mesclarAvaliacoes(std::vector<Avaliacao> todas);
bool aulaOcorreEm(const TopicoAula& t, const DateTime& d);
std::vector<TopicoAula> aulasDoDia(const std::vector<TopicoAula>& topicos, const DateTime& d);

struct OpcoesIcs { int lembreteMinutos{24 * 60}; std::string prefixoUid{"sigaa-viewer"}; };
std::string gerarIcs(const Snapshot& s, const OpcoesIcs& op = {});
```

A regra de "esta aula acontece hoje" mora aqui, e não na interface, porque um
tópico pode cobrir um bloco de dias ("Semana 3 (10/08 - 14/08)"): comparar só
com a data de início esvazia a tela em silêncio.

## notify: política de aviso

`namespace sigaa::notify` | `core/notify/Aviso.h`

```cpp
struct Aviso { std::string titulo, corpo; bool urgente; };

std::string_view rotulo(sync::TipoEvento t);
int              prioridade(sync::TipoEvento t);
std::optional<Aviso> montarAviso(const sync::ResultadoDiff& d);
```

Agrupa muitos eventos numa notificação só e escolhe o que merece o título.
Devolve `nullopt` quando não há o que avisar. Não toca no sistema operacional:
quem entrega é `plat::notificar`.

## report: relatório HTML

`namespace sigaa::report` | `core/report/HtmlReport.h`

```cpp
std::string renderHtml(const Snapshot& s);
```

Documento autocontido, sem dependência externa. Bom ponto de partida para quem
quiser outro formato de saída.

## config: instituição

`namespace sigaa::config` | `core/config/Instituicao.h`

```cpp
struct Instituicao {
    std::string id, nome, baseUrl;
    bool verificada{false};
    std::string host() const;
    bool valida() const;
};

const std::vector<Instituicao>& catalogo();
std::optional<Instituicao>      porId(std::string_view id);
Instituicao                     personalizada(std::string_view urlOuHost);
const Instituicao&              selecionada();
void                            selecionar(Instituicao inst);
```

`host()` entra na chave do cofre de credenciais. É por isso que trocar de
instituição não faz o app tentar a senha de uma universidade na outra. Só a
UNIFEI está marcada `verificada`; acrescentar entrada sem conferir contra o site
real é pior que não ter, porque o usuário culpa a própria senha quando o parser
é que não entende a página.

## util: caminhos UTF-8

`namespace sigaa::util` | `core/util/Caminho.h`

```cpp
std::filesystem::path deUtf8(std::string_view s);
std::string           paraUtf8(const std::filesystem::path& p);
std::string           nomeSeguro(std::string_view s);
std::filesystem::path caminhoLivre(const std::filesystem::path& dir, const std::string& nomeUtf8);
```

`std::string` no projeto é UTF-8, e `std::filesystem::path` não sabe disso. No
Windows, `path(std::string)` lê os bytes na code page ANSI e "PROJETO E ANÁLISE"
vira uma segunda pasta "PROJETO E ANÃLISE" ao lado da certa. Sempre converta com
`deUtf8()`, e volte com `paraUtf8()`: `path::string()` também achata o acento.

## platform: cofre e notificação

`namespace sigaa::plat` | `platform/Credenciais.h`, `platform/Notify.h`

```cpp
std::string backendCofre();
bool        cofreDisponivel();
bool        guardarNoCofre(const std::string& login, const std::string& senha, std::string* erro = nullptr);
std::optional<Credenciais> lerDoCofre(std::string* erro = nullptr);
bool        apagarDoCofre(std::string* erro = nullptr);

enum class Origem { Nenhuma, Ambiente, Cofre, DotEnv };
struct CredenciaisResolvidas { Credenciais cred; Origem origem; bool ok() const; };

CredenciaisResolvidas resolverCredenciais(const std::string& caminhoDotEnv = ".env");
void                  limparSegredo(std::string& s);
```

`resolverCredenciais()` implementa a precedência do projeto: ambiente, cofre do
sistema, `.env`, e então prompt (que é responsabilidade de quem chama). Nunca
grave no cofre antes de validar no SIGAA: senha errada guardada é tentada de
novo a cada ciclo, o SIGAA bloqueia a conta, e o usuário não liga o bloqueio a
este app.

Notificação nativa fica em `platform/Notify.h` (`plat::Notificacao`,
`plat::notificar`, `plat::backendNotificacao`). É o único lugar que fala com o
sistema operacional para avisar o usuário.
