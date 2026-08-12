#include "core/http/Trafego.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace sigaa::http {
namespace {

// 500 requisições cobrem um dia inteiro de app aberto com folga: o ciclo caro
// gasta ~22 e roda de 6 em 6 horas. Guardar tudo seria vazamento lento numa
// janela que quase ninguém abre.
constexpr std::size_t kMaxHistorico = 500;

struct Estado {
    std::mutex mtx;
    std::deque<EventoRequisicao> anel;
    std::vector<std::pair<int, Observador>> observadores;
    std::int64_t total{0};
    int proximoId{1};
};

Estado& estado() {
    // Singleton com vida de programa: o registro precisa sobreviver a qualquer
    // SigaaSession, que nasce e morre a cada ciclo.
    static Estado e;
    return e;
}

} // namespace

std::string EventoRequisicao::linha() const {
    const std::time_t t = std::chrono::system_clock::to_time_t(quando);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[64];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::string s = std::string(buf) + " #" + std::to_string(sequencia) + " " + metodo + " " +
                    url;
    if (!erro.empty()) {
        s += "  ERRO: " + erro;
    } else {
        s += "  " + std::to_string(status) + "  " + std::to_string(bytes) + "B  " +
             std::to_string(duracaoMs) + "ms";
        if (!pagina.empty()) s += "  [" + pagina + "]";
    }
    // Espera e tentativa só aparecem quando houve: uma linha que sempre diz
    // "espera 0ms" ensina o olho a ignorar o campo, e é justamente ele que
    // denuncia o rate limit trabalhando.
    if (esperaMs > 0) s += "  (esperou " + std::to_string(esperaMs) + "ms)";
    if (tentativas > 1) s += "  (" + std::to_string(tentativas) + " tentativas)";
    return s;
}

int registrarObservador(Observador o) {
    Estado& e = estado();
    std::lock_guard<std::mutex> lk(e.mtx);
    const int id = e.proximoId++;
    e.observadores.emplace_back(id, std::move(o));
    return id;
}

void removerObservador(int id) {
    Estado& e = estado();
    std::lock_guard<std::mutex> lk(e.mtx);
    e.observadores.erase(
        std::remove_if(e.observadores.begin(), e.observadores.end(),
                       [id](const auto& p) { return p.first == id; }),
        e.observadores.end());
}

std::vector<EventoRequisicao> historico() {
    Estado& e = estado();
    std::lock_guard<std::mutex> lk(e.mtx);
    return {e.anel.begin(), e.anel.end()};
}

std::int64_t totalRequisicoes() {
    Estado& e = estado();
    std::lock_guard<std::mutex> lk(e.mtx);
    return e.total;
}

void limparHistorico() {
    Estado& e = estado();
    std::lock_guard<std::mutex> lk(e.mtx);
    e.anel.clear();
}

void registrar(EventoRequisicao ev) {
    Estado& e = estado();

    // Cópia dos observadores sob o mutex, chamada FORA dele. Chamar segurando o
    // lock convidaria a um deadlock na primeira vez que alguém tentasse ler o
    // histórico de dentro do callback — e é exatamente o que uma janela de
    // diagnóstico tende a fazer.
    std::vector<Observador> obs;
    {
        std::lock_guard<std::mutex> lk(e.mtx);
        ev.sequencia = ++e.total;
        e.anel.push_back(ev);
        while (e.anel.size() > kMaxHistorico) e.anel.pop_front();
        obs.reserve(e.observadores.size());
        for (const auto& [id, o] : e.observadores) obs.push_back(o);
    }
    for (const auto& o : obs) o(ev);
}

} // namespace sigaa::http
