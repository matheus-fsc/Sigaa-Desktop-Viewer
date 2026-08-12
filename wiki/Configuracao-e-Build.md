# Configuração e Build

Como compilar, configurar e empacotar o **SIGAA Desktop Viewer**.

## Pré-requisitos

*   **Compilador C++20**: MSVC 2022+, GCC 12+ ou Clang 15+.
*   **CMake 3.24 ou superior** (o projeto usa `CMakePresets.json`, versão 6).
*   **As bibliotecas**, de uma das duas procedências — o `CMakeLists.txt` aceita
    as duas, e cada `find_package` tem plano B:
    *   `curl` (com SSL)
    *   `lexbor` (parser de HTML)
    *   `sqlite3`
    *   `nlohmann-json`
    *   `spdlog`
    *   `catch2` (só para os testes)
*   **vcpkg**, no Windows: as dependências vêm do manifesto `vcpkg.json` e não é
    preciso instalar nenhuma à mão.
*   **Qt 6.5+** (`Widgets` e `Svg`), **opcional**: só para a interface gráfica.
    Qt não vem do vcpkg, porque compilar do fonte leva horas.
*   *Em tempo de execução, no Linux*: `secret-tool` (pacote `libsecret` no Arch,
    `libsecret-tools` no Debian) para o cofre de credenciais, e `notify-send`
    (`libnotify`) para as notificações. **Não são dependências de compilação** —
    o app chama os dois por `execvp` e degrada sozinho se faltarem.

## Build

### Windows (vcpkg)

```sh
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "%CD%\vcpkg"

git clone https://github.com/matheus-fsc/Sigaa-Desktop-Viewer.git
cd Sigaa-Desktop-Viewer

cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

### Linux (pacotes da distro)

O preset **`linux-distro`** não passa pelo vcpkg. O preset `linux` continua
existindo para quem quiser o vcpkg também aqui.

```bash
# Arch / CachyOS / Manjaro
sudo pacman -S --needed base-devel cmake ninja curl sqlite nlohmann-json \
                        spdlog catch2 qt6-base qt6-svg qt6-wayland
yay -S lexbor            # única que não está nos repos oficiais

# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build libcurl4-openssl-dev \
                 libsqlite3-dev nlohmann-json3-dev libspdlog-dev catch2 \
                 qt6-base-dev libqt6svg6-dev
# lexbor: `liblexbor-dev` no Debian 13+/Ubuntu 24.10+; antes disso, compile
# de github.com/lexbor/lexbor (cmake, dois minutos).

cmake --preset linux-distro
cmake --build --preset linux-distro
ctest --preset linux-distro
```

Duas armadilhas conhecidas, ambas já tratadas no `CMakeLists.txt` — se você
mexer nele, não as reintroduza:

*   `find_package(CURL CONFIG REQUIRED)` **falha em qualquer distro**. O arquivo
    de config é coisa do vcpkg; quem acha a libcurl do sistema é o `FindCURL` que
    vem com o CMake. O mesmo vale para o `unofficial-sqlite3`, que é um nome que
    só existe no vcpkg.
*   O alvo do SQLite mudou de nome: `SQLite::SQLite3` até o CMake 3.30,
    `SQLite3::SQLite3` daí em diante.

### Só a CLI, sem Qt

**A interface é opcional.** Sem Qt instalado, o CMake avisa e segue em frente:
`sigaa-cli` e os testes continuam compilando. Quem só quer mexer no parser não
precisa baixar 1,5 GB de Qt.

```sh
cmake --preset linux -DSIGAA_UI=OFF     # desliga a interface e silencia o aviso
```

### Com a interface gráfica

Qt não vem do vcpkg. O caminho desatendido, sem instalador gráfico e sem conta
Qt:

```sh
pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:/Qt
```

O lugar certo para um caminho de máquina é o `CMakeUserPresets.json`, que está
no `.gitignore`:

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

O módulo `Svg` não é enfeite: os ícones são SVG dentro do `.qrc`, e quem ensina
o `QIcon` a abrir SVG é o plugin iconengine desse módulo. Sem ele a barra de
ferramentas fica só com texto.

## Configuração

### Credenciais

A precedência é: **ambiente > cofre do sistema > `.env` > prompt**.

| Origem | Como | Quando usar |
| --- | --- | --- |
| Ambiente | `SIGAA_LOGIN`, `SIGAA_SENHA` | automação e CI |
| Cofre do sistema | `sigaa-cli login` | uso normal, recomendado |
| `.env` | arquivo na pasta de execução | legado; o app sugere migrar |
| Prompt | interativo | primeira execução |

*   **Windows**: Credential Manager.
*   **Linux**: Secret Service via D-Bus (GNOME Keyring, KWallet), usando
    `libsecret`.

Login e senha vão juntos num blob cifrado. O CPF não fica no campo de usuário,
que é legível por qualquer aplicação. Nunca passe a senha por argumento: ela
fica no histórico do shell.

### Arquivo `.env` (opcional)

```env
SIGAA_LOGIN=seu_cpf
SIGAA_SENHA=sua_senha
SIGAA_URL=sigaa.unifei.edu.br
```

`SIGAA_URL` (ou `SIGAA_INSTITUICAO`) escolhe o SIGAA. Também dá para passar por
linha de comando, e vale para qualquer subcomando:

```sh
sigaa-cli --instituicao unifei doctor
sigaa-cli --url sigaa.suafaculdade.edu.br doctor
```

Só a UNIFEI foi verificada contra o site real. Outra instância pode rodar uma
versão diferente do SIGAA e falhar na leitura, e isso não é problema da senha.

### Primeiro uso

```sh
sigaa-cli login      # valida no SIGAA e só então grava no cofre
sigaa-cli doctor     # mostra backend de rede, cofre, notificação e instituição
sigaa-cli sync       # primeiro ciclo
```

Na interface, o diálogo de login aparece quando o cofre não devolve nada.

> Rodar o `sigaa-ui` faz **login de verdade** na conta da universidade. Se já há
> credencial no cofre, abrir o app dispara sincronização real. Não abra o
> binário só para conferir layout.

## Empacotamento

Release é a pasta `dist/`, gerada por `tools/empacotar.ps1` (Windows):

```powershell
pwsh tools/empacotar.ps1
# -> dist/SIGAA-Desktop-Viewer-v<versao>-windows-x64/ e o .zip ao lado
```

O script:

1.  Roda o `ctest` em Release e **recusa empacotar com teste falhando**
    (`-PularTestes` força, para iterar no próprio script).
2.  Monta a pasta de destino do zero, limpando a anterior.
3.  Chama o `windeployqt` para trazer DLLs e plugins do Qt.
4.  Exclui o que não é entregável: `sigaa_tests.exe`, `.lib`, `.pdb`.
5.  **Recusa** copiar `sigaa-viewer.db`, `relatorio.*` e `.env` que tenham
    sobrado no diretório de build. Esses arquivos nascem ali quando se roda o
    app pela IDE, e contêm dados reais de quem compilou.
6.  Gera o `.zip` distribuível.

Nada dentro de `build/` é entregável: `Debug/` precisa das `Qt6*d.dll`,
`Release/` não tem as DLLs de terceiros arrumadas, e os `*.dir/` são andaime do
gerador.
