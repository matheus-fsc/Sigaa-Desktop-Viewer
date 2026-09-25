cmake_minimum_required(VERSION 3.24)
project(sigaa-viewer VERSION 1.0.0 LANGUAGES CXX)

# O sufixo de pré-lançamento, que o `project(VERSION)` não aceita — ele só
# entende número.
#
# EXISTE PARA CASAR COM A TAG. Até a v0.1.2 a tag era "v0.1.2-alpha" e o CMake
# dizia "0.1.2", e pelo semver "0.1.2" é MAIS NOVA que "0.1.2-alpha" — o
# atualizador (core/atualizacao/Atualizador.h) via o AppImage da release como
# mais recente que a própria release e nunca ofereceria nada. Mudar um lado sem
# o outro traz o problema de volta.
#
# É CACHE, então um diretório de build ANTIGO mantém o valor que tinha quando
# foi configurado: quem vinha da 0.2.0-alpha e só recompila fica com um binário
# que se diz "1.0.0-alpha" enquanto o pacote ao lado se chama "1.0.0". Ao subir
# a versão, reconfigure com `-DSIGAA_SUFIXO=` ou apague o build. O CI configura
# do zero, então lá isso não acontece.
set(SIGAA_SUFIXO "" CACHE STRING "sufixo de pré-lançamento; vazio numa final")
if(SIGAA_SUFIXO)
  set(SIGAA_VERSAO_COMPLETA "${PROJECT_VERSION}-${SIGAA_SUFIXO}")
else()
  set(SIGAA_VERSAO_COMPLETA "${PROJECT_VERSION}")
endif()

# --- coerência da versão ---------------------------------------------------
#
# O .rc do Windows carrega a versão em texto, porque o formato VERSIONINFO não
# sabe ler uma variável do CMake. Duas fontes, e a segunda já ficou para trás
# uma vez: o arquivo dizia 0.3.0.0 enquanto o projeto era 0.2.0, e nada quebrou
# — só a aba Detalhes do Explorer passou a mentir, onde ninguém olha.
#
# Conferir em TODAS as plataformas, e não só no Windows: quem sobe a versão faz
# isso no Linux, e um aviso que só aparece na máquina de outra pessoa não é
# aviso. Falha na configuração, antes de compilar qualquer coisa.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/recursos/sigaa-ui.rc" _rc)
string(REPLACE "." ", " _versao_rc "${PROJECT_VERSION}, 0")
if(NOT _rc MATCHES "FILEVERSION ${_versao_rc}")
  message(FATAL_ERROR
    "src/ui/recursos/sigaa-ui.rc está desatualizado: o projeto é "
    "${PROJECT_VERSION} e o .rc não traz FILEVERSION ${_versao_rc}. "
    "Atualize FILEVERSION, PRODUCTVERSION, FileVersion e ProductVersion lá.")
endif()
if(NOT _rc MATCHES "\"${PROJECT_VERSION}\\.0\"")
  message(FATAL_ERROR
    "src/ui/recursos/sigaa-ui.rc: as strings FileVersion/ProductVersion não "
    "dizem ${PROJECT_VERSION}.0.")
