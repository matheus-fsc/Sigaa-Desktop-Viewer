#include "core/config/Instituicao.h"

#include <cctype>

namespace sigaa::config {
namespace {

std::string minusculas(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (const unsigned char c : s) o.push_back(static_cast<char>(std::tolower(c)));
    return o;
}

// Só a UNIFEI está aqui, e de propósito.
//
// A tentação é encher esta lista com os outros SIGAA federais — os endereços
// são públicos, e a lista pareceria mais completa. Mas cada entrada aqui é uma
// PROMESSA: "escolha isto e o app funciona". Ninguém rodou o parser contra
// nenhuma outra instância, e uma lista de vinte universidades onde dezenove
// falham no primeiro login é pior para o usuário do que uma lista de uma e um
// campo de endereço — porque no primeiro caso ele culpa a senha, e no segundo
// sabe exatamente o que está fazendo.
//
// Adicionar uma instituição, depois de alguém conferir de verdade contra o
// site: uma linha aqui, `verificada=true`, e uma nota em docs/RECON.md dizendo
// qual versão do SIGAA foi testada.
const std::vector<Instituicao>& catalogoInterno() {
    static const std::vector<Instituicao> lista = {
        {"unifei", "UNIFEI — Universidade Federal de Itajubá",
         "https://sigaa.unifei.edu.br", /*verificada=*/true},
    };
    return lista;
}

Instituicao& atual() {
    static Instituicao inst = catalogoInterno().front();
    return inst;
}

} // namespace

std::string Instituicao::host() const {
    std::string_view s = baseUrl;
    for (const std::string_view p : {std::string_view("https://"), std::string_view("http://")}) {
        if (s.substr(0, p.size()) == p) {
            s = s.substr(p.size());
            break;
        }
    }
    const auto barra = s.find('/');
    if (barra != std::string_view::npos) s = s.substr(0, barra);
    return std::string(s);
}

const std::vector<Instituicao>& catalogo() { return catalogoInterno(); }

std::optional<Instituicao> porId(std::string_view id) {
    for (const auto& i : catalogoInterno()) {
        if (i.id == id) return i;
    }
    return std::nullopt;
}

Instituicao personalizada(std::string_view urlOuHost) {
    std::string s = minusculas(urlOuHost);
    // trim
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && s.back() == '/') s.pop_back();

    Instituicao out;
    if (s.empty()) return out;

    // http:// digitado vira https://. Ver o comentário no cabeçalho: o que
    // trafega aqui é CPF e senha.
    if (s.rfind("http://", 0) == 0) s = "https://" + s.substr(7);
    if (s.rfind("https://", 0) != 0) s = "https://" + s;

    // Um host precisa de pelo menos um ponto. Sem isso o usuário digitou o
    // nome da universidade, não o endereço, e é melhor recusar do que tentar
    // "https://unifei" e culpar a rede.
    Instituicao teste;
    teste.baseUrl = s;
    if (teste.host().find('.') == std::string::npos) return out;

    out.id = "personalizada";
    out.nome = teste.host();
    out.baseUrl = s;
    out.verificada = false;

    // Uma URL personalizada que aponta para uma instituição do catálogo é ela
    // mesma — e aí o cofre e o aviso de "não verificada" ficam certos.
    for (const auto& i : catalogoInterno()) {
        if (i.host() == teste.host()) return i;
    }
    return out;
}

const Instituicao& selecionada() { return atual(); }

void selecionar(Instituicao inst) {
    if (!inst.valida()) return;
    atual() = std::move(inst);
}

} // namespace sigaa::config
