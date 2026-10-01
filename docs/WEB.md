# Acesso mobile

As telas do app no celular, pelo navegador, servidas por este computador
através de uma VPN. Código em `src/web/` (servidor e API), `src/web/pagina/`
(as telas) e `src/ui/Mobile.*` (a aba **Acesso mobile**).

## 1. O desenho

```
PC (app aberto ou `sigaa-cli web`)                 Celular
┌────────────────────────────────┐
│ cpp-httplib, 4 threads, só GET │    VPN      ┌───────────┐
│  ├─ páginas embutidas          │◄──────────►│ navegador │
│  ├─ /api/* (JSON do SQLite)    │ (Tailscale, │  ou PWA   │
│  └─ token no banco (meta)      │  WireGuard…)└───────────┘
└────────────────────────────────┘
```

| Decisão | Por quê |
|---|---|
| cpp-httplib | Um cabeçalho só, no vcpkg, no Arch (`cpp-httplib`) e no Homebrew. Drogon e Crow trariam ORM e plugins que o projeto não usa; o Boost.Beast traria o Boost inteiro. |
| HTTP puro, sem TLS próprio | Quem cifra é a VPN. Quem quer HTTPS (PWA instalável, service worker) põe `tailscale serve`, Caddy etc. na frente de `127.0.0.1`. O servidor não guarda certificado nenhum. |
| Páginas embutidas no binário | `cmake/Embutir.cmake` vira `PaginasWeb.cpp` na compilação. Não há pasta para escolher (o AppImage não tem uma gravável) e ninguém troca o `app.js` no disco. |
| Só leitura | Um "Atualizar" no celular é fácil de apertar dez vezes, e quem paga pelo bloqueio do SIGAA é a conta do aluno. A escrita, quando vier, passa pelo orçamento do MCP. |
| Sem dado de colega | Igual ao MCP: participantes não saem por esta porta. |
| Independente de VPN | Nada depende do Tailscale. O servidor escuta no IP da interface escolhida, e a autenticação é o token próprio. |

## 2. Endereço

A aba lista as interfaces IPv4 do computador. As de VPN vêm primeiro e
marcadas: nome `tailscale*`, `wg*`, `tun*`, `utun*`, `zt*`, `ppp*`…, tipo
*virtual* ou IP em `100.64.0.0/10`. Também aparecem a rede local (com aviso) e
`127.0.0.1` (para usar atrás de um proxy).

**`0.0.0.0` não é oferecido, e o servidor o recusa mesmo pelo CLI.** Escutar em
todas as interfaces é o erro que expõe o app no Wi-Fi da faculdade, e a VPN
não protege o que não passa por ela.

A escolha é guardada pelo **nome da interface**, não pelo IP. Com
"ligar sozinho" marcado, se a interface ainda não existe quando o app abre (a
VPN está subindo), a aba tenta de novo a cada 30 s. Ela não cai em outra rede
por conta própria.

## 3. Pareamento

- Token de 256 bits (`random_device`), em base64url, guardado no banco
  (`meta`, chave `web.token`). Não fica no cofre do sistema: quem lê o banco já
  tem todos os dados que o token protege.
- O QR code e o link carregam `http://<ip>:<porta>/#t=<token>`. O token vai no
  **fragmento**, que o navegador nunca envia, então não aparece em log, proxy
  nem histórico. A página o guarda em `localStorage` e limpa a barra de
  endereço.
- Toda chamada a `/api/*` leva `Authorization: Bearer <token>`. A checagem
  roda no *pre-routing*, antes de qualquer rota, então uma rota nova não
  nasce aberta. A comparação é em tempo constante.
- O token é lido do banco a cada pedido. **Gerar novo código** derruba os
  celulares antigos no pedido seguinte, sem reiniciar o servidor.
- 10 tokens errados vindos do mesmo IP em 10 minutos resultam em 429 até a
  janela passar.
- A janela e o `sigaa-cli web` usam o mesmo token, então parear por um vale
  para o outro.

## 4. API

Tudo é `GET` e devolve JSON com `Cache-Control: no-store`. Qualquer outro
método recebe 405, mesmo com token.

