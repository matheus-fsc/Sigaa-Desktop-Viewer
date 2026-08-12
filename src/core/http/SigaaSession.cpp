#include "core/http/SigaaSession.h"

#include <curl/curl.h>

#include <mutex>
#include <regex>
#include <thread>

#include "core/config/Instituicao.h"
#include "core/http/Trafego.h"
#include "core/jsf/JsfForm.h"
#include "core/parse/Html.h"   // toUtf8: o SIGAA serve cp1252 (RECON §1.10)

namespace sigaa::http {
namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string minusculas(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string aparar(std::string_view s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

// Uma linha de cabeçalho por chamada, incluindo a linha de status e a linha em
// branco final. Redirect faz o curl chamar de novo para a resposta seguinte —
// e aí a linha de status reaparece, que é onde limpamos o que veio do 302.
size_t headerCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t n = size * nmemb;
    auto* mapa = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string_view linha(ptr, n);

    if (linha.rfind("HTTP/", 0) == 0) {
        mapa->clear();   // resposta nova: descarta os cabeçalhos do redirect
        return n;
    }
    const size_t dp = linha.find(':');
    if (dp == std::string_view::npos) return n;
    (*mapa)[minusculas(aparar(linha.substr(0, dp)))] = aparar(linha.substr(dp + 1));
    return n;
}

// Inicializa/finaliza o curl uma vez por processo.
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};
const CurlGlobal& curlGlobal() {
    static CurlGlobal g;
    return g;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

} // namespace

std::string Response::cabecalho(const std::string& nome) const {
    const auto it = cabecalhos.find(nome);
    return it == cabecalhos.end() ? std::string{} : it->second;
}

std::string Response::nomeSugerido() const {
    const std::string cd = cabecalho("content-disposition");
    if (cd.empty()) return {};

    std::string nome;
    // RFC 5987 primeiro: filename*=UTF-8''nome%20com%20acento. Quando os dois
    // aparecem, este é o que tem o nome correto — o `filename=` puro é a versão
    // degradada para cliente velho.
    if (const size_t p = cd.find("filename*="); p != std::string::npos) {
        std::string v = cd.substr(p + 10);
        if (const size_t asp = v.find(';'); asp != std::string::npos) v = v.substr(0, asp);
        if (const size_t plic = v.rfind('\''); plic != std::string::npos) v = v.substr(plic + 1);
        // percent-decode
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '%' && i + 2 < v.size()) {
                nome += static_cast<char>(std::stoi(v.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                nome += v[i];
            }
        }
    } else if (const size_t q = cd.find("filename="); q != std::string::npos) {
        nome = cd.substr(q + 9);
        if (const size_t asp = nome.find(';'); asp != std::string::npos) {
            nome = nome.substr(0, asp);
        }
    }
    nome = aparar(nome);
    if (nome.size() >= 2 && nome.front() == '"' && nome.back() == '"') {
        nome = nome.substr(1, nome.size() - 2);
    }

    // Sanitiza: o nome vem do servidor, e um "..\\..\\algo.exe" no
    // Content-Disposition escreveria fora da pasta escolhida pelo usuário.
    // Fica só o último componente, sem separador nem caractere proibido.
    if (const size_t b = nome.find_last_of("/\\"); b != std::string::npos) {
        nome = nome.substr(b + 1);
    }
    std::string limpo;
    for (const char c : nome) {
        limpo += (std::string(":*?\"<>|").find(c) != std::string::npos ||
                  static_cast<unsigned char>(c) < 0x20)
                     ? '_'
                     : c;
    }
    if (limpo == "." || limpo == "..") return {};

    // Sai em UTF-8, como todo std::string do projeto. O `filename*=UTF-8''`
    // já veio assim; o `filename=` puro vem nos bytes que o servidor escolher,
    // e o SIGAA serve windows-1252 (RECON §1.10) — sem isto, "Introdução.pdf"
    // chegaria ao disco com o acento quebrado.
    return html::toUtf8(limpo);
}

bool Response::ehDownload() const {
    if (!cabecalho("content-disposition").empty()) return true;
    const std::string ct = minusculas(cabecalho("content-type"));
    return !ct.empty() && ct.rfind("text/html", 0) != 0;
}

std::string_view toString(PageKind k) {
    switch (k) {
        case PageKind::Portal:         return "Portal";
        case PageKind::TurmaVirtual:   return "TurmaVirtual";
        case PageKind::Login:          return "Login";
        case PageKind::SessaoExpirada: return "SessaoExpirada";
        default:                       return "Desconhecida";
    }
}

std::string infoBackend() {
    curlGlobal();
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    std::string s = "libcurl ";
    s += v->version ? v->version : "?";
    s += "\nTLS: ";
    s += v->ssl_version ? v->ssl_version : "NENHUM (https nao vai funcionar)";
    s += "\nCA bundle: ";
    s += (v->cainfo && *v->cainfo) ? v->cainfo : "(nao embutido)";
    s += "\nprotocolos: ";
    for (const char* const* p = v->protocols; p && *p; ++p) {
        s += *p;
        s += ' ';
    }
    return s;
}

// ---------------------------------------------------------------------------

struct SigaaSession::Impl {
    std::string baseUrl;
    std::string userAgent = "sigaa-viewer/0.1 (cliente pessoal; contato via repositorio)";
    CURL* curl{nullptr};
    std::mutex mtx;                        // invariante 1: uma req por vez
    std::chrono::steady_clock::time_point ultimaReq{};
    std::chrono::milliseconds intervalo{1500};   // invariante 2
    int maxTentativasLogin{3};                   // invariante 3
    int tentativasLogin{0};
    bool autenticado{false};
    char errbuf[CURL_ERROR_SIZE]{};   // mensagem detalhada; strerror so da a categoria
    int maxTentativas{3};             // retry de erros transitorios

