#include "web/Pareamento.h"

#include <array>
#include <random>

#include "core/store/Database.h"

namespace sigaa::web {
namespace {

constexpr const char* kChave = "web.token";

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
    return db.lerMeta(kChave).value_or(std::string());
}

std::string novoToken(store::Database& db) {
    std::string t = gerarToken();
    if (!db.gravarMeta(kChave, t)) return {};
    return t;
}

std::string tokenOuNovo(store::Database& db) {
    std::string t = tokenAtual(db);
    return t.empty() ? novoToken(db) : t;
}

bool tokensIguais(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char dif = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        dif |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return dif == 0;
}

bool autorizado(std::string_view cabecalho, std::string_view token) {
    // Token vazio = ninguém pareado. Sem esta guarda, "Bearer " sem nada
    // depois casaria com o token vazio e abriria a porta.
    if (token.empty()) return false;
    constexpr std::string_view prefixo = "Bearer ";
    if (cabecalho.substr(0, prefixo.size()) != prefixo) return false;
    return tokensIguais(cabecalho.substr(prefixo.size()), token);
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