| Rota | O que traz |
|---|---|
| `/api/resumo` | instituição, período, `ultimo_sync` (epoch) e as contagens da barra de navegação |
| `/api/agenda?semana=AAAA-MM-DD` | os 7 dias da semana daquela data (aulas pela grade, com o tópico do dia, faltas `n/limite` e a reposição como `extra`) e os prazos em aberto (até 7 dias de atraso) |
| `/api/provas` | as provas **efetivas** (SIGAA mais as correções do aluno), com `estado` (`do_sigaa`, `deduzida`, `confirmada`, `corrigida`, `criada`, `conflito`) e `data_sigaa` quando o aluno corrigiu |
| `/api/turmas` | as turmas, com faltas, próxima prova e quantos arquivos estão baixados |
| `/api/turmas/{id}` | tópicos, arquivos, notícias (em **texto puro**), frequência dia a dia (com as marcações do aluno) e provas |
| `/api/novidades` | as atualizações do portal, das mais novas para as mais antigas (no máximo 100) |
| `/api/arquivos/{turma}/{arquivo}` | o material **que o computador já baixou**. O caminho sai do manifesto da pasta da turma, nunca da URL, e os dois ids precisam ser só dígitos. O que não foi baixado não sai do SIGAA por aqui. |

O código de `src/web/Api.cpp` não conhece socket: recebe um caminho e devolve
uma resposta. Os testes (`tests/web_test.cpp`) o exercitam assim, e só os
testes de autenticação abrem uma porta em `127.0.0.1`.

## 5. Cabeçalhos

Toda resposta sai com:

```
Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self';
  img-src 'self' data: blob:; connect-src 'self'; manifest-src 'self';
  frame-ancestors 'none'; base-uri 'none'; form-action 'none'
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Referrer-Policy: no-referrer
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Resource-Policy: same-origin
```

A política fechada só é possível porque a página não usa script nem estilo
inline e não carrega nada de fora, nem fonte do Google: usa a fonte do sistema
do celular. A primeira barreira, porém, é o `app.js`: **nenhum dado vira
HTML**, tudo entra por `textContent`. Texto de tópico e de notícia é conteúdo
do professor.

## 6. As telas

Mobile-first, traduzidas da variante mobile dos protótipos (`server/Ajustes de
telas no quadro(1)/`). São quatro seções na barra de baixo, com contagens:

- **Agenda**: semana com ◀ ▶ e "Hoje". Os dias que já passaram na semana
  atual ficam recolhidos. Cada aula mostra o horário (código da grade), o
  tópico, a sala e as faltas, e embaixo vêm os prazos atuais.
- **Provas**: próxima prova, as deduzidas "a confirmar", carga das próximas
  seis semanas, a lista agrupada por semana e as provas antigas recolhidas.
- **Turmas**: lista e detalhe com as abas Aulas (o tópico em andamento vem
  aberto), Arquivos (com "Abrir" para o que está baixado), Notícias, Presença
  e Provas.
- **Novidades**: o que o portal anunciou, agrupado por dia.

O tema segue o sistema e pode ser trocado no menu ⋯, onde também fica
"Esquecer este celular". Ao voltar para a aba do navegador, a página relê os
dados, já que o computador pode ter sincronizado nesse meio-tempo.

## 7. Receitas

**Tailscale.** Instale no PC e no celular com a mesma conta e escolha
`tailscale0` (no Windows, `Tailscale`).

**HTTPS, para instalar como app:** escolha `127.0.0.1`, rode
`tailscale serve --bg 8765` e abra `https://<máquina>.<tailnet>.ts.net/#t=<token>`.

**WireGuard, OpenVPN, ZeroTier:** escolha a interface da VPN. O celular precisa
estar na mesma VPN.

**Sem janela:**

```sh
sigaa-cli web --escutar 100.101.2.3:8765    # ou o AppImage: SIGAA-Viewer.AppImage web …
sigaa-cli web token                          # mostra o código
sigaa-cli web token --novo                   # revoga o anterior
```

## 8. Falta

- Escrita pelo celular (confirmar ou corrigir uma prova, marcar presença,
  atualizar uma turma), passando pelo orçamento de rede do MCP.
- QR code no Windows: o vcpkg ainda não traz o qrcodegen no `vcpkg.json`. Até
  lá, o pacote do Windows mostra só o link para copiar.
- Service worker para abrir sem conexão. Só faz sentido atrás de HTTPS.