    Response perform(std::string_view path, const std::string* body);
    CURLcode executar(const std::string& url, const std::string* body, Response& r);
};

namespace {

// Erros que valem retentar: falhas de transporte, nao de conteudo. O SIGAA
// derruba a conexao no meio do handshake TLS de forma intermitente (visto em
// producao: "Recv failure: Connection was reset" wrapped em SSL connect
// error). Um app que roda sozinho a cada 20 min nao pode morrer por isso.
bool transitorio(CURLcode rc) {
    switch (rc) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

} // namespace

Response SigaaSession::Impl::perform(std::string_view path, const std::string* body) {
    std::lock_guard<std::mutex> lock(mtx);

    using relogio = std::chrono::steady_clock;
    const auto entrou = relogio::now();

    // Rate limit: espera o que faltar do intervalo mínimo.
    if (ultimaReq.time_since_epoch().count() != 0) {
        auto decorrido = relogio::now() - ultimaReq;
        if (decorrido < intervalo) std::this_thread::sleep_for(intervalo - decorrido);
    }

    Response r;
    std::string url = path.rfind("http", 0) == 0
                          ? std::string(path)
                          : baseUrl + std::string(path);

    const auto saiu = relogio::now();
    int tentativas = 1;

    for (int tentativa = 1; ; ++tentativa) {
        tentativas = tentativa;
        r = Response{};
        const CURLcode rc = executar(url, body, r);
        if (rc == CURLE_OK || !transitorio(rc) || tentativa >= maxTentativas) {
            if (rc != CURLE_OK) {
                r.error = (errbuf[0] ? std::string(errbuf) + " [" + curl_easy_strerror(rc) + "]"
                                     : std::string(curl_easy_strerror(rc)));
                if (tentativa > 1) {
                    r.error += " (apos " + std::to_string(tentativa) + " tentativas)";
                }
            }
            break;
        }
        // backoff exponencial: 1s, 2s, 4s...
        const auto espera = std::chrono::milliseconds(1000LL << (tentativa - 1));
        std::this_thread::sleep_for(espera);
    }

    ultimaReq = relogio::now();

    // O registro sai daqui, e não de quem chama: este é o único ponto por onde
    // TODA requisição passa, incluindo as que o login e o download fazem por
    // dentro. Registrar nos chamadores deixaria justamente as invisíveis de
    // fora — que são as que causam looping (RECON §2.2 e core/http/Trafego.h).
    EventoRequisicao ev;
    ev.quando = std::chrono::system_clock::now();
    ev.metodo = body ? "POST" : "GET";
    ev.url = url;
    if (!r.finalUrl.empty() && r.finalUrl != url) ev.urlFinal = r.finalUrl;
    ev.status = r.status;
    ev.bytes = r.body.size();
    ev.erro = r.error;
    ev.tentativas = tentativas;
    ev.esperaMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(saiu - entrou).count());
    ev.duracaoMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(ultimaReq - saiu).count());
    // Classificar custa quatro buscas de substring, e só faz sentido em HTML:
    // rodar isso sobre um PDF de 20 MB seria pagar caro por "Desconhecida".
    if (r.error.empty() && !r.ehDownload() && !r.body.empty()) {
        ev.pagina = std::string(toString(SigaaSession::classify(r.body)));
    }
    registrar(std::move(ev));

