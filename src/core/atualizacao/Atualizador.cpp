#include "core/atualizacao/Atualizador.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/atualizacao/Sha256.h"
#include "core/util/Caminho.h"

#ifndef SIGAA_VERSAO
#define SIGAA_VERSAO "0.0.0"
#endif

namespace sigaa::atualizacao {
namespace {

// O repositório oficial, cravado no código de propósito.
//
// Vir de configuração seria um jeito de apontar o atualizador para outro lugar
// editando um arquivo de texto — e o atualizador é justamente o componente que
// baixa e executa código. Mudar de repositório exige recompilar.
// A LISTA, e não `/releases/latest`.
//
// `/releases/latest` devolve 404 quando o repositório só tem pré-lançamentos —
// e este só tem: v0.1.0-alpha, v0.1.1-alpha, v0.1.2-alpha. A primeira versão
// deste arquivo usava aquele endpoint e o atualizador nunca teria achado
// nada, em silêncio, para sempre.
//
// `per_page=10` porque a lista vem da mais nova para a mais velha e nenhuma
// release vinte posições atrás interessa.
constexpr const char* kApiReleases =
    "https://api.github.com/repos/matheus-fsc/Sigaa-Desktop-Viewer/releases"
    "?per_page=10";

std::size_t paraString(char* p, std::size_t t, std::size_t n, void* d) {
    static_cast<std::string*>(d)->append(p, t * n);
    return t * n;
}

std::size_t paraArquivo(char* p, std::size_t t, std::size_t n, void* d) {
    auto* f = static_cast<std::ofstream*>(d);
    f->write(p, static_cast<std::streamsize>(t * n));
    return f->good() ? t * n : 0;
}

CURL* preparar(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    // A API do GitHub recusa quem não se identifica.
    curl_easy_setopt(c, CURLOPT_USERAGENT, "sigaa-viewer-atualizador");
    return c;
}

std::optional<std::string> buscarTexto(const std::string& url, std::string* erro) {
    CURL* c = preparar(url);
    if (!c) {
        if (erro) *erro = "nao consegui iniciar a requisicao";
        return std::nullopt;
    }
    std::string corpo;
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, paraString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &corpo);

    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        if (erro) *erro = curl_easy_strerror(rc);
        return std::nullopt;
    }
    if (status != 200) {
        if (erro) *erro = "o GitHub respondeu HTTP " + std::to_string(status);
        return std::nullopt;
    }
    return corpo;
}

// Divide "0.1.3-alpha" em (0,1,3) e "alpha".
struct Partes {
    int n[3]{0, 0, 0};
    std::string sufixo;
};

Partes partir(const std::string& v) {
    Partes p;
    std::size_t i = 0;
    // Tolera o "v" do nome da tag: `maisNova` é chamada com os dois formatos.
    if (i < v.size() && (v[i] == 'v' || v[i] == 'V')) ++i;

    for (int campo = 0; campo < 3 && i < v.size(); ++campo) {
        int valor = 0;
        bool viu = false;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
            valor = valor * 10 + (v[i] - '0');
            ++i;
            viu = true;
        }
        if (viu) p.n[campo] = valor;
        if (i < v.size() && v[i] == '.') ++i;
        else break;
    }
    // O que sobra depois de um '-' é pré-lançamento.
    if (const auto t = v.find('-', i); t != std::string::npos) p.sufixo = v.substr(t + 1);
    return p;
}

// O nome do pacote desta plataforma dentro da release.
bool ehDestaPlataforma(const std::string& nome) {
#if defined(_WIN32)
    return nome.find("windows") != std::string::npos || nome.ends_with(".zip");
#elif defined(__APPLE__)
    return nome.ends_with(".dmg");
#else
    // O AppImage, e não o portátil: o portátil existe para quem não tem FUSE,
    // e quem está com o app aberto neste momento claramente tem.
    return nome.ends_with(".AppImage");
#endif
}

} // namespace

std::string versaoAtual() { return SIGAA_VERSAO; }

bool maisNova(const std::string& candidata, const std::string& atual) {
    const Partes a = partir(candidata);
    const Partes b = partir(atual);

    for (int i = 0; i < 3; ++i) {
        if (a.n[i] != b.n[i]) return a.n[i] > b.n[i];
    }
    // Mesmos números: quem NÃO tem sufixo é o lançamento final, e ele vem
    // depois de qualquer pré-lançamento. "0.2.0" > "0.2.0-alpha".
    if (a.sufixo.empty() != b.sufixo.empty()) return a.sufixo.empty();
    // Dois pré-lançamentos: ordem alfabética serve para alpha < beta < rc.
    return a.sufixo > b.sufixo;
}

