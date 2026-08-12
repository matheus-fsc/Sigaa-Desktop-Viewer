cmake_minimum_required(VERSION 3.24)
project(sigaa-viewer VERSION 0.1.1 LANGUAGES CXX)

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
  src/core/store/Database.cpp
  src/core/sync/DiffEngine.cpp
  src/core/parse/TurmaParser.cpp
  src/core/parse/ArquivoParser.cpp
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
  find_package(Qt6 6.5 QUIET COMPONENTS Widgets Svg)
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
    src/ui/Icones.cpp
    src/ui/Modelos.cpp
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
    sigaa_core sigaa_platform Qt6::Widgets Qt6::Svg)

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
  install(TARGETS sigaa-ui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.matheus_fsc.SigaaDesktopViewer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # O SVG serve a qualquer tamanho e é o mesmo desenho do .qrc — uma fonte,
    # não duas que divergem. O KDE e o GNOME leem SVG em hicolor sem plugin
    # nenhum; o PNG só faria falta num ambiente sem suporte a SVG, que não
    # existe mais entre os alvos deste app.
    install(FILES src/ui/recursos/icones/app.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME io.github.matheus_fsc.SigaaDesktopViewer.svg)

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
    tests/calendario_test.cpp
    tests/aviso_test.cpp
    tests/database_test.cpp
    tests/arquivo_parser_test.cpp
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