    return r;
}

CURLcode SigaaSession::Impl::executar(const std::string& url, const std::string* body,
                                      Response& r) {
    curl_easy_reset(curl);

    // ATENCAO: curl_easy_reset apaga TODAS as opcoes, inclusive o cookie
    // engine ligado no construtor. Sem reativar aqui, o jar seria esvaziado a
    // cada requisicao e a sessao nunca se manteria — falha silenciosa, porque
    // o HTTP continua 200 e so o conteudo vem deslogado.
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

    errbuf[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   // gzip/deflate
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r.cabecalhos);

    struct curl_slist* hdrs = nullptr;
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
        // JSF exige o content-type de form; o default do curl já é esse, mas
        // deixamos explícito para não depender de default.
        hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (hdrs) curl_slist_free_all(hdrs);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    char* eff = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK && eff) {
        r.finalUrl = eff;
    }
    return rc;
}

// ---------------------------------------------------------------------------

SigaaSession::SigaaSession(std::string baseUrl) : impl_(std::make_unique<Impl>()) {
    curlGlobal();
    // Vazio = a instituição escolhida no onboarding. Resolver aqui, e não num
    // argumento padrão do cabeçalho, é o que faz a escolha valer para os cinco
    // pontos do app que abrem sessão sem saber que existe instituição.
    impl_->baseUrl = baseUrl.empty() ? config::selecionada().baseUrl : std::move(baseUrl);
    impl_->curl = curl_easy_init();
    if (impl_->curl) {
        // Cookie engine em memória: "" liga o parsing sem ler de arquivo.
        // O JSESSIONID nunca toca o disco.
        curl_easy_setopt(impl_->curl, CURLOPT_COOKIEFILE, "");
    }
}

SigaaSession::~SigaaSession() {
    if (impl_ && impl_->curl) curl_easy_cleanup(impl_->curl);
}

Response SigaaSession::get(std::string_view path) {
    return impl_->perform(path, nullptr);
}

Response SigaaSession::postForm(std::string_view path, std::string_view body) {
    std::string b(body);
    return impl_->perform(path, &b);
}

bool SigaaSession::autenticado() const { return impl_->autenticado; }

Response SigaaSession::irParaPortal() {
    auto r = get("/sigaa/verPortalDiscente.do");
    if (r.ok() && classify(r.body) == PageKind::Portal) impl_->autenticado = true;
    return r;
}

