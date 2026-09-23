<#
.SYNOPSIS
    Gera o pacote distribuivel do sigaa-viewer em dist/.

.NOTES
    POR QUE ESTE SCRIPT EXISTE.

    A arvore do CMake com o gerador do Visual Studio tem ~24 arquivos e pastas
    no topo que nao sao o app: ALL_BUILD.dir, ZERO_CHECK.dir, Continuous.dir,
    Nightly.dir, RUN_TESTS.dir e afins, cada um com quatro subpastas VAZIAS
    (Debug, Release, MinSizeRel, RelWithDebInfo). Somando Debug/ e Release/ com
    binarios de datas diferentes, nao da para olhar build/ e saber o que
    entregar — e a pergunta "qual e o release certo?" nao deveria exigir
    arqueologia.

    Este script responde a pergunta com um caminho so:

        dist/SIGAA-Desktop-Viewer-v<versao>-windows-x64/   (e o .zip ao lado)

    dist/ fica FORA de build/ de proposito: assim o andaime do gerador nunca se
    mistura com o que vai para o usuario, e apagar build/ inteiro nao leva o
    pacote junto.

    O QUE ELE SE RECUSA A FAZER:

      - empacotar com teste falhando (use -PularTestes se souber o que faz);
      - empacotar binario de Debug, que exige as DLLs Qt6*d.dll e nao roda numa
        maquina sem o Qt instalado;
      - copiar sigaa_tests.exe, .pdb, ou qualquer sigaa-viewer.db / relatorio.*
        que tenha sobrado no diretorio de build. O banco e o relatorio contem
        DADOS PESSOAIS reais de quem compilou (docs/RECON.md §4) — eles aparecem
        ali quando alguem roda o app com o diretorio de trabalho no build, o que
        acontece o tempo todo ao depurar pela IDE.

.PARAMETER Versao
    Sobrescreve a versao do nome da pasta. O padrao vem do project() do
    CMakeLists.txt, para o nome nunca discordar do que foi compilado.

.PARAMETER PularTestes
    Empacota sem rodar o ctest. Existe para iterar no proprio script.

.EXAMPLE
    pwsh -File tools/empacotar.ps1
    pwsh -File tools/empacotar.ps1 -Versao 0.2.0-beta
#>

[CmdletBinding()]
param(
    [string]$Preset = 'windows',
    [string]$Versao,
    [string]$Sufixo = 'windows-x64',
    [switch]$PularTestes
)

$ErrorActionPreference = 'Stop'

$raiz = Split-Path -Parent $PSScriptRoot
$build = Join-Path $raiz "build/$Preset"

if (-not (Test-Path $build)) {
    throw "diretorio de build nao encontrado: $build`nRode antes: cmake --preset $Preset"
}

# Versao do CMakeLists, nao um literal aqui: duas fontes divergem no dia em que
# alguem sobe a versao num lugar so, e o pacote passa a mentir o proprio nome.
if (-not $Versao) {
    $cm = Get-Content (Join-Path $raiz 'CMakeLists.txt') -Raw
    if ($cm -match 'project\([^)]*VERSION\s+([0-9][0-9.]*)') {
        $Versao = $Matches[1]
    } else {
        throw 'nao achei VERSION no project() do CMakeLists.txt; passe -Versao'
    }
}

$nome = "SIGAA-Desktop-Viewer-v$Versao-$Sufixo"
$dist = Join-Path $raiz 'dist'
$destino = Join-Path $dist $nome
$zip = "$destino.zip"

Write-Host "== compilando Release ==" -ForegroundColor Cyan
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'a compilacao Release falhou' }

if (-not $PularTestes) {
    Write-Host "== testes (Release) ==" -ForegroundColor Cyan
    Push-Location $build
    try {
        ctest -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw 'teste falhando — nada foi empacotado. Use -PularTestes para forcar.'
        }
    } finally {
        Pop-Location
    }
}

$origem = Join-Path $build 'Release'
foreach ($exe in 'sigaa-ui.exe', 'sigaa-cli.exe') {
    if (-not (Test-Path (Join-Path $origem $exe))) {
        throw "$exe nao esta em $origem — a compilacao Release nao produziu o binario"
    }
}

Write-Host "== montando $nome ==" -ForegroundColor Cyan
if (Test-Path $destino) { Remove-Item -Recurse -Force $destino }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force -Path $destino | Out-Null

# Lista de EXCLUSAO, e nao de inclusao, para as DLLs: o windeployqt decide
# quais plugins o Qt precisa, e uma lista fixa aqui ficaria desatualizada em
# silencio na primeira vez que o Qt mudasse de dependencia — o app so quebraria
# na maquina do usuario, que e onde nao da para depurar.
$proibidos = @('sigaa_tests.exe', 'sigaa_core.lib', 'sigaa_platform.lib')

Get-ChildItem $origem | ForEach-Object {
    if ($_.PSIsContainer) {
        Copy-Item $_.FullName -Destination $destino -Recurse
        return
    }
    if ($proibidos -contains $_.Name) { return }
    if ($_.Extension -in '.pdb', '.ilk', '.exp', '.lib') { return }
    # Dados de quem compilou. Ver as notas do topo.
    if ($_.Name -eq 'sigaa-viewer.db' -or $_.Name -like 'relatorio.*') { return }
    Copy-Item $_.FullName -Destination $destino
}

Copy-Item (Join-Path $raiz 'README.md') -Destination $destino
Copy-Item (Join-Path $raiz '.env.example') -Destination $destino

# Ultima linha de defesa: mesmo que a regra acima mude, nada com cara de dado
# pessoal sai daqui. Falhar alto e melhor do que publicar um zip com o banco de
# alguem dentro.
$vazamento = Get-ChildItem $destino -Recurse -File |
    Where-Object { $_.Name -eq 'sigaa-viewer.db' -or $_.Name -like 'relatorio.*' -or $_.Extension -eq '.env' }
if ($vazamento) {
    Remove-Item -Recurse -Force $destino
    throw "arquivo com dados pessoais entrou no pacote: $($vazamento.Name). Pacote descartado."
}

Compress-Archive -Path "$destino/*" -DestinationPath $zip

# ---------------------------------------------------------------------------
# Soma de verificacao.
#
# NAO e enfeite: e o que o atualizador automatico do app confere antes de
# trocar os proprios arquivos (core/atualizacao/Atualizador.h). Sem ela o app
# se recusa a instalar -- e a recusa e o comportamento certo, porque uma
# atualizacao automatica que nao verifica nada e o melhor alvo que um programa
# pode oferecer.
#
# Mesmo formato do sha256sum do Unix ("<hex>  <nome>"), porque o leitor do lado
# do app e um so para as duas plataformas.
# ---------------------------------------------------------------------------
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$linha = "$hash  " + (Split-Path $zip -Leaf)
# Sem BOM: o leitor compara os 64 primeiros caracteres da linha, e um BOM os
# desloca em tres bytes -- a soma nunca bateria, e a mensagem de erro falaria
# de um arquivo adulterado que esta intacto.
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS'), "$linha`n",
                        (New-Object Text.UTF8Encoding $false))
Write-Host "somas : $(Join-Path $dist 'SHA256SUMS')" -ForegroundColor Green

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ''
Write-Host "pacote: $destino" -ForegroundColor Green
Write-Host "zip   : $zip ($mb MB)" -ForegroundColor Green
Write-Host ''
Write-Host 'para testar num ambiente limpo, copie a pasta para uma maquina sem Qt'
Write-Host 'instalado e abra o sigaa-ui.exe: e assim que o usuario final recebe.'
