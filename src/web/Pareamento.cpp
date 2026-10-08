#include "web/Pareamento.h"

#include <algorithm>
#include <array>
#include <random>

#include "core/atualizacao/Sha256.h"

namespace sigaa::web {
namespace {

constexpr const char* kChaveToken = "web.token";
constexpr const char* kChavePin = "web.pin";
constexpr size_t kNomeMaximo = 60;

std::string base64url(const unsigned char* p, size_t n) {
    static const char* abc =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string s;
    unsigned v = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        v = (v << 8) | p[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            s += abc[(v >> bits) & 63];
        }
    }
    if (bits > 0) s += abc[(v << (6 - bits)) & 63];
    return s;
}

std::string hashDoPin(const std::string& sal, std::string_view pin) {
    return hash::sha256Hex(sal + ":" + std::string(pin));
}

// Nome de aparelho para a lista: sem controle, sem quebra de linha, curto.
// É texto de terceiro e a UI o mostra como texto puro; isto só garante que
// ele caiba numa linha.
std::string limparNome(std::string_view n) {
    std::string s;
    for (unsigned char c : n) {
        if (c < 0x20 || c == 0x7f) continue;
        s += static_cast<char>(c);
    }
    if (s.size() > kNomeMaximo) {
        s.resize(kNomeMaximo);
        // Não cortar no meio de um caractere UTF-8.
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0x80)) s.pop_back();
    }
    return s.empty() ? std::string("Aparelho sem nome") : s;
}

} // namespace

std::string gerarToken() {
    // random_device é a fonte do sistema (/dev/urandom, BCryptGenRandom). Um
    // mt19937 semeado com ela seria previsível depois de alguns tokens; aqui
    // cada byte vem direto dela.
    std::random_device rd;
    std::array<unsigned char, 32> b{};
    for (size_t i = 0; i < b.size(); i += 4) {
        const unsigned v = rd();
        for (size_t k = 0; k < 4 && i + k < b.size(); ++k) {
            b[i + k] = static_cast<unsigned char>(v >> (8 * k));
        }
    }
    return base64url(b.data(), b.size());
}

std::string tokenAtual(store::Database& db) {
    if (!db.aberto()) return {};
    return db.lerMeta(kChaveToken).value_or(std::string());
}

std::string novoToken(store::Database& db) {
    std::string t = gerarToken();
    if (!db.gravarMeta(kChaveToken, t)) return {};
    return t;
}

std::string tokenOuNovo(store::Database& db) {
    std::string t = tokenAtual(db);
    return t.empty() ? novoToken(db) : t;
}

bool pinValido(std::string_view pin) {
    return pin.size() >= static_cast<size_t>(kPinMinimo) &&
           pin.size() <= static_cast<size_t>(kPinMaximo) &&
           std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool temPin(store::Database& db) {
    return db.aberto() && !db.lerMeta(kChavePin).value_or(std::string()).empty();
}

bool definirPin(store::Database& db, std::string_view pin) {
    if (!pinValido(pin)) return false;
    // Sal por PIN: dois alunos com o mesmo PIN não ficam com o mesmo hash.
    const std::string sal = gerarToken().substr(0, 16);
    return db.gravarMeta(kChavePin, sal + "$" + hashDoPin(sal, pin));
}

bool removerPin(store::Database& db) { return db.gravarMeta(kChavePin, ""); }

bool pinConfere(store::Database& db, std::string_view pin) {
    if (!pinValido(pin)) return false;
    const std::string v = db.lerMeta(kChavePin).value_or(std::string());
    const auto cifrao = v.find('$');
    if (cifrao == std::string::npos) return false;
    return tokensIguais(hashDoPin(v.substr(0, cifrao), pin), v.substr(cifrao + 1));
}

std::string hashDoToken(std::string_view token) { return hash::sha256Hex(std::string(token)); }

std::string parear(store::Database& db, const Pedido& p, std::int64_t agora) {
    if (!db.aberto()) return {};
    std::string via;
    if (!p.codigo.empty()) {
        const std::string atual = tokenAtual(db);
        if (!atual.empty() && tokensIguais(p.codigo, atual)) via = "qr";
    } else if (!p.pin.empty() && pinConfere(db, p.pin)) {
        via = "pin";
    }
    if (via.empty()) return {};
    std::string token = gerarToken();
    if (db.criarDispositivoWeb(hashDoToken(token), limparNome(p.nome), via, p.ip, agora) == 0) {
        return {};
    }
    return token;
}

std::optional<store::Database::DispositivoWeb> aparelhoDoCabecalho(store::Database& db,
                                                                   std::string_view cabecalho) {
    constexpr std::string_view prefixo = "Bearer ";
    if (cabecalho.substr(0, prefixo.size()) != prefixo) return std::nullopt;
    const std::string_view token = cabecalho.substr(prefixo.size());
    // Um token nosso tem 43 caracteres; o resto nem vai ao banco.
    if (token.size() != 43) return std::nullopt;
    // A busca é pelo hash, então o tempo dela não diz nada sobre o token.
    return db.dispositivoWebPorToken(hashDoToken(token));
}

bool tokensIguais(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char dif = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        dif |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return dif == 0;
}

bool enderecoAberto(std::string_view host) {
    return host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]" || host == "*";
}

std::string urlDePareamento(const std::string& host, int porta, const std::string& token) {
    const bool v6 = host.find(':') != std::string::npos;
    std::string u = "http://";
    u += v6 ? "[" + host + "]" : host;
    u += ":" + std::to_string(porta) + "/";
    if (!token.empty()) u += "#t=" + token;
    return u;
}

} // namespace sigaa::web