std::optional<Lancamento> ultimoLancamento(std::string* erro) {
    const auto corpo = buscarTexto(kApiReleases, erro);
    if (!corpo) return std::nullopt;

    std::optional<Lancamento> melhor;
    int vistas = 0;
    int semPacote = 0;

    try {
        const auto js = nlohmann::json::parse(*corpo);
        for (const auto& j : js) {
            // Rascunho não é lançamento: só quem tem acesso de escrita ao
            // repositório o enxerga, e ele pode sumir sem aviso.
            if (j.value("draft", false)) continue;
            ++vistas;

            Lancamento l;
            l.tag = j.value("tag_name", "");
            if (l.tag.empty()) continue;
            l.versao = l.tag.starts_with("v") ? l.tag.substr(1) : l.tag;
            l.notas = j.value("body", "");
            l.paginaUrl = j.value("html_url", "");

            for (const auto& a : j.value("assets", nlohmann::json::array())) {
                const std::string nome = a.value("name", "");
                const std::string url = a.value("browser_download_url", "");
                if (nome == "SHA256SUMS") {
                    l.somasUrl = url;
                } else if (l.arquivoUrl.empty() && ehDestaPlataforma(nome)) {
                    l.arquivoNome = nome;
                    l.arquivoUrl = url;
                    l.bytes = a.value("size", 0LL);
                }
            }

            // Sem pacote para esta plataforma a release não serve — mas uma
            // ANTERIOR pode servir, então seguimos procurando em vez de parar.
            if (l.arquivoUrl.empty()) {
                ++semPacote;
                continue;
            }
            // Pela VERSÃO, e não pela ordem da lista: a API ordena por data de
            // publicação, e uma correção de emergência numa série antiga sai
            // depois da versão nova sem ser mais nova que ela.
            if (!melhor || maisNova(l.versao, melhor->versao)) melhor = std::move(l);
        }
    } catch (const std::exception& e) {
        if (erro) *erro = std::string("resposta do GitHub ilegivel: ") + e.what();
        return std::nullopt;
    }

    if (!melhor) {
        if (erro) {
            *erro = vistas == 0
                        ? "o repositorio ainda nao publicou release nenhuma"
                        : "nenhuma das " + std::to_string(vistas) +
                              " releases tem pacote para esta plataforma";
            (void)semPacote;
        }
        return std::nullopt;
    }
    return melhor;
}

bool baixarEVerificar(const Lancamento& l, const std::string& destino,
                      const Progresso& progresso, std::string* erro) {
    auto falhar = [&](std::string m) {
        if (erro) *erro = std::move(m);
        std::error_code ec;
        std::filesystem::remove(destino, ec);
        return false;
    };

    // A SOMA VEM PRIMEIRO. Baixar 90 MB para descobrir no fim que a release
    // não publicou como verificá-los é gastar a banda do usuário para chegar
    // na mesma recusa.
    if (l.somasUrl.empty()) {
        return falhar("esta release nao publicou SHA256SUMS; nao da para "
                      "verificar o download, entao nao vou instalar");
    }
    const auto somas = buscarTexto(l.somasUrl, erro);
    if (!somas) return falhar(erro && !erro->empty() ? *erro : "nao consegui ler as somas");

    std::string esperado;
    {
        std::istringstream in(*somas);
        std::string linha;
        while (std::getline(in, linha)) {
            // Formato do sha256sum: "<hex>  <nome>".
            const auto esp = linha.find(' ');
            if (esp == std::string::npos) continue;
            if (linha.find(l.arquivoNome) != std::string::npos) {
                esperado = linha.substr(0, esp);
                break;
            }
        }
    }
    if (esperado.size() != 64) {
        return falhar("o SHA256SUMS nao traz a soma de " + l.arquivoNome);
    }

    {
        std::ofstream f(destino, std::ios::binary | std::ios::trunc);
        if (!f) return falhar("nao consegui escrever em " + destino);

        CURL* c = preparar(l.arquivoUrl);
        if (!c) return falhar("nao consegui iniciar o download");
        // Sem CURLOPT_TIMEOUT: um pacote de 90 MB numa conexão ruim passa de
        // qualquer teto razoável, e cortar no meio seria transformar internet
        // lenta em erro. O timeout de CONEXÃO continua valendo.
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, paraArquivo);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &f);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);   // 1 KB/s
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);      // por 60 s = desistir

        if (progresso) {
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(
                c, CURLOPT_XFERINFOFUNCTION,
                +[](void* p, curl_off_t total, curl_off_t agora, curl_off_t,
                    curl_off_t) -> int {
                    (*static_cast<const Progresso*>(p))(agora, total);
                    return 0;
                });
            curl_easy_setopt(c, CURLOPT_XFERINFODATA, &progresso);
        }

        const CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(c);
        f.close();

        if (rc != CURLE_OK) return falhar(std::string("download falhou: ") +
                                          curl_easy_strerror(rc));
        if (status != 200) return falhar("download devolveu HTTP " +
                                         std::to_string(status));
    }

    // Confere em blocos: o AppImage tem ~90 MB e não há por que carregá-lo
    // inteiro na memória para somar.
    hash::Sha256 h;
    {
        std::ifstream f(destino, std::ios::binary);
        if (!f) return falhar("nao consegui reler o arquivo baixado");
        char buf[64 * 1024];
        while (f.read(buf, sizeof buf) || f.gcount() > 0) {
            h.consumir(buf, static_cast<std::size_t>(f.gcount()));
        }
    }
    const std::string obtido = h.hex();
    if (obtido != esperado) {
        return falhar("a soma do arquivo baixado nao confere com a publicada "
                      "(esperado " + esperado.substr(0, 12) + "…, obtido " +
                      obtido.substr(0, 12) + "…). O download foi descartado.");
    }
    return true;
}

