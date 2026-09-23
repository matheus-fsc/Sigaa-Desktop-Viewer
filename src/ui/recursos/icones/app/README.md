# Ícone do aplicativo

`original-1254.png` é o mestre. Todo o resto é derivado dele e pode ser
regerado — não edite os tamanhos à mão, porque a próxima regeração apagaria
o ajuste.

```sh
cd src/ui/recursos/icones
for t in 16 24 32 48 64 128 256 512; do
  magick app/original-1254.png -resize ${t}x${t} -strip \
         -define png:compression-level=9 app/app-${t}.png
done

# .ico do Windows, montado a partir dos tamanhos já prontos.
# Gerar direto do mestre com `icon:auto-resize` produz um arquivo 3x maior:
# as camadas grandes saem como BMP sem compressão.
magick app/app-16.png app/app-32.png app/app-48.png \
       app/app-64.png app/app-128.png app/app-256.png -colors 256 app.ico
```

## Por que tantos tamanhos

O sistema pede o ícone em resoluções diferentes conforme o lugar: 16 px na
barra de título, 22 na bandeja, 48 no alt-tab, 256 no gerenciador de arquivos.
Deixar o Qt reduzir um desenho de 256 px para 16 px borra os traços finos do
capelo a ponto de virar uma mancha — cada tamanho aqui foi reduzido do mestre,
que tem detalhe de sobra para todos.

O `.ico` para de crescer em 256 px: o formato não define nada acima disso, e o
Windows usa o maior que encontrar.

## Onde cada um é usado

| arquivo | quem lê |
|---|---|
| `app-16` … `app-256` (no `.qrc`) | `ui::iconeApp()`, em tempo de execução |
| `app-16` … `app-512` (instalados) | `share/icons/hicolor` no Linux, lido pelo KDE e pelo GNOME |
| `app.ico` | `packaging/windows/sigaa-ui.rc`, que dá ícone ao `.exe` |

`app.svg` ao lado é o desenho ANTERIOR, mantido só porque ainda está no `.qrc`.
Nada o carrega desde que `iconeApp()` passou a montar o ícone dos PNGs.
