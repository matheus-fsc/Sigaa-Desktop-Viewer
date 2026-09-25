#include "core/http/CadeiaAia.h"

#include <curl/curl.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sigaa::http {
namespace {

size_t paraString(char* dados, size_t tam, size_t n, void* destino) {
    static_cast<std::string*>(destino)->append(dados, tam * n);
    return tam * n;
}

// O bundle de CAs que a libcurl usa por padrão. Precisamos do CONTEÚDO porque
// `CURLOPT_CAINFO_BLOB` substitui a lista inteira — passar só o intermediário
// deixaria o app sem nenhuma raiz e reprovaria tudo.
std::optional<std::string> lerBundleDoSistema() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    if (!v || !v->cainfo || !*v->cainfo) return std::nullopt;

    std::ifstream f(v->cainfo, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    if (s.find("-----BEGIN CERTIFICATE-----") == std::string::npos) return std::nullopt;
    return s;
}

std::string base64(const std::string& bruto) {
    static constexpr char kTabela[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bruto.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < bruto.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(bruto[i]) << 16) |
                           (static_cast<unsigned char>(bruto[i + 1]) << 8) |
                           static_cast<unsigned char>(bruto[i + 2]);
        out += kTabela[(v >> 18) & 0x3F];
        out += kTabela[(v >> 12) & 0x3F];
        out += kTabela[(v >> 6) & 0x3F];
        out += kTabela[v & 0x3F];
    }
    // O resto: 1 ou 2 bytes, completados com '=' — sem isto o OpenSSL recusa
    // o PEM inteiro, e o sintoma seria "o AIA não funciona" sem dizer por quê.
    if (i < bruto.size()) {
        unsigned v = static_cast<unsigned char>(bruto[i]) << 16;
        const bool doisBytes = (i + 1) < bruto.size();
        if (doisBytes) v |= static_cast<unsigned char>(bruto[i + 1]) << 8;
        out += kTabela[(v >> 18) & 0x3F];
        out += kTabela[(v >> 12) & 0x3F];
        out += doisBytes ? kTabela[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

// Uma requisição GET simples. `verificar=false` SÓ na sondagem que lê o
// certificado — ver o cabeçalho sobre por que isso não abre brecha.
std::optional<std::string> buscar(const std::string& url, bool verificar,
                                  std::string* certinfo = nullptr) {
    CURL* c = curl_easy_init();
    if (!c) return std::nullopt;

    std::string corpo;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, paraString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &corpo);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "sigaa-viewer/aia");
    if (!verificar) {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(c, CURLOPT_CERTINFO, 1L);
        // Nada de corpo: queremos o handshake, não a página. E deixa explícito
        // que esta conexão não transporta dado nenhum.
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    }

    const CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(c);
        return std::nullopt;
    }

    if (certinfo) {
        struct curl_certinfo* ci = nullptr;
        if (curl_easy_getinfo(c, CURLINFO_CERTINFO, &ci) == CURLE_OK && ci) {
            for (int i = 0; i < ci->num_of_certs; ++i) {
                for (auto* s = ci->certinfo[i]; s; s = s->next) {
                    *certinfo += s->data;
                    *certinfo += '\n';
                }
            }
        }
    }
    curl_easy_cleanup(c);
    return corpo;
}

} // namespace

std::string derParaPem(const std::string& bytes) {
    if (bytes.find("-----BEGIN CERTIFICATE-----") != std::string::npos) return bytes;

    const std::string b64 = base64(bytes);
    std::string pem = "-----BEGIN CERTIFICATE-----\n";
    for (size_t i = 0; i < b64.size(); i += 64) {
        pem += b64.substr(i, 64);
        pem += '\n';
    }
    pem += "-----END CERTIFICATE-----\n";
    return pem;
}

std::vector<std::string> urlsDeEmissor(const std::string& certinfo) {
    // O certinfo da libcurl é texto plano, uma linha por campo. A extensão vem
    // como "Authority Information Access:CA Issuers - URI:http://..." — e o
    // OCSP vem na mesma linha ou na seguinte. Queremos SÓ o CA Issuers: buscar
    // qualquer URL pegaria o respondedor OCSP, que não é um certificado.
    std::vector<std::string> urls;
    const std::string marca = "CA Issuers - URI:";

    for (size_t i = certinfo.find(marca); i != std::string::npos;
         i = certinfo.find(marca, i + 1)) {
        const size_t ini = i + marca.size();
        size_t fim = ini;
        while (fim < certinfo.size() && certinfo[fim] != '\n' && certinfo[fim] != '\r' &&
               certinfo[fim] != ' ' && certinfo[fim] != ',') {
            ++fim;
        }
        std::string u = certinfo.substr(ini, fim - ini);
        // Só http/https: o AIA aceita LDAP, e não vamos falar LDAP por causa
        // de um servidor mal configurado.
        if (u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0) {
            bool repetida = false;
            for (const auto& j : urls) repetida = repetida || j == u;
            if (!repetida) urls.push_back(std::move(u));
        }
    }
    return urls;
}

std::optional<std::string> montarBundleAia(const std::string& baseUrl) {
    auto sistema = lerBundleDoSistema();
    if (!sistema) {
        // Sem o bundle do sistema em arquivo não dá para montar um substituto
        // — e é o caso do Windows com Schannel e do macOS com Secure
        // Transport, onde a loja de confiança é do sistema operacional e não
        // um .pem em disco. Nos dois isto também não é necessário: o próprio
        // sistema faz AIA chasing ao validar, então o erro que motivou este
        // arquivo (docs/RECON.md §6.1) não chega a acontecer lá.
        //
        // Degradar em silêncio é correto AQUI, e só aqui: quem chama trata
        // `nullopt` como "siga com a verificação padrão", que continua sendo
        // verificação de verdade. Em nenhum caminho isto vira um TLS relaxado.
        return std::nullopt;
    }

    std::string certinfo;
    if (!buscar(baseUrl, /*verificar=*/false, &certinfo)) return std::nullopt;

    const auto urls = urlsDeEmissor(certinfo);
    if (urls.empty()) return std::nullopt;

    std::string extras;
    for (const auto& u : urls) {
        if (auto cert = buscar(u, /*verificar=*/true)) {
            if (!cert->empty()) extras += derParaPem(*cert);
        }
        // Falha ao buscar um emissor não aborta os outros: uma cadeia pode ter
        // mais de um buraco, e consertar parte dela já pode bastar.
    }
    if (extras.empty()) return std::nullopt;

    return *sistema + "\n" + extras;
}

} // namespace sigaa::http
