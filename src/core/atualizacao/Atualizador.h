#pragma once
// Descobrir, baixar e instalar a versão nova a partir das releases do GitHub.
//
// O QUE ESTE MÓDULO SE RECUSA A FAZER, e as recusas são o desenho:
//
//   NÃO instala sem conferir a soma. Uma atualização automática é o melhor
//   alvo que um programa pode oferecer: ele baixa um executável e o roda, sem
//   ninguém olhar. O `SHA256SUMS` publicado junto do pacote
//   (tools/empacotar.sh) é conferido ANTES de qualquer coisa virar executável;
//   sem ele, o download é descartado e o usuário fica com a versão que já tem.
//
//   NÃO reinicia o app sozinho. Trocar o binário é reversível enquanto o
//   processo velho está de pé; matá-lo no meio de uma coleta não é. O app
//   avisa e quem reinicia é o usuário.
//
//   NÃO se atualiza fora do AppImage. Uma instalação por pacote da distro
//   pertence ao gerenciador de pacotes, e sobrescrever arquivos em /usr pelas
//   costas dele é como se quebra um sistema. Nesses casos o app abre a página
//   da release e para por aí.
//
// A VERSÃO vem do CMake (`SIGAA_VERSAO`), não de uma constante repetida aqui:
// duas fontes divergem, e a que mente é sempre a que ninguém lembra de mudar.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace sigaa::atualizacao {

// A versão deste binário, como o CMake a definiu.
std::string versaoAtual();

struct Lancamento {
    std::string tag;         // "v0.1.3-alpha"
    std::string versao;      // "0.1.3-alpha"
    std::string notas;       // corpo da release, em Markdown
    std::string paginaUrl;   // para abrir no navegador
    std::string arquivoNome; // o pacote desta plataforma
    std::string arquivoUrl;
    std::string somasUrl;    // SHA256SUMS, "" quando a release não publicou
    std::int64_t bytes{0};
};

// `candidata` é mais nova que `atual`?
//
// Compara número a número, e trata o sufixo de pré-lançamento como MENOR que a
// versão sem sufixo — "0.2.0" > "0.2.0-alpha", como manda o semver. Sem isso
// o app anunciaria uma atualização para trás no dia em que a 0.2.0 final
// saísse depois da 0.2.0-alpha.
bool maisNova(const std::string& candidata, const std::string& atual);

// Consulta a última release do repositório oficial.
//
// `nullopt` com `erro` preenchido. Uma release sem pacote para esta plataforma
// também devolve `nullopt`: anunciar uma atualização que não dá para instalar
// é pior que ficar quieto.
std::optional<Lancamento> ultimoLancamento(std::string* erro = nullptr);

// (recebidos, total). `total` é 0 enquanto o servidor não disser o tamanho.
using Progresso = std::function<void(std::int64_t, std::int64_t)>;

// Baixa para `destino` e CONFERE a soma publicada.
//
// Falha — e apaga o arquivo — quando a soma não bate ou quando a release não
// publicou somas. Preferir um download não verificado seria trocar a única
// garantia que existe por conveniência.
bool baixarEVerificar(const Lancamento& l, const std::string& destino,
                      const Progresso& progresso, std::string* erro);

// Como a atualização pode ser aplicada NESTA instalação.
enum class Instalacao {
    // Linux/AppImage: um arquivo só. Trocamos e o app segue de pé.
    TrocaDireta,
    // Windows: o pacote é o executável MAIS as DLLs do Qt, e o Windows não
    // deixa sobrescrever DLL carregada. Um auxiliar espera o app sair, troca
    // os arquivos e reabre.
    FecharParaTrocar,
    // Instalação por pacote da distro, ou qualquer coisa que não reconhecemos.
    // Mexer aqui é passar por cima do gerenciador de pacotes.
    Manual,
};

Instalacao comoInstalar();

// O caminho do AppImage em execução, ou "" quando o app não roda como AppImage.
// Sai da variável APPIMAGE, que o runtime define.
std::string caminhoDoAppImage();

// A pasta onde o executável em execução mora.
std::string pastaDoApp();

// O caminho do executável em execução.
std::string caminhoDoExecutavel();

// O conteúdo do .cmd que troca os arquivos no Windows.
//
// FUNÇÃO PURA, e compilada em TODA plataforma de propósito: é a peça mais
// delicada da atualização do Windows e não dá para exercitá-la aqui. Deixando-a
// fora do `#ifdef`, ela ao menos compila e é testada em qualquer máquina —
// caminhos com espaço, aspas, a espera pelo processo. O que fica atrás do
// `#ifdef` é só gravar o arquivo e disparar o processo.
std::string scriptDeTrocaWindows(long pid, const std::string& zip,
                                 const std::string& pastaTemporaria,
                                 const std::string& pastaDestino,
                                 const std::string& exe);

// Windows: deixa o auxiliar pronto e disparado. Quem chama deve FECHAR o app
// em seguida — o auxiliar espera por isso antes de tocar em qualquer arquivo.
bool agendarTrocaWindows(const std::string& zip, std::string* erro);

// Põe `baixado` no lugar do AppImage em execução.
//
// Guarda o anterior como `.anterior` ao lado: se a versão nova não abrir, o
// usuário tem para onde voltar sem precisar da internet que talvez não tenha.
bool instalarAppImage(const std::string& baixado, std::string* erro);

} // namespace sigaa::atualizacao
