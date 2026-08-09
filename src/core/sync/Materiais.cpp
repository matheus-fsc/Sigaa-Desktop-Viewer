#include "core/sync/Materiais.h"

#include <filesystem>
#include <fstream>

#include "core/jsf/JsfForm.h"
#include "core/parse/ArquivoParser.h"
#include "core/parse/Html.h"

namespace sigaa::sync {
namespace {

void falhar(std::string* erro, std::string msg) {
    if (erro) *erro = std::move(msg);
}

// Todo std::string neste projeto é UTF-8. std::filesystem::path, porém,
// interpreta um `char*` na code page ANSI do sistema quando construído no
// Windows — e "PROJETO E ANÁLISE" (C3 81 em UTF-8) vira "PROJETO E ANÃLISE"
// no disco, uma segunda pasta ao lado da certa. O construtor de char8_t é o
// que diz ao path "estes bytes são UTF-8".
//
// Tolera receber cp1252 em vez de UTF-8: no Windows o `argv` do CLI chega na
// code page ANSI, e o construtor de char8_t com bytes inválidos ABORTA o
// processo. `toUtf8` devolve a entrada intacta quando ela já é UTF-8, então o
// caminho normal (a UI, que manda UTF-8) não paga nada por isto.
std::filesystem::path deUtf8(std::string_view s) {
    const std::string u = html::toUtf8(s);
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(u.data()), u.size()));
}

// E a volta: `path::string()` no Windows converte para a code page ANSI e
// perde o acento que não couber nela. Quem recebe o caminho de volta é a UI,
// que espera UTF-8 como todo std::string daqui.
std::string paraUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// Nome de arquivo que não existe ainda: "apostila.pdf", "apostila (2).pdf"...
// Sobrescrever silenciosamente seria perder a versão que o aluno já tinha
// anotado — e o SIGAA reaproveita nome entre semestres.
std::filesystem::path caminhoLivre(const std::filesystem::path& dir,
                                   const std::string& nome) {
    const std::filesystem::path p = dir / deUtf8(nome);
    if (!std::filesystem::exists(p)) return p;

    // Concatenar como `path`, não como string: `p.stem().string()` já teria
    // achatado o acento na code page ANSI, e o sufixo (2) recriaria o mojibake
    // que `deUtf8` acabou de evitar.
    const std::filesystem::path base = p.stem();
    const std::filesystem::path ext = p.extension();
    for (int i = 2; i < 1000; ++i) {
        std::filesystem::path cand = dir / base;
        cand += deUtf8(" (" + std::to_string(i) + ")");
        cand += ext;
        if (!std::filesystem::exists(cand)) return cand;
    }
    return p;
}

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
    naTurma_ = true;
    return true;
}

bool SessaoTurma::recarregarAbaArquivos(std::string* erro) {
    // Pelo RÓTULO, nunca pelo id do componente: `formMenu:j_id_jsp_..._123` é
    // posicional e muda quando a instituição recompila o JSP — e o SIGAA
    // responderia 200 com a aba errada, sem erro nenhum (RECON §1.6.1).
    auto cmd = jsf::findCommandByLabel(docTurma_, "Arquivos");
    if (!cmd) {
        falhar(erro, "esta turma nao tem a aba Arquivos no menu");
        return false;
    }
    const auto form = jsf::parseForm(docTurma_, cmd->formId);
    if (!form) {
        falhar(erro, "form " + cmd->formId + " nao encontrado");
        return false;
    }
    auto ra = sessao_.postForm(form->action, form->buildPostBody(cmd->params));
    if (!ra.ok()) {
        falhar(erro, ra.error.empty() ? "HTTP " + std::to_string(ra.status) : ra.error);
        return false;
    }
    if (!docArquivos_.parse(ra.body)) {
        falhar(erro, "parse da aba Arquivos falhou");
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
