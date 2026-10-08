#include "web/Servidor.h"

// Sem OpenSSL nem zlib: o servidor fala HTTP puro dentro da VPN.
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_ZLIB_SUPPORT
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/store/Database.h"
#include "core/util/Caminho.h"
#include "web/Paginas.h"
#include "web/Pareamento.h"

namespace sigaa::web {
namespace {

// O que toda resposta leva. A página não tem script nem estilo inline, e não
// busca nada fora deste servidor — nem fonte do Google —, então a política
// pode ser a mais fechada: se um texto do professor conseguisse virar HTML,
// ele ainda não teria onde rodar.
const httplib::Headers kCabecalhos = {
    {"Content-Security-Policy",
     "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; "
     "connect-src 'self'; manifest-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
     "form-action 'none'"},
    {"X-Content-Type-Options", "nosniff"},
    {"X-Frame-Options", "DENY"},
    {"Referrer-Policy", "no-referrer"},
    {"Cross-Origin-Opener-Policy", "same-origin"},
    {"Cross-Origin-Resource-Policy", "same-origin"},
    {"Permissions-Policy", "camera=(), microphone=(), geolocation=()"},
};

// Tentativas erradas de token, por IP. O token tem 256 bits e não se adivinha;
// o limite existe para um script batendo na porta não encher a lista de
// acessos nem ocupar as quatro threads.
class Tentativas {
public:
    bool bloqueado(const std::string& ip) {
        std::lock_guard<std::mutex> l(m_);
        auto it = mapa_.find(ip);
        if (it == mapa_.end()) return false;
        if (agora() - it->second.inicio > kJanela) {
            mapa_.erase(it);
            return false;
        }
        return it->second.erros >= kMaximo;
    }
    void falhou(const std::string& ip) {
        std::lock_guard<std::mutex> l(m_);
        auto& e = mapa_[ip];
        if (e.erros == 0 || agora() - e.inicio > kJanela) e = {agora(), 0};
        ++e.erros;
    }

private:
    static constexpr std::time_t kJanela = 10 * 60;
    static constexpr int kMaximo = 10;
    struct Estado {
        std::time_t inicio{0};
        int erros{0};
    };
    static std::time_t agora() { return std::time(nullptr); }
    std::mutex m_;
    std::map<std::string, Estado> mapa_;
};

// PIN errado, de QUALQUER IP. O limite por IP não basta para o PIN: ele tem
// só um milhão de combinações, e quem troca de IP a cada dez tentativas
// passaria. Cinco erros num quarto de hora fecham o PIN para todo mundo até a
// janela passar; o QR code continua funcionando.
class BloqueioPin {
public:
    bool bloqueado() {
        std::lock_guard<std::mutex> l(m_);
        limpar();
        return erros_.size() >= kMaximo;
    }
    void falhou() {
        std::lock_guard<std::mutex> l(m_);
        limpar();
        erros_.push_back(std::time(nullptr));
    }

private:
    static constexpr std::time_t kJanela = 15 * 60;
    static constexpr size_t kMaximo = 5;
    void limpar() {
        const std::time_t corte = std::time(nullptr) - kJanela;
        erros_.erase(std::remove_if(erros_.begin(), erros_.end(),
                                    [corte](std::time_t t) { return t < corte; }),
                     erros_.end());
    }
    std::mutex m_;
    std::vector<std::time_t> erros_;
};

// O nome do aparelho do pedido em curso, para o registro de acessos. O
// cpp-httplib chama o logger na mesma thread que atendeu o pedido.
thread_local std::string tAparelho;

void json(httplib::Response& res, int status, const std::string& corpo) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(corpo, "application/json; charset=utf-8");
}

} // namespace

struct Servidor::Impl {
    Config cfg;
    httplib::Server http;
    std::thread thread;
    Tentativas tentativas;
    BloqueioPin bloqueioPin;
};

Servidor::Servidor(Config c) : impl_(std::make_unique<Impl>()) { impl_->cfg = std::move(c); }

Servidor::~Servidor() { parar(); }

const Config& Servidor::config() const { return impl_->cfg; }

bool Servidor::rodando() const { return impl_->thread.joinable() && impl_->http.is_running(); }

