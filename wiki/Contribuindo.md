# Guia de Contribuição

Bem-vindo(a) ao SIGAA Desktop Viewer! Agradecemos o seu interesse em contribuir para o projeto. Este guia fornecerá as diretrizes para que você possa começar a ajudar, garantindo a organização e a qualidade do código.

## Índice

- [Começando](#começando)
- [Padrões de Commit (Conventional Commits)](#padrões-de-commit-conventional-commits)
- [Estilo de Código](#estilo-de-código)
- [Testes](#testes)
- [Diretrizes para Pull Requests (PRs)](#diretrizes-para-pull-requests-prs)
- [Regras de Segurança](#regras-de-segurança)
- [Etiqueta no Servidor SIGAA](#etiqueta-no-servidor-sigaa)

## Começando

O fluxo de trabalho principal baseia-se em _fork_ e _pull request_:

1. **Fork** este repositório para a sua conta do GitHub.
2. **Clone** o repositório forkado para a sua máquina local.
3. Crie uma **Branch** a partir da branch principal (`main` ou `master`):
   - Nomenclatura da branch: `feat/<nome>`, `fix/<nome>`, `docs/<nome>`, `refactor/<nome>`
   - Exemplo: `feat/download-paralelo`
4. **Codifique** suas alterações, seguindo as diretrizes deste guia.
5. Escreva e rode os **Testes**.
6. Faça o **Pull Request (PR)** de volta para o repositório original.

## Padrões de Commit (Conventional Commits)

Nós utilizamos a convenção de [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) para manter o histórico de alterações limpo e legível.

**Formato:** `tipo(escopo): descrição`

**Tipos permitidos:**
- `feat`: Uma nova funcionalidade.
- `fix`: Correção de um bug.
- `docs`: Alteração exclusiva em documentação.
- `refactor`: Refatoração de código sem mudança de comportamento.
- `test`: Adição ou correção de testes.
- `chore`: Tarefas de manutenção, ferramentas, atualizações de dependências.
- `perf`: Melhoria de performance.
- `style`: Formatação, ponto e vírgula faltando, etc. (sem mudança lógica).

**Escopos permitidos:**
- `model`, `http`, `parse`, `store`, `sync`, `calendar`, `config`, `servico`, `notify`, `ui`, `platform`, `app`, `build`

**Exemplos:**
```
feat(sync): adiciona download paralelo de materiais com retry
fix(parse): trata lista de tópicos vazia na página da turma
docs(wiki): adiciona diagramas de arquitetura
test(store): adiciona asserções de idempotência no UPSERT
refactor(http): extrai parser do Content-Disposition
```

**Regras:**
- O tamanho do _subject_ (a primeira linha) não deve passar de 72 caracteres.
- Use o modo imperativo (ex: "adiciona", não "adicionado").
- No **Body** (corpo do commit opcional), explique o **PORQUÊ** da alteração, não apenas o QUE foi feito.

## Estilo de Código

Estamos utilizando **C++20**. Siga as diretrizes abaixo:

- **Features C++20:** Encorajamos o uso de `std::string_view`, _structured bindings_, e `std::ranges`.
- **Idioma:** 
  - Nomes de conceitos do domínio SIGAA em **Português** (`Turma`, `Avaliacao`, `Atividade`, `Topico`).
  - Nomes de utilitários genéricos e padrões de software em **Inglês** (ex: `HttpManager`, `StoreFactory`).
- **Nomenclatura:**
  - Classes: `PascalCase`
  - Funções/Métodos: `camelCase`
  - Constantes: `SCREAMING_SNAKE_CASE`
- **Arquivos:**
  - Classes: `PascalCase` (ex: `TurmaParser.cpp`).
  - Testes: `snake_case` (ex: `turma_parser_test.cpp`).
- **Headers:** Utilize `#pragma once` em todos os headers.
- **Passagem de parâmetros:** Prefira `const&`. Utilize `std::string_view` para strings de somente leitura.
- **Gerenciamento de recursos:** Utilize **RAII** rigorosamente. Evite ponteiros puros para controle de ciclo de vida.

## Testes

Testar o código é fundamental.

- **Framework:** Utilizamos [Google Test](https://google.github.io/googletest/).
- **Nomenclatura dos arquivos:** `tests/<module>_test.cpp`.
- **Como rodar:** `ctest --test-dir build -C Release`
- **Mock de rede:** Mock as chamadas de rede. **NUNCA bata no servidor real do SIGAA nos testes automatizados.**
- **Fixtures (Mocks HTML):** Utilize HTML limpo. **NUNCA commite HTML bruto do SIGAA**, pois ele contém informações sensíveis (PII).
- **Sanitização:** Use o script `tools/redact.py` para sanitizar fixtures antes de usar.

## Diretrizes para Pull Requests (PRs)

- Faça apenas **uma mudança lógica por PR**.
- Garanta que **todos os testes** estejam passando.
- Se você adicionar um novo módulo ou conceito, atualize a documentação na wiki.
- Descreva o **PORQUÊ** na descrição do PR. O que motivou a mudança?
- Para mudanças na interface (UI), inclua capturas de tela (Screenshots).

## Regras de Segurança

> [!CAUTION]
> Lidar com credenciais dos usuários exige o máximo de cuidado. O não cumprimento destas regras resultará em rejeição imediata do PR.

- **NUNCA** commite arquivos `.env`, HTML bruto (fixtures) ou arquivos `.har`.
- **NUNCA** aceite senhas via linha de comando (`argv`).
- **NUNCA** registre (log) senhas ou tokens de credenciais.
- O **CPF** deve ficar dentro do _blob_ criptografado do Vault (cofre), não no campo de nome de usuário normal.
- Limpe a memória após o uso de senhas usando `SecureZeroMemory` (Windows) ou `explicit_bzero` (Linux).
- Limite as tentativas de login (máximo 3).
- Valide as respostas do SIGAA antes de armazenar e confirmar as credenciais.

## Etiqueta no Servidor SIGAA

> [!WARNING]
> Abusar da infraestrutura da universidade nos prejudica a todos. Siga estas regras rigorosamente!

- **Intervalo de polling:** Entre 15 a 30 minutos. **NUNCA menor que 5 minutos.**
- Envie apenas **uma requisição por vez** na mesma sessão. Nada de flood no SIGAA.
- Utilize *exponential backoff* com *jitter* caso encontre erros de conexão.
- Respeite os códigos HTTP `429`, `503` e páginas de manutenção do sistema.
- Envie um **User-Agent identificável** do aplicativo.
- Crie um limite estrito nas tentativas de repetição de login (evite bloquear a conta do usuário por tentativas erradas).

Agradecemos imensamente a sua contribuição. Mãos à obra!
