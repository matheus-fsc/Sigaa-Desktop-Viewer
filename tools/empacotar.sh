#!/usr/bin/env bash
# Gera o AppImage do sigaa-viewer em dist/.
#
# POR QUE APPIMAGE, e não um .deb ou um PKGBUILD: o público é aluno de uma
# universidade, e cada um está numa distro diferente. Um arquivo, chmod +x,
# duplo clique — sem repositório, sem sudo, sem conflito de versão de Qt com a
# distro de quem baixou (o Qt vai DENTRO do pacote). Empacotar para os repos é
# melhor para quem já sabe usá-los, e nada impede que exista depois: o
# `cmake --install` que este script usa é o mesmo que um PKGBUILD usaria.
#
# É o irmão Unix do tools/empacotar.ps1, e mantém as mesmas duas recusas:
#
#   - não empacota com teste falhando (--pular-testes se souber o que faz);
#   - não deixa entrar sigaa-viewer.db, relatorio.* ou .env. Esses três nascem
#     no diretório de trabalho de quem roda o app e contêm DADOS PESSOAIS reais
#     (docs/RECON.md §4). Se algum aparecer no AppDir, o script apaga o pacote
#     e falha alto.
#
# Uso:
#   tools/empacotar.sh                    # configura, compila, testa, empacota
#   tools/empacotar.sh --pular-testes
#   tools/empacotar.sh --versao 0.2.0-beta
set -euo pipefail

raiz="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="linux-distro"
versao=""
pular_testes=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)        preset="$2"; shift 2 ;;
        --versao)        versao="$2"; shift 2 ;;
        --pular-testes)  pular_testes=1; shift ;;
        -h|--help)       sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 2 ;;
    esac
done

build="$raiz/build/$preset"
dist="$raiz/dist"
appdir="$dist/AppDir"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/sigaa-viewer"

# A versão sai do project() do CMakeLists, nunca de um literal aqui: duas
# fontes divergem no dia em que alguém sobe a versão num lugar só, e o pacote
# passa a mentir o próprio nome.
if [[ -z "$versao" ]]; then
    versao="$(sed -nE 's/^project\(.*VERSION[[:space:]]+([0-9][0-9.]*).*/\1/p' \
                  "$raiz/CMakeLists.txt" | head -1)"
    [[ -n "$versao" ]] || { echo "não achei a versão em CMakeLists.txt" >&2; exit 1; }
fi

arquitetura="$(uname -m)"
saida="SIGAA-Desktop-Viewer-v$versao-$arquitetura.AppImage"

echo ":: configurando ($preset)"
cmake --preset "$preset" >/dev/null

echo ":: compilando"
cmake --build --preset "$preset" --parallel

if [[ $pular_testes -eq 0 ]]; then
    echo ":: testes"
    ctest --preset "$preset"
else
    echo ":: testes PULADOS (--pular-testes)"
fi

if [[ ! -x "$build/sigaa-ui" ]]; then
    echo "sigaa-ui não foi compilado — o Qt 6 não foi encontrado na configuração." >&2
    echo "Instale qt6-base e qt6-svg (ou configure com -DSIGAA_UI=OFF, mas aí não há o que empacotar)." >&2
    exit 1
fi

echo ":: montando o AppDir"
rm -rf "$appdir" "$dist/$saida"
mkdir -p "$dist"
cmake --install "$build" --prefix "$appdir/usr" >/dev/null

# A recusa que importa. Vem DEPOIS do install e antes do empacotamento, porque
# é o único ponto em que dá para olhar exatamente o que vai ser distribuído.
vazados="$(find "$appdir" \( -name 'sigaa-viewer.db' -o -name 'relatorio.*' \
                             -o -name '.env' -o -name '*.har' \) -print)"
if [[ -n "$vazados" ]]; then
    rm -rf "$appdir"
    echo "ABORTADO: dados pessoais entrariam no pacote:" >&2
    echo "$vazados" >&2
    exit 1
fi

# linuxdeploy: copia as bibliotecas do sistema para dentro do AppDir e escreve
# o AppRun. Fica no cache do usuário, e não no repositório: são ~60 MB de
# binário de terceiros que não têm por que ser versionados.
mkdir -p "$cache"
baixar() {
    local nome="$1" url="$2"
    if [[ ! -x "$cache/$nome" ]]; then
        echo ":: baixando $nome"
        curl -fL --progress-bar -o "$cache/$nome" "$url"
        chmod +x "$cache/$nome"
    fi
}
base="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
baixar "linuxdeploy-$arquitetura.AppImage" "$base/linuxdeploy-$arquitetura.AppImage"

