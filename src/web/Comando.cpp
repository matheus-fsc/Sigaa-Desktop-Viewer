#include "web/Comando.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "core/store/Database.h"
#include "core/util/Caminho.h"
#include "core/util/Pastas.h"
#include "web/Pareamento.h"
#include "web/Servidor.h"

namespace sigaa::web {
namespace {

std::atomic<bool> gParar{false};

void aoSinal(int) { gParar = true; }

std::string ambiente(const char* n) {
    const char* v = std::getenv(n);
    return v ? std::string(v) : std::string();
}

int uso() {
    std::cerr << "uso:\n"
                 "  sigaa-cli web [--escutar <ip>[:<porta>]] [--banco <db>] [--materiais <dir>]\n"
                 "                 serve as telas para o celular (padrao 127.0.0.1:8765)\n"
                 "  sigaa-cli web token [--novo]\n"
                 "                 mostra o codigo de pareamento; --novo revoga o anterior\n"
                 "\n"
                 "Escute no IP da VPN (tailscale0, wg0...), nunca em 0.0.0.0. Ver docs/WEB.md.\n";
    return 2;
}

} // namespace

int comando(int argc, char** argv) {
    Config cfg;
    bool pedirToken = false, tokenNovo = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--escutar" && i + 1 < argc) {
            // "100.64.1.2:9000", "100.64.1.2", "[fd7a::1]:9000" ou "fd7a::1".
            const std::string v = argv[++i];
            std::string porta;
            if (!v.empty() && v.front() == '[') {
                const auto fim = v.find(']');
                if (fim == std::string::npos) return uso();
                cfg.host = v.substr(1, fim - 1);
                if (v.size() > fim + 2 && v[fim + 1] == ':') porta = v.substr(fim + 2);
            } else if (const auto dois = v.find(':');
                       dois != std::string::npos && v.find(':', dois + 1) == std::string::npos) {
                cfg.host = v.substr(0, dois);
                porta = v.substr(dois + 1);
            } else {
                cfg.host = v;
            }
            if (!porta.empty()) cfg.porta = std::atoi(porta.c_str());
        } else if (a == "--porta" && i + 1 < argc) {
            cfg.porta = std::atoi(argv[++i]);
        } else if (a == "--banco" && i + 1 < argc) {
            cfg.fonte.banco = argv[++i];
        } else if (a == "--materiais" && i + 1 < argc) {
            cfg.fonte.materiais = argv[++i];
        } else if ((a == "--url" || a == "--instituicao") && i + 1 < argc) {
            ++i;   // já tratado em main(); só não é argumento daqui
        } else if (a == "token") {
            pedirToken = true;
        } else if (a == "--novo") {
            tokenNovo = true;
        } else {
            return uso();
        }
    }
    if (cfg.porta <= 0 || cfg.porta > 65535) return uso();

    if (cfg.fonte.banco.empty()) cfg.fonte.banco = ambiente("SIGAA_BANCO");
    if (cfg.fonte.banco.empty()) cfg.fonte.banco = "sigaa-viewer.db";
    if (cfg.fonte.materiais.empty()) cfg.fonte.materiais = ambiente("SIGAA_MATERIAIS");
    if (cfg.fonte.materiais.empty()) cfg.fonte.materiais = util::pastaMateriaisPadrao();
    std::error_code ec;
    const auto abs = std::filesystem::absolute(util::deUtf8(cfg.fonte.banco), ec);
    if (!ec) cfg.fonte.banco = util::paraUtf8(abs);

    store::Database db(cfg.fonte.banco, store::Database::Abertura::SoExistente);
    if (!db.aberto()) {
        std::cerr << "sigaa-cli web: banco nao encontrado em " << cfg.fonte.banco
                  << " — rode um sync primeiro, ou passe --banco.\n";
        return 1;
    }
    const std::string token = tokenNovo ? novoToken(db) : tokenOuNovo(db);
    if (token.empty()) {
        std::cerr << "sigaa-cli web: nao consegui gravar o codigo de pareamento no banco.\n";
        return 1;
    }
    if (pedirToken) {
        std::cout << token << "\n";
        if (tokenNovo) std::cerr << "Codigo novo gravado. Os celulares pareados antes precisam parear de novo.\n";
        return 0;
    }

    Servidor srv(cfg);
    srv.aoAcessar = [](const Acesso& a) {
        std::cerr << a.ip << " " << a.metodo << " " << a.caminho << " " << a.status << "\n";
    };
    std::string erro;
    if (!srv.iniciar(&erro)) {
        std::cerr << "sigaa-cli web: " << erro << "\n";
        return 1;
    }
    std::cerr << "Acesso mobile no ar. Abra no celular (o codigo vai depois do #, que o\n"
                 "navegador nao envia a ninguem):\n\n  "
              << urlDePareamento(cfg.host, cfg.porta, token)
              << "\n\nCtrl+C para parar.\n";

    std::signal(SIGINT, aoSinal);
    std::signal(SIGTERM, aoSinal);
    while (!gParar && srv.rodando()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    srv.parar();
    return 0;
}

} // namespace sigaa::web
