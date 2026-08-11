#pragma once
// Sessão HTTP com o SIGAA.
//
// Três invariantes que vêm do recon (docs/RECON.md) e NÃO são negociáveis:
//
//  1. UMA requisição por vez. O SIGAA invalida a view se detectar navegação
//     concorrente ("múltiplas abas"). Nada de paralelizar o crawl.
//  2. Intervalo mínimo entre requisições. São os dados do próprio usuário, mas
//     o cliente não pode parecer um ataque.
//  3. Limite rígido de tentativas de login. Senha errada em loop bloqueia a
//     conta do aluno no SIGAA — o dano é do usuário, não nosso.

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sigaa::http {

struct Response {
    long status{0};
    std::string finalUrl;   // após redirects
    std::string body;       // bytes crus, sem transcodificar (pode ser binário)
    std::string error;      // mensagem do libcurl, vazia em sucesso

    // Cabeçalhos da resposta, chave já em minúsculas. Existem por causa do
    // download de material: o nome real do arquivo só vem no
    // Content-Disposition, e adivinhá-lo pelo título da tabela daria
    // "Notas de Aula - Aula 01" sem extensão nenhuma.
    std::map<std::string, std::string> cabecalhos;

    // "" se ausente. `nome` deve vir em minúsculas.
    std::string cabecalho(const std::string& nome) const;

    bool ok() const { return error.empty() && status >= 200 && status < 400; }

    // Nome sugerido pelo Content-Disposition, já sem caminho e sem caractere
    // proibido em nome de arquivo. Vazio se o servidor não mandou.
    std::string nomeSugerido() const;

    // O servidor mandou um arquivo, e não uma página? Serve para detectar a
    // sessão expirada: o SIGAA responde 200 com HTML de login em vez do PDF, e
    // salvar isso no disco daria um "apostila.pdf" que na verdade é uma tela de
    // login — o pior tipo de erro, porque só aparece na hora de abrir.
    bool ehDownload() const;
};

// O SIGAA responde tudo em 200 e não muda a URL nos postbacks (RECON §1.4),
// então a única forma confiável de saber onde estamos é olhar o conteúdo.
enum class PageKind {
    Portal,          // #formAtividades + #formAtualizacoesTurmas
    TurmaVirtual,    // #formAva
    Login,           // form com user.login / user.senha
    SessaoExpirada,
    Desconhecida
};

std::string_view toString(PageKind k);

// Versão do libcurl e backend TLS em uso. O backend importa: sem um, ou sem
// bundle de CA, todo https falha com "SSL connect error" — e o erro não diz
// qual das duas coisas é.
std::string infoBackend();

class SigaaSession {
public:
    // Vazio = a instituição selecionada (core/config/Instituicao.h). Não é um
    // literal aqui porque este app deixou de ser só da UNIFEI.
    explicit SigaaSession(std::string baseUrl = {});
    ~SigaaSession();
    SigaaSession(const SigaaSession&) = delete;
    SigaaSession& operator=(const SigaaSession&) = delete;

    // --- requisições cruas (já serializadas e com rate limit) -------------
    Response get(std::string_view path);
    Response postForm(std::string_view path, std::string_view urlEncodedBody);

    // --- fluxos de alto nível ---------------------------------------------

    // POST /sigaa/logar.do?dispatch=logOn — 8 campos, sem CSRF (RECON §1.8).
    // Retorna false e preenche `erro` se não cair no portal.
    bool login(std::string_view cpf, std::string_view senha, std::string* erro = nullptr);

    // GET /sigaa/verPortalDiscente.do — devolve o portal com ViewState novo
    // sem precisar re-logar. É a primitiva de reset da navegação (RECON §1.1).
    Response irParaPortal();

    bool autenticado() const;

    // --- classificação ----------------------------------------------------
    static PageKind classify(std::string_view html);
    // Lê "Tempo de Sessão: 00:25" do cabeçalho (RECON §1.7).
    static std::optional<int> minutosSessaoRestantes(std::string_view html);

    // --- configuração -----------------------------------------------------
    void setIntervaloMinimo(std::chrono::milliseconds ms);
    void setUserAgent(std::string ua);
    void setMaxTentativasLogin(int n);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sigaa::http