std::string caminhoDoAppImage() {
    if (const char* p = std::getenv("APPIMAGE"); p && *p) return p;
    return {};
}

std::string caminhoDoExecutavel() {
#if defined(_WIN32)
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0) return {};
    // `paraUtf8`, e NÃO `path::string()`: no Windows aquele converte pela code
    // page ANSI e estraga "C:\Users\João" — que é o caminho de boa parte do
    // público deste app. É o mesmo cuidado de core/util/Caminho.h, que nasceu
    // de um bug igual nas pastas de turma.
    return util::paraUtf8(std::filesystem::path(std::wstring(buf, n)));
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : util::paraUtf8(p);
#endif
}

std::string pastaDoApp() {
    const std::string exe = caminhoDoExecutavel();
    if (exe.empty()) return {};
    return util::paraUtf8(util::deUtf8(exe).parent_path());
}

Instalacao comoInstalar() {
#if defined(_WIN32)
    // O pacote do Windows é um zip com o .exe e as DLLs do Qt, extraído onde o
    // usuário quis. Não há instalador nem registro para consultar: se dá para
    // escrever na pasta do executável, dá para trocar o conteúdo dela.
    //
    // A escrita é TESTADA, não suposta: um app em `C:\Program Files` sem
    // elevação falharia no meio da troca, com metade dos arquivos novos.
    std::error_code ec;
    const auto teste =
        std::filesystem::path(pastaDoApp()) / ".sigaa-teste-de-escrita";
    { std::ofstream f(teste); if (!f) return Instalacao::Manual; }
    std::filesystem::remove(teste, ec);
    return Instalacao::FecharParaTrocar;
#else
    return caminhoDoAppImage().empty() ? Instalacao::Manual
                                       : Instalacao::TrocaDireta;
#endif
}

std::string scriptDeTrocaWindows(long pid, const std::string& zip,
                                 const std::string& pastaTemporaria,
                                 const std::string& pastaDestino,
                                 const std::string& exe) {
    // Aspas em toda variável: "C:\Program Files\..." sem elas vira dois
    // argumentos, e o script apagaria ou copiaria a pasta errada.
    std::ostringstream o;
    o << "@echo off\r\n"
      // O cmd.exe lê o .cmd na code page OEM (850/437 no Brasil), e este
      // arquivo é gravado em UTF-8. Sem o chcp, um caminho como
      // "C:\Users\João\..." chega ao robocopy como outra coisa e a troca
      // acontece na pasta errada — ou em pasta nenhuma.
      << "chcp 65001 >nul\r\n"
      << "setlocal\r\n"
      << "rem Auxiliar de atualizacao do SIGAA Viewer. Gerado pelo app.\r\n"
      << "rem Espera o app fechar, troca os arquivos e reabre.\r\n"
      << "set \"ZIP=" << zip << "\"\r\n"
      << "set \"TEMP_DIR=" << pastaTemporaria << "\"\r\n"
      << "set \"DEST=" << pastaDestino << "\"\r\n"
      << "set \"EXE=" << exe << "\"\r\n"
      << "\r\n"
      // Sem esta espera o robocopy encontraria as DLLs ainda carregadas e
      // falharia arquivo por arquivo, deixando a instalacao pela metade.
      << ":esperar\r\n"
      << "tasklist /FI \"PID eq " << pid << "\" 2>nul | find \"" << pid
      << "\" >nul\r\n"
      << "if not errorlevel 1 (\r\n"
      << "  timeout /t 1 /nobreak >nul\r\n"
      << "  goto esperar\r\n"
      << ")\r\n"
      << "\r\n"
      // Extrai PRIMEIRO num lugar a parte: um zip corrompido tem de falhar
      // antes de encostar na instalacao que funciona.
      << "rmdir /s /q \"%TEMP_DIR%\" 2>nul\r\n"
      << "mkdir \"%TEMP_DIR%\"\r\n"
      << "tar -xf \"%ZIP%\" -C \"%TEMP_DIR%\"\r\n"
      << "if errorlevel 1 goto falhou\r\n"
      << "\r\n"
      // /E copia subpastas; SEM /MIR, que apagaria o que nao veio no pacote —
      // e o que nao vem no pacote e justamente o banco do aluno.
      // Codigo de saida do robocopy: ate 7 e sucesso, 8 ou mais e erro.
      << "robocopy \"%TEMP_DIR%\" \"%DEST%\" /E /NFL /NDL /NJH /NJS /NP >nul\r\n"
      << "if errorlevel 8 goto falhou\r\n"
      << "\r\n"
      << "rmdir /s /q \"%TEMP_DIR%\" 2>nul\r\n"
      << "del /q \"%ZIP%\" 2>nul\r\n"
      << "start \"\" \"%EXE%\"\r\n"
      << "goto fim\r\n"
      << "\r\n"
      << ":falhou\r\n"
      << "echo A atualizacao falhou. A versao anterior continua instalada e\r\n"
      << "echo funcionando - basta abrir o app de novo.\r\n"
      << "pause\r\n"
      << "\r\n"
      // `(goto) 2>nul` encerra o script ANTES do del, que e o jeito de um .cmd
      // apagar a si mesmo sem o "O processo nao pode acessar o arquivo".
      << ":fim\r\n"
      << "(goto) 2>nul & del \"%~f0\"\r\n";
    return o.str();
}

