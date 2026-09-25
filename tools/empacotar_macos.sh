#!/usr/bin/env bash
# Gera o .dmg do SIGAA Viewer em dist/.
#
# É o terceiro irmão de tools/empacotar.sh (AppImage) e tools/empacotar.ps1
# (zip do Windows), e mantém as mesmas duas recusas:
#
#   - não empacota com teste falhando (--pular-testes se souber o que faz);
#   - não deixa entrar sigaa-viewer.db, relatorio.*, .env ou .har. Esses nascem
#     no diretório de trabalho de quem roda o app e contêm DADOS PESSOAIS reais
#     (docs/RECON.md §4). Se algum aparecer no bundle, o script apaga o pacote
#     e falha alto.
#
# POR QUE .dmg, e não um .pkg: instalar no Mac é arrastar para Aplicativos.
# Um instalador pediria senha de administrador para copiar uma pasta, e pedir
# senha de admin é exatamente o hábito que este app não deve ensinar a quem o
# usa para olhar as próprias faltas.
#
# SEM ASSINATURA E SEM NOTARIZAÇÃO: não há conta de desenvolvedor Apple neste
# projeto. O Gatekeeper vai barrar a primeira abertura, e o contorno é clicar
# com o botão direito > Abrir. O README explica; o script NÃO finge que o
# problema não existe, porque quem baixa descobre do jeito pior.
#
# Uso (só roda no macOS — precisa do macdeployqt e do hdiutil):
#   tools/empacotar_macos.sh
#   tools/empacotar_macos.sh --versao 1.0.0
#   tools/empacotar_macos.sh --pular-testes
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "este script só roda no macOS (precisa de macdeployqt e hdiutil)." >&2
    exit 1
fi

raiz="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$raiz/build/macos"
dist="$raiz/dist"
versao=""
pular_testes=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --versao)       versao="$2"; shift 2 ;;
        --pular-testes) pular_testes=1; shift ;;
        -h|--help)      sed -n '2,28p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 2 ;;
    esac
done

