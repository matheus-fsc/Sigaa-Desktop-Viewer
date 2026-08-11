# Configuração e Build

Este guia descreve os pré-requisitos, a preparação do ambiente e as etapas necessárias para compilar e empacotar o projeto **SIGAA Desktop Viewer**.

## Pré-requisitos

Para compilar o código e os recursos corretamente, você precisará de:

*   **Compilador C++20**:
    *   No Windows: MSVC 2022+ ou MinGW (GCC 12+)
    *   No Linux: GCC 12+
*   **CMake** versão 3.21 ou superior
*   **Qt 6.5+**: Assegure a presença dos módulos `Widgets`, `Svg` e `Network`.
*   **vcpkg**: Gerenciador de pacotes da Microsoft para tratar as bibliotecas C/C++ dependentes:
    *   `gumbo-parser`
    *   `nlohmann-json`
    *   `SQLite3`
    *   `Google Test`
*   *(Opcional / Linux)* `libsecret-1-dev`: Necessário para compilar suporte ao cofre de credenciais nativo.

## Build no Windows

Utilizando Powershell, abra o terminal no diretório base desejado:

```powershell
# 1. Clone o repositório
git clone https://github.com/matheus-fsc/Sigaa-Desktop-Viewer.git
cd Sigaa-Desktop-Viewer

# 2. Configure a toolchain utilizando vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<caminho_do_vcpkg>/scripts/buildsystems/vcpkg.cmake

# 3. Construa o binário na configuração Release
cmake --build build --config Release

# 4. Execute a suíte de testes
ctest --test-dir build -C Release
```

*(Lembre-se de substituir `<caminho_do_vcpkg>` pelo caminho local de instalação do seu vcpkg).*

## Build no Linux

Instale as dependências providas pelo gerenciador de pacotes do sistema:

```bash
# Instalação das dependências e ferramentas no Debian/Ubuntu:
sudo apt update
sudo apt install qt6-base-dev libqt6svg6-dev libsecret-1-dev cmake g++ build-essential

# 1. Clone e entre no projeto (mesmo acima)

# 2. Configure a toolchain
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<caminho_do_vcpkg>/scripts/buildsystems/vcpkg.cmake

# 3. Compile usando todos os núcleos da máquina
cmake --build build -j$(nproc)

# 4. Rode os testes
ctest --test-dir build
```

## Configuração do Ambiente

O SIGAA Viewer pode ler variáveis de ambiente se configurado localmente via arquivo `.env`.

### Arquivo `.env` (Opcional)

Formato:
```env
SIGAA_LOGIN=usuario_cpf
SIGAA_SENHA=sua_senha
SIGAA_URL=https://sigaa.unifei.edu.br/sigaa/
```

### Cofre de Credenciais

*   **Windows**: O projeto utiliza automaticamente o Windows Credential Manager.
*   **Linux**: Conecta-se à D-Bus Secret Service API (GNOME Keyring / KWallet).

### Primeiro Uso

Após um build com sucesso, o primeiro run requer que você instancie uma sessão logada:

Via CLI:
```bash
sigaa-cli login
```

Via GUI:
Execute o binário base, e a caixa de diálogo principal forçará o input das credenciais caso o cofre retorne nulo.

## Empacotamento

Na pasta `tools/`, providenciamos scripts de empacotamento.

Para **Windows**, utilize `tools/empacotar.ps1`. Este script:
1.  Limpa compilações antigas.
2.  Inicia o build na diretiva de `Release`.
3.  Usa a ferramenta `windeployqt` (automática pelo Qt6) para copiar as DLLs necessárias (Network, Core, Gui, Widgets).
4.  Gera e agrupa os recursos na pasta final formando um `.ZIP` distribuível aos usuários.