bool Servidor::iniciar(std::string* erro) {
    auto falha = [&](std::string m) {
        if (erro) *erro = std::move(m);
        return false;
    };
    if (impl_->thread.joinable()) return true;
    Impl& im = *impl_;
    if (enderecoAberto(im.cfg.host)) {
        return falha("Recusado: escutar em todas as interfaces exporia o app a qualquer "
                     "rede em que este computador estiver. Escolha o endereço da VPN.");
    }

    auto& s = im.http;
    s.new_task_queue = [] { return new httplib::ThreadPool(4); };
    s.set_default_headers(kCabecalhos);
    s.set_payload_max_length(4 * 1024);
    s.set_read_timeout(5, 0);
    s.set_write_timeout(30, 0);
    s.set_keep_alive_max_count(50);

    // Autenticação ANTES do roteamento: uma rota esquecida não fica aberta.
    s.set_pre_routing_handler([&im](const httplib::Request& req, httplib::Response& res) {
        tAparelho.clear();
        const bool parear = req.path == "/api/parear";
        // Só leitura. A única escrita é o próprio pareamento.
        if (!(req.method == "GET" || req.method == "HEAD" || (parear && req.method == "POST"))) {
            json(res, 405, R"({"erro":"somente leitura"})");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.path.rfind("/api/", 0) != 0) return httplib::Server::HandlerResponse::Unhandled;

        if (im.tentativas.bloqueado(req.remote_addr)) {
            json(res, 429, R"({"erro":"tentativas demais; espere alguns minutos"})");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (parear) return httplib::Server::HandlerResponse::Unhandled;

        // Lido do banco a cada pedido: "desconectar" na janela derruba o
        // aparelho no pedido seguinte, sem reiniciar o servidor.
        store::Database db(im.cfg.fonte.banco, store::Database::Abertura::SoExistente);
        const auto ap = aparelhoDoCabecalho(db, req.get_header_value("Authorization"));
        if (!ap) {
            im.tentativas.falhou(req.remote_addr);
            json(res, 401, R"({"erro":"pareamento necessario"})");
            return httplib::Server::HandlerResponse::Handled;
        }
        tAparelho = ap->nome;
        // "Último acesso" com resolução de um minuto: gravar a cada pedido
        // seria uma escrita por tela aberta, disputando o banco com o sync.
        const auto agora = static_cast<std::int64_t>(std::time(nullptr));
        if (agora - ap->ultimoAcesso >= 60 || ap->ultimoIp != req.remote_addr) {
            db.tocarDispositivoWeb(ap->id, req.remote_addr, agora);
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    s.Post("/api/parear", [&im](const httplib::Request& req, httplib::Response& res) {
        Pedido p;
        p.ip = req.remote_addr;
        try {
            const auto j = nlohmann::json::parse(req.body);
            p.codigo = j.value("codigo", std::string());
            p.pin = j.value("pin", std::string());
            p.nome = j.value("nome", std::string());
        } catch (...) {
            json(res, 400, R"({"erro":"pedido malformado"})");
            return;
        }
        const bool porPin = p.codigo.empty();
        if (porPin && im.bloqueioPin.bloqueado()) {
            json(res, 429, R"({"erro":"PIN bloqueado por 15 minutos depois de várias tentativas erradas. Use o QR code."})");
            return;
        }
        std::string token;
        {
            store::Database db(im.cfg.fonte.banco, store::Database::Abertura::SoExistente);
            token = parear(db, p, static_cast<std::int64_t>(std::time(nullptr)));
        }
        if (token.empty()) {
            im.tentativas.falhou(req.remote_addr);
            if (porPin) im.bloqueioPin.falhou();
            json(res, 401, porPin ? R"({"erro":"PIN errado, ou nenhum PIN foi criado no computador."})"
                                  : R"({"erro":"Este código não vale mais. Leia o QR code de novo."})");
            return;
        }
        tAparelho = p.nome;
        json(res, 200, nlohmann::json{{"token", token}}.dump());
    });

    s.Get(R"(/api/.*)", [&im](const httplib::Request& req, httplib::Response& res) {
        std::map<std::string, std::string> q;
        for (const auto& [k, v] : req.params) q[k] = v;
        Resposta r = responder(im.cfg.fonte, req.path, q);
        if (r.arquivo.empty()) {
            json(res, r.status, r.corpo);
            return;
        }
        auto in = std::make_shared<std::ifstream>(util::deUtf8(r.arquivo), std::ios::binary);
        if (!*in) {
            json(res, 404, R"({"erro":"arquivo sumiu do disco"})");
            return;
        }
        in->seekg(0, std::ios::end);
        const auto tamanho = static_cast<size_t>(in->tellg());
        in->seekg(0);
        res.set_header("Cache-Control", "no-store");
        // O nome vai como filename* (RFC 5987): título de material tem acento.
        std::string nome;
        for (unsigned char ch : r.nomeArquivo) {
            if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') {
                nome += static_cast<char>(ch);
            } else {
                char b[4];
                std::snprintf(b, sizeof b, "%%%02X", ch);
                nome += b;
            }
        }
        res.set_header("Content-Disposition", "attachment; filename*=UTF-8''" + nome);
        res.set_content_provider(
            tamanho, r.tipo,
            [in](size_t pos, size_t n, httplib::DataSink& sink) {
                char buf[64 * 1024];
                in->seekg(static_cast<std::streamoff>(pos));
                in->read(buf, static_cast<std::streamsize>(std::min(n, sizeof buf)));
                const auto lidos = in->gcount();
                if (lidos <= 0) return false;
                sink.write(buf, static_cast<size_t>(lidos));
                return true;
            });
    });

    s.Get(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
        const Pagina* p = acharPagina(req.path);
        if (!p) {
            res.status = 404;
            res.set_content("Não encontrado.", "text/plain; charset=utf-8");
            return;
        }
        // Sem cache entre versões: o app atualizado traz páginas novas, e
        // o celular não pode ficar com o app.js de antes.
        res.set_header("Cache-Control", "no-cache");
        res.set_content(reinterpret_cast<const char*>(p->dados), p->tamanho, p->tipo);
    });

    s.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        json(res, 500, R"({"erro":"falha interna"})");
    });

    s.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        if (!aoAcessar) return;
        Acesso a;
        a.quando = static_cast<std::int64_t>(std::time(nullptr));
        a.ip = req.remote_addr;
        a.metodo = req.method;
        a.caminho = req.path;
        a.status = res.status;
        a.aparelho = tAparelho;
        aoAcessar(a);
    });

    if (!s.bind_to_port(im.cfg.host, im.cfg.porta)) {
        return falha("Não consegui abrir " + im.cfg.host + ":" + std::to_string(im.cfg.porta) +
                     ". A porta pode estar em uso, ou o endereço não existe mais neste "
                     "computador (a VPN caiu?).");
    }
    im.thread = std::thread([&s] { s.listen_after_bind(); });
    s.wait_until_ready();
    return true;
}

void Servidor::parar() {
    if (!impl_->thread.joinable()) return;
    impl_->http.stop();
    impl_->thread.join();
}

} // namespace sigaa::web