endif()
unset(_rc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# DUAS PROCEDÊNCIAS POSSÍVEIS PARA A MESMA BIBLIOTECA, e é por isso que cada
# find_package abaixo tem plano B: no Windows as dependências vêm do vcpkg, que
# exporta arquivos de CONFIG próprios; no Linux elas vêm do gerenciador de
# pacotes da distro, e quem sabe encontrá-las são os módulos Find* que já vêm
# com o CMake. Exigir CONFIG nas duas plataformas era o que fazia
# `cmake -S . -B build` falhar num Arch com libcurl instalada — a biblioteca
# está lá, o arquivo que o vcpkg escreve é que não.
#
# Os alvos importados diferem entre os dois caminhos; o nome resolvido vai para
# uma variável e só ela aparece no target_link_libraries.
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
  find_package(CURL MODULE REQUIRED)   # FindCURL do CMake -> CURL::libcurl
endif()

find_package(unofficial-sqlite3 CONFIG QUIET)   # nome que só o vcpkg usa
if(unofficial-sqlite3_FOUND)
  set(SIGAA_SQLITE unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 MODULE REQUIRED)
  # O FindSQLite3 renomeou o alvo: SQLite::SQLite3 até o CMake 3.30, e
  # SQLite3::SQLite3 daí em diante (o antigo continua existindo, mas avisa que
  # está obsoleto em toda configuração). Perguntar qual existe é mais barato do
  # que amarrar o projeto a uma versão de CMake.
  if(TARGET SQLite3::SQLite3)
    set(SIGAA_SQLITE SQLite3::SQLite3)
  else()
    set(SIGAA_SQLITE SQLite::SQLite3)
  endif()
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# lexbor não exporta config em todas as portas do vcpkg nem nas distros;
# resolver na mão. No Arch/CachyOS ela vem do AUR (`lexbor`), não dos repos
# oficiais — é a única dependência que precisa disso.
find_path(LEXBOR_INCLUDE_DIR lexbor/html/html.h REQUIRED)
find_library(LEXBOR_LIBRARY NAMES lexbor lexbor_static REQUIRED)

# ---------------------------------------------------------------------------
# core/ — SEM dependência de UI, testável headless.
# Esta é a regra de ouro do projeto: nada de Qt/wx aqui dentro.
# ---------------------------------------------------------------------------
add_library(sigaa_core STATIC
  src/core/parse/Html.cpp
  src/core/jsf/JsfForm.cpp
  src/core/http/SigaaSession.cpp
  src/core/http/Trafego.cpp
  src/core/parse/PortalParser.cpp
  src/core/report/HtmlReport.cpp
  src/core/report/TurmaMd.cpp
  src/core/store/Database.cpp
  src/core/sync/DiffEngine.cpp
  src/core/parse/TurmaParser.cpp
  src/core/http/CadeiaAia.cpp
  src/core/http/SessaoViva.cpp
  src/core/parse/ArquivoParser.cpp
  src/core/parse/FrequenciaParser.cpp
  src/core/parse/ParticipanteParser.cpp
  src/core/atualizacao/Atualizador.cpp
  src/core/atualizacao/Sha256.cpp
  src/core/avaliacao/Ajustes.cpp
  src/core/frequencia/Presenca.cpp
  src/core/calendar/Calendario.cpp
  src/core/sync/Crawler.cpp
  src/core/sync/Materiais.cpp
  src/core/sync/Baixador.cpp
  src/core/notify/Aviso.cpp
  src/core/servico/Servico.cpp
  src/core/config/DotEnv.cpp
  src/core/config/Instituicao.cpp
  src/core/util/Caminho.cpp
)

target_include_directories(sigaa_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE ${LEXBOR_INCLUDE_DIR}
)

target_link_libraries(sigaa_core
  PUBLIC  nlohmann_json::nlohmann_json
  PRIVATE CURL::libcurl ${LEXBOR_LIBRARY} spdlog::spdlog ${SIGAA_SQLITE}
)

# A versão do projeto chega ao código por AQUI, e não por uma constante
# repetida num .h: duas fontes divergem, e a que mente é sempre a que ninguém
# lembra de atualizar. É o número que o atualizador compara com a tag da
# release (core/atualizacao/Atualizador.h).
target_compile_definitions(sigaa_core PUBLIC SIGAA_VERSAO="${SIGAA_VERSAO_COMPLETA}")

# ---------------------------------------------------------------------------
# platform/ — a única parte que fala com o SO. Fica FORA de core/ de propósito:
# core/ continua compilando e sendo testado em qualquer lugar, headless.
# ---------------------------------------------------------------------------
if(WIN32)
  set(SIGAA_PLATFORM_SRC src/platform/Notify_win.cpp src/platform/Credenciais_win.cpp)
else()
  set(SIGAA_PLATFORM_SRC src/platform/Notify_posix.cpp src/platform/Credenciais_posix.cpp)
endif()

add_library(sigaa_platform STATIC
  ${SIGAA_PLATFORM_SRC}
  src/platform/Credenciais.cpp
)
target_include_directories(sigaa_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# platform -> core, nunca o contrario. A resolucao de credenciais precisa do
# leitor de .env, e core/ continua sem saber que platform/ existe.
target_link_libraries(sigaa_platform PUBLIC sigaa_core)
if(WIN32)
  # advapi32: CredWriteW/CredReadW/CredDeleteW (Gerenciador de Credenciais).
  target_link_libraries(sigaa_platform PRIVATE shell32 user32 advapi32)
  # Sem UNICODE, as macros TCHAR do SDK viram as versoes ANSI e IDI_WARNING
  # nao compila contra LoadIconW. Definir aqui tambem impede que uma API ANSI
  # entre por acidente e estrague acento em nome de turma.
  target_compile_definitions(sigaa_platform PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# sigaa-cli — entregável da Fase 1. Prova que o scraping funciona,
# sem escrever uma linha de UI.
# ---------------------------------------------------------------------------
add_executable(sigaa-cli
  src/app/main.cpp
  src/app/Prompt.cpp
)
target_link_libraries(sigaa-cli PRIVATE sigaa_core sigaa_platform spdlog::spdlog)

if(MSVC)
  # F5 na IDE precisa de argumentos, senão o CLI só imprime o "uso:" e sai.
  # Fica no CMake (e não na IDE) para sobreviver à regeneração do projeto.
  set_target_properties(sigaa-cli PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS
      "post \"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/portal_min.html\" ADMINISTRACAO"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
               PROPERTY VS_STARTUP_PROJECT sigaa-cli)
endif()

# ---------------------------------------------------------------------------
# sigaa-ui — Fase 3. Qt 6 Widgets.
#
# OPCIONAL DE PROPOSITO. Qt nao vem do vcpkg (compilar do fonte leva horas), e
# quem so quer rodar os testes do parser nao deveria ser obrigado a instalar
# 1,5 GB. Sem Qt, o CMake avisa e segue — sigaa-cli e os testes continuam
# compilando. Ver README, secao "Interface grafica".
#
# Aponte o CMake para o Qt com CMAKE_PREFIX_PATH (o CMakeUserPresets.json,
# que e ignorado pelo git, e o lugar certo para esse caminho de maquina).
# ---------------------------------------------------------------------------
option(SIGAA_UI "Compila a interface grafica (exige Qt 6)" ON)

if(SIGAA_UI)
  # Svg nao e enfeite: os icones da interface sao SVG dentro do .qrc, e quem
  # ensina o QIcon a abrir SVG e o plugin iconengine deste modulo. Sem ele o
  # QIcon devolve pixmap nulo e a barra de ferramentas fica so com texto.
  # Linkar tambem e o que faz o windeployqt copiar o plugin para junto do .exe.
  find_package(Qt6 6.5 QUIET COMPONENTS Widgets Svg Network)
endif()

if(SIGAA_UI AND NOT Qt6_FOUND)
  message(STATUS
    "sigaa-ui: Qt6 nao encontrado, pulando a interface. "
    "Defina CMAKE_PREFIX_PATH para a pasta do Qt (ex.: C:/Qt/6.8.3/msvc2022_64) "
    "ou configure com -DSIGAA_UI=OFF para silenciar este aviso.")
elseif(SIGAA_UI)
  add_executable(sigaa-ui
    src/ui/main.cpp
    src/ui/JanelaPrincipal.cpp
    src/ui/CalendarioProvas.cpp
    src/ui/DialogoLogin.cpp
    src/ui/JanelaTurma.cpp
    src/ui/JanelaDiagnostico.cpp
    src/ui/DialogoAtualizar.cpp
    src/ui/DialogoOpcoes.cpp
    src/ui/DialogosAvaliacao.cpp
    src/ui/Copiar.cpp
    src/ui/Distintivos.cpp
    src/ui/Icones.cpp
    src/ui/InstanciaUnica.cpp
    src/ui/Modelos.cpp
    src/ui/Tema.cpp
    src/ui/Trabalhador.cpp

    # Os .ui nao geram objeto: entram na lista para o AUTOUIC enxerga-los como
    # dependencia e o Visual Studio mostra-los na arvore do projeto (abrir com
    # duplo clique no Qt Designer).
    src/ui/forms/JanelaPrincipal.ui
    src/ui/forms/DialogoLogin.ui
    src/ui/forms/JanelaTurma.ui
    src/ui/forms/JanelaDiagnostico.ui

    src/ui/recursos/recursos.qrc
  )
  # AUTOMOC/AUTOUIC/AUTORCC no alvo, e nao via qt_standard_project_setup():
  # aquela funcao tambem mexe em CMAKE_CXX_STANDARD no escopo inteiro, e o
  # padrao do projeto (C++20) ja esta definido no topo deste arquivo.
  #
  # AUTOUIC_SEARCH_PATHS e obrigatorio porque os forms moram em src/ui/forms/ e
  # nao ao lado do .cpp que os inclui; sem isto o AUTOUIC procura o .ui so na
  # pasta do fonte e falha com "failed to find file".
  set_target_properties(sigaa-ui PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/forms"
    AUTORCC ON)
  target_link_libraries(sigaa-ui PRIVATE
    sigaa_core sigaa_platform Qt6::Widgets Qt6::Svg Qt6::Network)

  if(WIN32)
    # WIN32_EXECUTABLE tira o console preto que apareceria atras da janela.
    # Consequencia: std::cerr some. A UI nao deve depender de stderr para nada
    # que o usuario precise ver — mensagem de erro vai para a interface.
    set_target_properties(sigaa-ui PROPERTIES WIN32_EXECUTABLE ON)

    # Icone e metadados do .exe. Isto e o que o Explorer, a barra de tarefas e
    # o Alt+Tab leem — o .qrc so existe depois que o processo subiu, tarde
    # demais para essas tres coisas. Regravar com tools/gerar_icone.py.
    target_sources(sigaa-ui PRIVATE src/ui/recursos/sigaa-ui.rc)

    # Copia as DLLs do Qt para junto do .exe. Sem isto o binario so roda com o
    # bin/ do Qt no PATH, e da erro de DLL ausente ao abrir pelo Explorer —
    # que e exatamente como o usuario final vai abrir.
    find_program(SIGAA_WINDEPLOYQT windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(SIGAA_WINDEPLOYQT)
      add_custom_command(TARGET sigaa-ui POST_BUILD
        COMMAND "${SIGAA_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                --no-opengl-sw "$<TARGET_FILE:sigaa-ui>"
        COMMENT "windeployqt: copiando as DLLs do Qt")
    endif()
  endif()

  if(APPLE)
    # O entregavel do macOS e um BUNDLE, e nao um executavel solto: o Finder so
    # mostra nome e icone decentes para um .app, o Dock so aceita um .app, e o
    # macdeployqt (que copia os frameworks do Qt para dentro) recusa qualquer
    # outra coisa. Um binario solto abriria sem icone, com o nome do arquivo na
    # barra de menus, e so a partir do terminal.
    set_target_properties(sigaa-ui PROPERTIES
      MACOSX_BUNDLE ON
      OUTPUT_NAME "SIGAA Viewer"
      MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "SIGAA Viewer"
      MACOSX_BUNDLE_EXECUTABLE_NAME "SIGAA Viewer"
      MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.matheus-fsc.SigaaDesktopViewer"
      MACOSX_BUNDLE_ICON_FILE "app.icns"
      # CFBundleShortVersionString aceita sufixo; CFBundleVersion e so numero, e
      # o macOS recusa o bundle se vier "0.2.0-alpha" ali.
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${SIGAA_VERSAO_COMPLETA}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # O .icns entra como RECURSO do bundle. O Info.plist so aponta o NOME do
    # arquivo — se ele nao estiver em Contents/Resources, o app abre com o
    # icone generico e nenhum erro em lugar nenhum.
    target_sources(sigaa-ui PRIVATE src/ui/recursos/app.icns)
    set_source_files_properties(src/ui/recursos/app.icns PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources)
  endif()

  if(MSVC)
    set_target_properties(sigaa-ui PROPERTIES
      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()

# ---------------------------------------------------------------------------
# instalação — usada pelo `cmake --install` e, por tabela, pelo AppImage.
#
# No Windows o entregável é a pasta que o tools/empacotar.ps1 monta, e não um
# prefixo de instalação; estas regras existem para o mundo Unix, onde o
# empacotador (AppImage, .deb, PKGBUILD) espera achar o app num prefixo com a
# hierarquia do freedesktop: binário em bin/, .desktop em share/applications e
# ícone em share/icons/hicolor. O linuxdeploy lê exatamente esses três lugares
# para montar o AppDir — sem eles ele monta um AppImage sem nome nem ícone.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS sigaa-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(TARGET sigaa-ui)
  # BUNDLE DESTINATION e obrigatorio quando o alvo e um .app: sem ele o CMake
  # recusa o install no macOS com "no BUNDLE DESTINATION". Nas outras
  # plataformas a palavra e simplesmente ignorada.
  install(TARGETS sigaa-ui
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          BUNDLE  DESTINATION .)

  if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.matheus_fsc.SigaaDesktopViewer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # PNG em cada tamanho do hicolor, e NÃO só o SVG.
    #
    # O ícone do app é um desenho raster (build a partir de um PNG de 1254 px),
    # então não há SVG dele para instalar em `scalable`. E mesmo houvesse: o
    # hicolor com tamanhos fixos é o que a barra de tarefas do KDE e do GNOME
    # consulta primeiro, e um desenho ajustado para 16 px é legível onde a
    # redução automática de um vetor vira mancha.
    foreach(TAM 16 24 32 48 64 128 256 512)
      install(FILES src/ui/recursos/icones/app/app-${TAM}.png
              DESTINATION
                ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${TAM}x${TAM}/apps
              RENAME io.github.matheus_fsc.SigaaDesktopViewer.png)
    endforeach()

    # NÃO instalamos AppStream (share/metainfo) ainda, de propósito: o formato
    # exige um <project_license> e este repositório não tem arquivo de licença.
    # Declarar uma ali seria inventar um fato jurídico. Quando houver LICENSE,
    # o metainfo é o que faz o app aparecer com descrição e captura de tela no
    # Discover do KDE e no GNOME Software.
  endif()
endif()

# ---------------------------------------------------------------------------
# testes — rodam contra fixtures HTML, nunca contra o site ao vivo
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  find_package(Catch2 3 CONFIG REQUIRED)
  add_executable(sigaa_tests
    tests/jsf_form_test.cpp
    tests/session_test.cpp
    tests/html_encoding_test.cpp
    tests/diff_engine_test.cpp
    tests/ajustes_test.cpp
    tests/turma_md_test.cpp
    tests/cadeia_aia_test.cpp
    tests/atualizador_test.cpp
    tests/atualizador_windows_test.cpp
    tests/sha256_test.cpp
    tests/presenca_test.cpp
    tests/sessao_viva_test.cpp
    tests/frequencia_parser_test.cpp
    tests/calendario_test.cpp
    tests/aviso_test.cpp
    tests/database_test.cpp
    tests/arquivo_parser_test.cpp
    tests/participante_parser_test.cpp
    tests/topico_parser_test.cpp
    tests/baixador_test.cpp
    tests/instituicao_test.cpp
  )
  target_link_libraries(sigaa_tests PRIVATE sigaa_core Catch2::Catch2WithMain)
  target_compile_definitions(sigaa_tests PRIVATE
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  include(Catch)
  catch_discover_tests(sigaa_tests)
endif()