bool SigaaSession::login(std::string_view cpf, std::string_view senha, std::string* erro) {
    auto fail = [&](std::string m) {
        if (erro) *erro = std::move(m);
        return false;
    };

    if (impl_->tentativasLogin >= impl_->maxTentativasLogin) {
        return fail("limite de tentativas de login atingido; o SIGAA bloqueia a conta "
                    "apos varias senhas erradas — corrija a credencial e reinicie");
    }
    ++impl_->tentativasLogin;

    // Busca a tela de login antes de postar. O POST em si nao precisa de CSRF
    // (RECON §1.8), mas o Tomcat espera um JSESSIONID ja estabelecido — no
    // browser ele sempre existe porque a pagina foi carregada antes. Sem este
    // GET, o login pode ser rejeitado por sessao inexistente.
    if (auto pre = get("/sigaa/logar.do?dispatch=logOff"); !pre.ok()) {
        return fail("nao consegui abrir a tela de login: " +
                    (pre.error.empty() ? "HTTP " + std::to_string(pre.status) : pre.error));
    }

    // Os 8 campos observados no browser (RECON §1.8).
    const jsf::Params campos{
        {"width", "1920"},
        {"height", "1080"},
        {"urlRedirect", ""},
        {"subsistemaRedirect", ""},
        {"acao", ""},
        {"acessibilidade", ""},
        {"user.login", std::string(cpf)},
        {"user.senha", std::string(senha)},
    };

    std::string body;
    for (const auto& [k, v] : campos) {
        if (!body.empty()) body.push_back('&');
        body += jsf::urlEncode(k);
        body.push_back('=');
        body += jsf::urlEncode(v);
    }

    auto r = postForm("/sigaa/logar.do?dispatch=logOn", body);
    if (!r.ok()) return fail("falha de rede no login: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error));

    switch (classify(r.body)) {
        case PageKind::Portal:
            impl_->autenticado = true;
            impl_->tentativasLogin = 0;
            return true;
        case PageKind::Login:
            return fail("credenciais rejeitadas (voltou para a tela de login)");
        default:
            // Pode ser tela de vinculo, termo de uso ou troca de senha —
            // nao observadas nesta conta, mas possiveis (RECON §1.8).
            return fail("login nao caiu no portal; pagina inesperada ("
                        + std::string(toString(classify(r.body))) + ")");
    }
}

// ---------------------------------------------------------------------------

PageKind SigaaSession::classify(std::string_view html) {
    // Ordem importa: expirada primeiro, senão uma página de erro que ainda
    // contém o menu seria classificada como portal.
    if (contains(html, "expirad") || contains(html, "ViewExpired") ||
        contains(html, "sess\xC3\xA3o expirou")) {
        return PageKind::SessaoExpirada;
    }
    if (contains(html, "user.senha") && contains(html, "user.login")) {
        return PageKind::Login;
    }
    if (contains(html, "id=\"formAva\"") || contains(html, "id='formAva'")) {
        return PageKind::TurmaVirtual;
    }
    if (contains(html, "formAtividades") || contains(html, "formAtualizacoesTurmas")) {
        return PageKind::Portal;
    }
    return PageKind::Desconhecida;
}

std::optional<int> SigaaSession::minutosSessaoRestantes(std::string_view html) {
    // O contador NAO e texto corrido: o rotulo e o valor vivem em elementos
    // separados —
    //   <div id="tempoSessao"><small><em>Tempo de Sessão:</em>
    //     <span id="spanRelogio" title="...">00:25</span>
    // Procurar "Tempo de Sessão: <numero>" nunca casa no HTML cru; so casaria
    // no texto ja renderizado. Por isso miramos o span diretamente.
    std::match_results<std::string_view::const_iterator> m;

    static const std::regex reSpan(
        R"(id=["']spanRelogio["'][^>]*>\s*(\d{1,2}):(\d{2}))");
    if (std::regex_search(html.begin(), html.end(), m, reSpan)) {
        return std::stoi(m[1].str()) * 60 + std::stoi(m[2].str());
    }

    // Fallback: texto ja renderizado (DOM salvo do browser, ou se o SIGAA
    // mudar o markup). Tolerante ao 'ã' em UTF-8 (2 bytes).
    static const std::regex reTexto(R"(Tempo de Sess.{1,3}o:\s*(\d{1,2}):(\d{2}))");
    if (std::regex_search(html.begin(), html.end(), m, reTexto)) {
        return std::stoi(m[1].str()) * 60 + std::stoi(m[2].str());
    }
    return std::nullopt;
}

void SigaaSession::setIntervaloMinimo(std::chrono::milliseconds ms) {
    impl_->intervalo = ms;
}
void SigaaSession::setUserAgent(std::string ua) { impl_->userAgent = std::move(ua); }
void SigaaSession::setMaxTentativasLogin(int n) { impl_->maxTentativasLogin = n; }

} // namespace sigaa::http
