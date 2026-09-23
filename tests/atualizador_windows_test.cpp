// O auxiliar de atualizacao do Windows.
//
// POR QUE ESTE ARQUIVO EXISTE NUMA SUITE QUE RODA NO LINUX: o script .cmd e a
// peca mais delicada da atualizacao do Windows, e quem escreve o projeto nao
// tem Windows a mao. Se ele ficasse dentro de um `#ifdef _WIN32`, nao seria
// nem COMPILADO aqui — e o primeiro a descobrir um erro de aspas seria um
// usuario com o app pela metade.
//
// `scriptDeTrocaWindows` e uma funcao pura, compilada em toda plataforma
// justamente para poder ser conferida aqui. O que fica atras do `#ifdef` e so
// gravar o arquivo e disparar o processo.
//
// O QUE ESTES TESTES PROTEGEM, em uma frase: um caminho com espaco
// ("C:\Program Files\...") sem aspas vira dois argumentos, e o script apagaria
// ou copiaria a pasta errada.
//
// Nomes de TEST_CASE em ASCII (ver jsf_form_test.cpp).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/atualizacao/Atualizador.h"

using sigaa::atualizacao::scriptDeTrocaWindows;

namespace {

std::string script() {
    return scriptDeTrocaWindows(
        4242, R"(C:\Users\Aluno\AppData\Local\Temp\pacote.zip)",
        R"(C:\Users\Aluno\AppData\Local\Temp\sigaa-update-4242)",
        R"(C:\Program Files\SIGAA Viewer)",
        R"(C:\Program Files\SIGAA Viewer\sigaa-ui.exe)");
}

bool contem(const std::string& s, const std::string& agulha) {
    return s.find(agulha) != std::string::npos;
}

}  // namespace

TEST_CASE("todo caminho sai entre aspas", "[atualizador][windows]") {
    // O teste central. "C:\Program Files\SIGAA Viewer" sem aspas vira
    // "C:\Program" mais "Files\SIGAA Viewer", e o robocopy copiaria para um
    // lugar que nao existe — ou pior, o rmdir apagaria o que nao devia.
    const std::string s = script();
    CHECK(contem(s, R"(set "DEST=C:\Program Files\SIGAA Viewer")"));
    CHECK(contem(s, R"(set "EXE=C:\Program Files\SIGAA Viewer\sigaa-ui.exe")"));
    CHECK(contem(s, R"(set "ZIP=C:\Users\Aluno\AppData\Local\Temp\pacote.zip")"));
    // E no uso, nao so na atribuicao.
    CHECK(contem(s, R"(robocopy "%TEMP_DIR%" "%DEST%")"));
    CHECK(contem(s, R"(tar -xf "%ZIP%" -C "%TEMP_DIR%")"));
    CHECK(contem(s, R"(start "" "%EXE%")"));
}

TEST_CASE("espera o app sair antes de tocar em arquivo", "[atualizador][windows]") {
    // Sem a espera, o robocopy encontraria as DLLs do Qt ainda carregadas e
    // falharia arquivo por arquivo — deixando a instalacao pela metade, que e
    // o pior estado possivel.
    const std::string s = script();
    const auto espera = s.find("goto esperar");
    const auto extrai = s.find("tar -xf");
    REQUIRE(espera != std::string::npos);
    REQUIRE(extrai != std::string::npos);
    CHECK(espera < extrai);
    CHECK(contem(s, R"(tasklist /FI "PID eq 4242")"));
}

TEST_CASE("extrai fora antes de encostar na instalacao", "[atualizador][windows]") {
    // Um zip corrompido tem de falhar ANTES de sobrescrever o que funciona.
    const std::string s = script();
    const auto extrai = s.find("tar -xf");
    const auto checa = s.find("if errorlevel 1 goto falhou");
    const auto copia = s.find("robocopy");
    REQUIRE(checa != std::string::npos);
    CHECK(extrai < checa);
    CHECK(checa < copia);
}

TEST_CASE("nunca usa /MIR no robocopy", "[atualizador][windows]") {
    // /MIR espelha: apagaria do destino tudo que nao veio no pacote — e o que
    // nao vem no pacote e justamente o sigaa-viewer.db do aluno, com o
    // semestre inteiro dentro.
    const std::string s = script();
    CHECK_FALSE(contem(s, "/MIR"));
    CHECK(contem(s, "/E "));
}

TEST_CASE("le o codigo de saida do robocopy corretamente", "[atualizador][windows]") {
    // O robocopy devolve 1 quando COPIOU arquivos — sucesso. Tratar `errorlevel
    // 1` como erro faria toda atualizacao bem-sucedida parecer falha. Erro de
    // verdade e a partir de 8.
    const std::string s = script();
    CHECK(contem(s, "if errorlevel 8 goto falhou"));
}

TEST_CASE("reabre o app e se apaga no fim", "[atualizador][windows]") {
    const std::string s = script();
    CHECK(contem(s, R"(start "" "%EXE%")"));
    // `(goto) 2>nul` encerra o script antes do del: e o jeito de um .cmd
    // apagar a si mesmo sem "O processo nao pode acessar o arquivo".
    CHECK(contem(s, R"((goto) 2>nul & del "%~f0")"));
}

TEST_CASE("a falha deixa a versao anterior funcionando", "[atualizador][windows]") {
    // O usuario precisa saber que nao ficou sem app — senao ele vai procurar
    // um instalador achando que quebrou tudo.
    const std::string s = script();
    CHECK(contem(s, ":falhou"));
    CHECK(contem(s, "versao anterior continua instalada"));
    CHECK(contem(s, "pause"));
}

TEST_CASE("troca a code page para UTF-8", "[atualizador][windows]") {
    // O cmd.exe le o .cmd na code page OEM (850 no Brasil) e o arquivo e
    // gravado em UTF-8. Sem o chcp, "C:\Users\Joao" com til chega ao
    // robocopy como outra coisa — e a troca acontece na pasta errada.
    // O publico deste app e brasileiro; nome de usuario com acento e regra,
    // nao excecao.
    const std::string s = script();
    CHECK(contem(s, "chcp 65001"));
    CHECK(s.find("chcp") < s.find("set \"ZIP="));
}

TEST_CASE("as linhas terminam em CRLF", "[atualizador][windows]") {
    // Um .cmd com terminacao Unix falha em linhas com `goto` — o rotulo fica
    // com um \r grudado e o interpretador nao o acha.
    const std::string s = script();
    CHECK(contem(s, "\r\n"));
    // Nenhum \n solto: todo \n tem de ser precedido de \r.
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') REQUIRE(i > 0);
        if (s[i] == '\n') CHECK(s[i - 1] == '\r');
    }
}
