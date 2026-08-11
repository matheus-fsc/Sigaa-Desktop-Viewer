#include "core/sync/Materiais.h"

#include <filesystem>
#include <fstream>

#include "core/jsf/JsfForm.h"
#include "core/parse/ArquivoParser.h"
#include "core/parse/Html.h"
#include "core/parse/TurmaParser.h"
#include "core/util/Caminho.h"

namespace sigaa::sync {
namespace {

void falhar(std::string* erro, std::string msg) {
    if (erro) *erro = std::move(msg);
}

using util::caminhoLivre;
using util::deUtf8;
using util::paraUtf8;

} // namespace

bool SessaoTurma::entrar(const Turma& turma, std::string* erro) {
    naTurma_ = false;
    if (turma.frontEndId.empty()) {
        falhar(erro, "turma sem frontEndIdTurma — nao da para entrar nela");
        return false;
    }
    turma_ = turma;

    // Sempre a partir do portal: os comandos só valem na página onde foram
    // lidos, e um GET aqui devolve ViewState novo sem re-login (RECON §1.1).
    auto rp = sessao_.irParaPortal();
    if (!rp.ok()) {
        falhar(erro, rp.error.empty() ? "HTTP " + std::to_string(rp.status) : rp.error);
        return false;
    }
    html::Document portal;
    if (!portal.parse(rp.body)) {
        falhar(erro, "parse do portal falhou");
        return false;
    }

    const jsf::Command* cmd = nullptr;
    const auto cmds = jsf::findCommands(portal);
    for (const auto& c : cmds) {
        for (const auto& [k, v] : c.params) {
            if (k == "frontEndIdTurma" && v == turma.frontEndId) cmd = &c;
        }
        if (cmd) break;
    }
    if (!cmd) {
        falhar(erro, "nao achei o link da turma no portal");
        return false;
    }
    const auto form = jsf::parseForm(portal, cmd->formId);
    if (!form) {
        falhar(erro, "form " + cmd->formId + " nao encontrado");
        return false;
    }

    auto rt = sessao_.postForm(form->action, form->buildPostBody(cmd->params));
    if (!rt.ok()) {
        falhar(erro, rt.error.empty() ? "HTTP " + std::to_string(rt.status) : rt.error);
        return false;
    }
    if (!docTurma_.parse(rt.body)) {
        falhar(erro, "parse da turma falhou");
        return false;
    }
    if (http::SigaaSession::classify(rt.body) == http::PageKind::Login) {
        falhar(erro, "a sessao expirou");
        return false;
    }

    menu_.clear();
    for (const auto& c : jsf::findCommands(docTurma_)) {
        if (c.formId == "formMenu" && !c.label.empty()) menu_.push_back(c.label);
    }

    // A resposta que acabou de chegar É a linha do tempo das aulas. Parsear
    // aqui não custa requisição nenhuma; deixar para depois custaria uma.
    conteudo_ = parse::parseTurmaVirtual(docTurma_, turma_.idTurma, turma_.nome,
                                         turma_.periodo);

    naTurma_ = true;
    return true;
}

bool abrirAbaPorRotulo(http::SigaaSession& sessao, const html::Document& docTurma,
                       const std::string& rotulo, html::Document* saida,
                       std::string* erro) {
    auto cmd = jsf::findCommandByLabel(docTurma, rotulo);
    if (!cmd) {
        falhar(erro, "esta turma nao tem a aba " + rotulo + " no menu");
        return false;
    }
    const auto form = jsf::parseForm(docTurma, cmd->formId);
    if (!form) {
        falhar(erro, "form " + cmd->formId + " nao encontrado");
        return false;
    }
    auto ra = sessao.postForm(form->action, form->buildPostBody(cmd->params));
    if (!ra.ok()) {
        falhar(erro, ra.error.empty() ? "HTTP " + std::to_string(ra.status) : ra.error);
        return false;
    }
    if (!saida || !saida->parse(ra.body)) {
        falhar(erro, "parse da aba " + rotulo + " falhou");
        return false;
    }
    return true;
}

bool SessaoTurma::recarregarAbaArquivos(std::string* erro) {
    if (!abrirAbaPorRotulo(sessao_, docTurma_, "Arquivos", &docArquivos_, erro)) {
        return false;
    }

    const auto lista = parse::parseArquivos(docArquivos_, turma_.idTurma, turma_.nome);
    if (!lista.pareceAbaArquivos && lista.arquivos.empty()) {
        // Nem a moldura da aba veio. Chamar isso de "sem arquivos" ensinaria o
        // aluno a acreditar que a turma não tem material.
        falhar(erro, "a resposta nao parece a aba Arquivos (sessao expirada?)");
        return false;
    }
    arquivos_ = lista.arquivos;
    vazioConfirmado_ = lista.vazioConfirmado;
    return true;
}

bool SessaoTurma::abrirArquivos(std::string* erro) {
    if (!naTurma_) {
        falhar(erro, "chame entrar() antes");
        return false;
    }
    return recarregarAbaArquivos(erro);
}

std::optional<std::string> SessaoTurma::baixar(const std::string& idArquivo,
                                               const std::string& diretorio,
                                               std::string* erro) {
    if (!naTurma_) {
        falhar(erro, "chame entrar() antes");
        return std::nullopt;
    }

    // Duas tentativas: a primeira usa a view que já está carregada; se o SIGAA
    // devolver HTML em vez do arquivo, a view envelheceu e recarregamos a aba
    // uma vez. Sem isto, baixar o segundo arquivo da lista falharia com um
    // "PDF" que na verdade é uma página de erro.
    for (int tentativa = 0; tentativa < 2; ++tentativa) {
        const jsf::Command* cmd = nullptr;
        const auto cmds = jsf::findCommands(docArquivos_);
        for (const auto& c : cmds) {
            for (const auto& [k, v] : c.params) {
                if (k == "id" && v == idArquivo) cmd = &c;
            }
            if (cmd) break;
        }
        if (!cmd) {
            falhar(erro, "arquivo " + idArquivo + " nao esta na pagina");
            return std::nullopt;
        }
        const auto form = jsf::parseForm(docArquivos_, cmd->formId);
        if (!form) {
            falhar(erro, "form " + cmd->formId + " nao encontrado");
            return std::nullopt;
        }

        auto r = sessao_.postForm(form->action, form->buildPostBody(cmd->params));
        if (!r.ok()) {
            falhar(erro, r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
            return std::nullopt;
        }

        if (!r.ehDownload()) {
            if (tentativa == 0 && recarregarAbaArquivos(nullptr)) continue;
            falhar(erro, "o SIGAA devolveu uma pagina em vez do arquivo "
                         "(sessao expirada, ou o professor removeu o material)");
            return std::nullopt;
        }
        if (r.body.empty()) {
            falhar(erro, "o arquivo veio vazio");
            return std::nullopt;
        }

        std::string nome = r.nomeSugerido();
        if (nome.empty()) nome = "material-" + idArquivo;

        const auto pasta = deUtf8(diretorio);
        std::error_code ec;
        std::filesystem::create_directories(pasta, ec);
        const auto destino = caminhoLivre(pasta, nome);

        std::ofstream f(destino, std::ios::binary);
        if (!f) {
            falhar(erro, "nao consegui escrever em " + paraUtf8(destino));
            return std::nullopt;
        }
        f.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
        if (!f) {
            falhar(erro, "escrita incompleta em " + paraUtf8(destino));
            return std::nullopt;
        }
        f.close();

        // A view foi consumida pelo download. Recarregar agora, e não na
        // próxima chamada, deixa o objeto pronto para o arquivo seguinte.
        recarregarAbaArquivos(nullptr);
        return paraUtf8(destino);
    }
    return std::nullopt;
}

} // namespace sigaa::sync