# A versão sai do project() do CMakeLists, nunca de um literal aqui: duas
# fontes divergem no dia em que alguém sobe a versão num lugar só, e o pacote
# passa a mentir o próprio nome.
if [[ -z "$versao" ]]; then
    versao="$(sed -nE 's/^project\(.*VERSION[[:space:]]+([0-9][0-9.]*).*/\1/p' \
        "$raiz/CMakeLists.txt" | head -1)"
    [[ -n "$versao" ]] || { echo "não achei a versão em CMakeLists.txt" >&2; exit 1; }
    sufixo="$(sed -nE 's/^set\(SIGAA_SUFIXO "([^"]*)".*/\1/p' \
        "$raiz/CMakeLists.txt" | head -1)"
    [[ -z "$sufixo" ]] || versao="$versao-$sufixo"
fi

# arm64 nos Apple Silicon, x86_64 nos Intel. O nome do arquivo diz qual é: um
# .dmg arm64 aberto num Intel falha com "não pode ser aberto", e sem o sufixo
# ninguém sabe qual baixar.
arquitetura="$(uname -m)"
saida="SIGAA-Desktop-Viewer-v$versao-macos-$arquitetura.dmg"
app="SIGAA Viewer.app"

echo ":: configurando"
cmake -S "$raiz" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo ":: compilando"
cmake --build "$build" --parallel

if [[ $pular_testes -eq 0 ]]; then
    echo ":: testes"
    ctest --test-dir "$build" --output-on-failure
else
    echo ":: testes PULADOS (--pular-testes)"
fi

[[ -d "$build/$app" ]] || {
    echo "$app não foi gerado — o Qt 6 não foi encontrado na configuração." >&2
    echo "Instale o Qt (brew install qt) ou aponte CMAKE_PREFIX_PATH para ele." >&2
    exit 1
}

echo ":: montando o bundle"
rm -rf "$dist/estagio" "$dist/$saida"
mkdir -p "$dist/estagio"
cp -a "$build/$app" "$dist/estagio/"

# O sigaa-cli vai JUNTO, dentro do bundle. Ele é o que gera relatório e roda a
# coleta sem interface, e deixá-lo de fora faria a versão do Mac ser a única
# sem essa metade do projeto. Em Contents/MacOS porque é a única pasta do
# bundle de onde um executável roda sem o macOS reclamar.
cp "$build/sigaa-cli" "$dist/estagio/$app/Contents/MacOS/"

# macdeployqt: copia os frameworks do Qt e os plugins para dentro do .app e
# reescreve os caminhos das bibliotecas. Sem ele o app só abre numa máquina que
# tenha o Qt instalado no mesmo lugar — ou seja, só na de quem compilou.
macdeployqt="$(command -v macdeployqt || true)"
if [[ -z "$macdeployqt" ]]; then
    for p in "${QT_ROOT_DIR:-}/bin/macdeployqt" /opt/homebrew/opt/qt/bin/macdeployqt \
             /usr/local/opt/qt/bin/macdeployqt; do
        [[ -x "$p" ]] && { macdeployqt="$p"; break; }
    done
fi
[[ -n "$macdeployqt" ]] || { echo "macdeployqt não encontrado." >&2; exit 1; }

echo ":: macdeployqt"
"$macdeployqt" "$dist/estagio/$app" -always-overwrite

# A recusa que importa. Depois do deploy e antes de fechar a imagem, porque é o
# único ponto em que dá para olhar exatamente o que vai ser distribuído.
vazados="$(find "$dist/estagio" \( -name 'sigaa-viewer.db' -o -name 'relatorio.*' \
                                   -o -name '.env' -o -name '*.har' \) -print)"
if [[ -n "$vazados" ]]; then
    rm -rf "$dist/estagio"
    echo "ABORTADO: dados pessoais entrariam no pacote:" >&2
    echo "$vazados" >&2
    exit 1
fi

# Assinatura ad-hoc (`-`), que não é a assinatura de um desenvolvedor Apple: ela
# não tira o aviso do Gatekeeper. Serve para outra coisa, e obrigatória nos
# Apple Silicon: um binário arm64 sem NENHUMA assinatura é morto pelo sistema
# ao abrir. Depois do macdeployqt, que reescreve as bibliotecas e invalidaria
# qualquer assinatura feita antes.
echo ":: assinatura ad-hoc"
codesign --force --deep --sign - "$dist/estagio/$app"

# O atalho para /Applications dentro da imagem é o que torna "arraste para cá"
# uma instrução e não um palpite.
ln -s /Applications "$dist/estagio/Applications"

echo ":: dmg"
hdiutil create -volname "SIGAA Viewer" -srcfolder "$dist/estagio" \
    -ov -format UDZO "$dist/$saida" >/dev/null
rm -rf "$dist/estagio"

# ---------------------------------------------------------------------------
# Somas de verificação.
#
# NÃO é enfeite: é o que o atualizador do app confere antes de abrir o pacote
# baixado (core/atualizacao/Atualizador.h). Sem elas o app baixaria um arquivo
# do GitHub sob a palavra do DNS.
#
# `shasum -a 256`, e não `sha256sum`: o coreutils do GNU não vem no macOS. O
# formato de saída é o mesmo, que é o que o publicador junta com os das outras
# plataformas.
# ---------------------------------------------------------------------------
( cd "$dist" && shasum -a 256 "$saida" > SHA256SUMS )

echo
echo "pronto:"
echo "  dist/$saida  ($(du -h "$dist/$saida" | cut -f1))"
echo "  dist/SHA256SUMS"
echo
echo "AVISO: o pacote não é notarizado. Na primeira abertura o macOS vai dizer"
echo "que o app não pôde ser verificado — abrir com o botão direito > Abrir."