bool agendarTrocaWindows(const std::string& zip, std::string* erro) {
    auto falhar = [&](std::string m) {
        if (erro) *erro = std::move(m);
        return false;
    };

#if defined(_WIN32)
    const std::string destino = pastaDoApp();
    const std::string exe = caminhoDoExecutavel();
    if (destino.empty() || exe.empty()) return falhar("nao descobri onde o app esta");

    std::error_code ec;
    const auto tmp = std::filesystem::temp_directory_path(ec);
    if (ec) return falhar("nao achei a pasta temporaria: " + ec.message());

    const long pid = static_cast<long>(GetCurrentProcessId());
    const auto pastaExtracao = tmp / ("sigaa-update-" + std::to_string(pid));
    const auto script = tmp / ("sigaa-update-" + std::to_string(pid) + ".cmd");

    {
        // Binário, não texto: o conteúdo já traz \r\n, e o modo texto os
        // duplicaria — um .cmd com \r\r\n falha em linhas com `goto`.
        std::ofstream f(script, std::ios::binary);
        if (!f) return falhar("nao consegui escrever o auxiliar de atualizacao");
        const std::string conteudo = scriptDeTrocaWindows(
            pid, zip, util::paraUtf8(pastaExtracao), destino, exe);
        f.write(conteudo.data(), static_cast<std::streamsize>(conteudo.size()));
        if (!f) return falhar("escrita incompleta do auxiliar de atualizacao");
    }

    // DETACHED_PROCESS: o auxiliar tem de sobreviver ao fim deste processo —
    // ele existe justamente para agir depois.
    std::wstring cmd = L"cmd.exe /c \"" + script.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr,
                        nullptr, &si, &pi)) {
        return falhar("nao consegui iniciar o auxiliar de atualizacao");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)zip;
    return falhar("a troca por auxiliar so existe no Windows");
#endif
}

bool instalarAppImage(const std::string& baixado, std::string* erro) {
    auto falhar = [&](std::string m) {
        if (erro) *erro = std::move(m);
        return false;
    };

    const std::string atual = caminhoDoAppImage();
    if (atual.empty()) return falhar("o app nao esta rodando como AppImage");

    std::error_code ec;
    const auto destino = std::filesystem::path(atual);
    const auto anterior = std::filesystem::path(atual + ".anterior");

    // O binário EM EXECUÇÃO pode ser renomeado no Linux — o processo segue de
    // pé com o inode antigo. É o que permite trocar a versão sem fechar o app.
    std::filesystem::remove(anterior, ec);
    std::filesystem::rename(destino, anterior, ec);
    if (ec) return falhar("nao consegui guardar a versao atual: " + ec.message());

    std::filesystem::rename(baixado, destino, ec);
    if (ec) {
        // Volta atrás: melhor a versão velha funcionando do que nenhuma.
        std::error_code ec2;
        std::filesystem::rename(anterior, destino, ec2);
        return falhar("nao consegui pôr a versao nova no lugar: " + ec.message());
    }

    std::filesystem::permissions(destino,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add, ec);
    if (ec) return falhar("a versao nova ficou sem permissao de execucao");
    return true;
}

} // namespace sigaa::atualizacao
