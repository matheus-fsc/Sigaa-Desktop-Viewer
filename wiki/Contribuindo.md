# Guia de contribuição

Obrigado pelo interesse no SIGAA Desktop Viewer. Este guia reúne as convenções
do projeto e, principalmente, as regras que não são óbvias: quase todas nasceram
de um bug real.

## Índice

- [Começando](#começando)
- [Padrões de commit](#padrões-de-commit)
- [Estilo de código](#estilo-de-código)
- [Testes](#testes)
- [Pull requests](#pull-requests)
- [Regras de segurança](#regras-de-segurança)
- [Etiqueta com o servidor do SIGAA](#etiqueta-com-o-servidor-do-sigaa)

## Começando

O fluxo é fork e pull request:

1. **Fork** deste repositório.
2. **Clone** o seu fork.
3. Crie uma **branch** a partir de `main`:
   - Nomenclatura: `feat/<nome>`, `fix/<nome>`, `docs/<nome>`, `refactor/<nome>`
   - Exemplo: `feat/parser-de-notas`
4. **Compile e rode os testes** antes de começar, para separar o que você
   quebrou do que já estava quebrado.
5. **Codifique**, seguindo as convenções abaixo.
6. Abra o **pull request** contra `main`.

Se for mexer em parser ou em qualquer coisa que fale com o SIGAA, leia
`docs/RECON.md` antes. É o registro de engenharia reversa verificado contra o
site real, e a wiki não substitui os detalhes de protocolo que estão lá.

## Padrões de commit

Usamos [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).

**Formato:** `tipo(escopo): descrição`

**Tipos:** `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `perf`, `style`.

**Escopos:** `model`, `http`, `jsf`, `parse`, `store`, `sync`, `calendar`,
`config`, `servico`, `notify`, `report`, `util`, `ui`, `platform`, `app`,
`build`.

**Exemplos:**
```
feat(sync): adiciona download paralelo de materiais com retry
fix(parse): trata lista de topicos vazia na pagina da turma
docs(wiki): adiciona referencia da API do core
test(store): adiciona assercoes de idempotencia no UPSERT
refactor(http): extrai parser do Content-Disposition
```

**Regras:**
- O assunto (primeira linha) não passa de 72 caracteres.
- Modo imperativo: "adiciona", não "adicionado".
- No corpo, explique o **porquê**. O que foi feito o diff já mostra.

## Estilo de código

O projeto é **C++20**. Comentários e nomes em português, como o resto da base.

- **Recursos de C++20**: `std::string_view`, structured bindings e `std::ranges`
  são bem-vindos.
- **Idioma dos nomes**: conceitos do domínio SIGAA em português (`Turma`,
  `Avaliacao`, `TopicoAula`, `ArquivoTurma`); o vocabulário técnico segue o que
  já existe no módulo (`SigaaSession`, `DiffEngine`, `Baixador`, `CacheLocal`).
  Consistência com o arquivo vizinho vale mais que a regra.
- **Nomenclatura**: classes em `PascalCase`, funções e métodos em `camelCase`,
  membros com sufixo `_`, constantes de arquivo com prefixo `k`
  (`kMinutosPortal`).
- **Arquivos**: classes em `PascalCase.cpp`, testes em `snake_case_test.cpp`.
- **Headers**: `#pragma once` em todos.
- **Parâmetros**: prefira `const&`; use `std::string_view` para strings só de
  leitura.
- **Recursos**: RAII sempre. Evite ponteiro cru para controlar tempo de vida.
- **Erros**: o padrão do core é devolver `bool`/`optional` com um
  `std::string* erro` opcional, não lançar exceção. Siga isso em código novo.

### A regra de ouro

`src/core/` **não conhece framework de interface nenhum**. Integração com o
sistema operacional vai em `src/platform/`; Qt fica confinado em `src/ui/`. Um
`#include <Q...>` dentro de `core/` quebra o build sem Qt e tira o módulo do CI
headless. Detalhes das camadas em [[Arquitetura]], e das assinaturas em
[[Referencia-da-API]].

## Testes

- **Framework:** [Catch2 v3](https://github.com/catchorg/Catch2)
  (`#include <catch2/catch_test_macros.hpp>`).
- **Arquivos:** `tests/<modulo>_test.cpp`.
- **Como rodar:** `ctest --preset windows` (ou `linux`, `macos`).
- **Nomes de `TEST_CASE` em ASCII puro.** O CTest passa o nome como filtro, e a
  code page do console do Windows corrompe acento: o teste **some em silêncio**
  ("No tests ran") em vez de falhar. Essa é a pegadinha que mais custa tempo
  aqui.
- **Nunca bata no servidor real do SIGAA em teste automatizado.** Injete o que
  fala com a rede. O `Baixador`, por exemplo, recebe uma fábrica de canal, e o
  teste passa um canal falso.
- **Fixtures vêm do HTML cru da rede**, capturado com `sigaa-cli explorar` ou
  `SIGAA_DUMP=...`, nunca de Ctrl+S no browser: o DOM pós-JavaScript é 2,7 vezes
  maior e tem markup que o servidor nunca envia (`docs/RECON.md` §1.7). Fixture
  assim mente para o parser.
- **Todo HTML capturado passa por `tools/redact.py`** antes de virar fixture. A
  redação é idempotente, e `--check` serve como hook de pre-commit.

## Pull requests

- Uma mudança lógica por PR.
- Todos os testes passando.
- Descreva o **porquê** na descrição do PR.
- Para mudança de interface, inclua captura de tela.
- Se você adicionou módulo ou conceito novo, atualize a wiki (inclusive
  [[Referencia-da-API]], se mexeu em header público).
- Depois de mexer no código, rode `graphify update .` para manter o grafo de
  conhecimento em dia.

## Regras de segurança

> [!CAUTION]
> Este app lida com a credencial da conta acadêmica de alguém. Descumprir
> qualquer regra abaixo é motivo de rejeição imediata do PR.

- **NUNCA** commite `.env`, HTML cru do SIGAA ou arquivos `.har`. O `.gitignore`
  bloqueia `tests/fixtures/raw/` e `*.har`; mantenha assim.
- **NUNCA** aceite senha por `argv`: fica no histórico do shell.
- **NUNCA** registre senha ou token em log.
- **NUNCA** grave credencial no cofre sem validar no SIGAA antes. Senha errada
  guardada é tentada de novo a cada ciclo, o SIGAA bloqueia a conta, e o usuário
  não liga o bloqueio a este app.
- O CPF vai cifrado dentro do blob do cofre, não no campo de usuário.
- Limpe segredo da memória depois do uso (`plat::limparSegredo`).
- Limite rígido de tentativas de login.
- O host do SIGAA entra na chave do cofre. Não crave `sigaa.unifei.edu.br` em
  lugar nenhum novo.

## Etiqueta com o servidor do SIGAA

> [!WARNING]
> A infraestrutura é da universidade e é compartilhada com todo mundo.

- **Intervalo de polling** entre 15 e 30 minutos. Nunca menos que 5.
- **Uma requisição por vez na mesma sessão.** Não é só educação: a view mora no
  servidor, e duas navegações simultâneas no mesmo `JSESSIONID` invalidam o
  estado. Para paralelizar, abra uma sessão por canal, com teto de 3.
- Use backoff exponencial com jitter em erro de conexão.
- Respeite `429`, `503` e páginas de manutenção.
- Envie User-Agent identificável.
- Limite as tentativas de repetição de login, para não bloquear a conta do
  usuário.