# --- plugins do Qt ---------------------------------------------------------
#
# NÃO usamos o `--plugin qt` do linuxdeploy, e vale explicar: ele copia a pasta
# de plugins INTEIRA do Qt, o que inclui os `kimg_*.so` do kimageformats numa
# máquina com KDE. Um deles (`kimg_jxr.so`) tem dependência opcional que quase
# ninguém instala, o linuxdeploy não acha o .so e aborta o empacotamento —
# ou seja, o plugin falha justamente na distro do desenvolvedor típico deste
# projeto. Além disso ele embarcaria HEIF, JPEG-XL, OpenEXR e RAW num app que
# só desenha ícone SVG.
#
# Listar o que entra é mais código e menos mágica, mas o critério fica visível:
# a plataforma (X11 e Wayland), o iconengine que abre os SVGs do .qrc — sem ele
# a barra de ferramentas fica só com texto — e o formato de imagem SVG.
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[[ -n "$QMAKE" ]] || { echo "qmake6 não encontrado — instale qt6-base." >&2; exit 1; }
qt_plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"

# Grupos inteiros: o Wayland precisa das integrações de shell e de decoração,
# e omitir uma faz a janela abrir sem barra de título em vez de falhar.
for grupo in xcbglintegrations wayland-shell-integration \
             wayland-decoration-client wayland-graphics-integration-client; do
    [[ -d "$qt_plugins/$grupo" ]] || continue
    mkdir -p "$appdir/usr/plugins/$grupo"
    cp -n "$qt_plugins/$grupo"/*.so "$appdir/usr/plugins/$grupo/" 2>/dev/null || true
done

# Arquivos escolhidos a dedo. libqxcb é obrigatório: mesmo numa sessão Wayland
# ele é o caminho de volta pelo XWayland se o resto faltar.
for p in platforms/libqxcb.so \
         platforms/libqoffscreen.so \
         iconengines/libqsvgicon.so \
         imageformats/libqsvg.so \
         imageformats/libqico.so \
         imageformats/libqjpeg.so; do
    [[ -f "$qt_plugins/$p" ]] || continue
    mkdir -p "$appdir/usr/plugins/$(dirname "$p")"
    cp -n "$qt_plugins/$p" "$appdir/usr/plugins/$p"
done

# Wayland por GLOB, e não por nome exato: o Arch empacota `libqwayland.so`, o
# Ubuntu `libqwayland-generic.so` e `libqwayland-egl.so`. Uma lista de nomes
# fixos deixava o Arch de fora em silêncio — o AppImage saía sem Wayland numa
# máquina que tinha o qt6-wayland instalado.
mkdir -p "$appdir/usr/plugins/platforms"
cp -n "$qt_plugins"/platforms/libqwayland*.so "$appdir/usr/plugins/platforms/" 2>/dev/null || true

# Aviso, não erro: sem os plugins de Wayland o AppImage ainda abre — cai no
# XWayland pelo libqxcb. Só que aí ele herda o borrão do XWayland em tela HiDPI,
# que é exatamente o tipo de defeito que o usuário atribui ao app. Quem empacota
# precisa saber que o pacote saiu assim.
if ! compgen -G "$appdir/usr/plugins/platforms/libqwayland*.so" > /dev/null; then
    echo "   aviso: nenhum plugin de Wayland encontrado (pacote qt6-wayland) — o" >&2
    echo "          AppImage vai rodar via XWayland nas sessões Wayland. Instale e" >&2
    echo "          empacote de novo para um pacote completo." >&2
fi

# qt.conf ao lado do executável: é assim que o Qt aprende que os plugins moram
# em ../plugins dentro do AppDir, e não no /usr/lib/qt6 da máquina de quem
# baixou — que pode ter outra versão, ou não ter Qt nenhum.
mkdir -p "$appdir/usr/bin"
printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' > "$appdir/usr/bin/qt.conf"

# --deploy-deps-only: resolve as bibliotecas de que os plugins precisam sem
# tratá-los como executáveis do app.
deps_only=()
while IFS= read -r d; do deps_only+=(--deploy-deps-only "$d"); done \
    < <(find "$appdir/usr/plugins" -mindepth 1 -maxdepth 1 -type d)

# --appimage-extract-and-run: o próprio linuxdeploy é um AppImage, e montá-lo
# exige FUSE. Muitas máquinas (e todo container de CI) não têm; extrair custa
# alguns segundos e funciona em qualquer lugar.
export OUTPUT="$saida"

echo ":: linuxdeploy"
( cd "$dist" && "$cache/linuxdeploy-$arquitetura.AppImage" --appimage-extract-and-run \
    --appdir "$appdir" \
    --output appimage \
    --executable "$appdir/usr/bin/sigaa-ui" \
    --executable "$appdir/usr/bin/sigaa-cli" \
    "${deps_only[@]}" \
    --desktop-file "$appdir/usr/share/applications/io.github.matheus_fsc.SigaaDesktopViewer.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/io.github.matheus_fsc.SigaaDesktopViewer.svg" )

[[ -f "$dist/$saida" ]] || { echo "linuxdeploy não gerou $saida" >&2; exit 1; }
chmod +x "$dist/$saida"
cp "$raiz/.env.example" "$dist/" 2>/dev/null || true

echo
echo "pronto: dist/$saida"
echo "        $(du -h "$dist/$saida" | cut -f1)"
