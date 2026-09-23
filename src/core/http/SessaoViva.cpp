#include "core/http/SessaoViva.h"

namespace sigaa::http {
namespace {

bool venceu(std::chrono::steady_clock::time_point ultimoUso) {
    if (ultimoUso.time_since_epoch().count() == 0) return true;
    return std::chrono::steady_clock::now() - ultimoUso > SessaoViva::kValidade;
}

} // namespace

SigaaSession* SessaoViva::obter(const std::string& login, const std::string& senha,
                                std::string* erro) {
    std::lock_guard<std::mutex> g(mtx_);

    // Reuso: o caminho que este arquivo existe para tornar comum.
    if (sessao_ && sessao_->autenticado() && !venceu(ultimoUso_)) {
        // O relógio é renovado no USO, não na criação: é a última requisição
        // que conta para o timeout do SIGAA, não o login.
        ultimoUso_ = std::chrono::steady_clock::now();
        return sessao_.get();
    }

    // Sessão velha: o SIGAA provavelmente já a descartou. Encerrar antes de
    // abrir a próxima é uma requisição que quase sempre não muda nada — e
    // deixar a anterior viva foi o que travou o login. Só quando ainda parece
    // autenticada; se nem isso, não há o que encerrar.
    if (sessao_ && sessao_->autenticado()) sessao_->logout();
    sessao_.reset();

    auto nova = std::make_unique<SigaaSession>();
    if (!nova->login(login, senha, erro)) return nullptr;

    sessao_ = std::move(nova);
    ultimoUso_ = std::chrono::steady_clock::now();
    return sessao_.get();
}

void SessaoViva::invalidar() {
    std::lock_guard<std::mutex> g(mtx_);
    // Sem logout: a sessão já morreu no servidor, e mandar logOff numa sessão
    // inexistente é gastar uma requisição para receber a tela de login.
    sessao_.reset();
    ultimoUso_ = {};
}

void SessaoViva::encerrar() {
    std::lock_guard<std::mutex> g(mtx_);
    if (sessao_ && sessao_->autenticado()) sessao_->logout();
    sessao_.reset();
    ultimoUso_ = {};
}

bool SessaoViva::viva() const {
    std::lock_guard<std::mutex> g(mtx_);
    return sessao_ && sessao_->autenticado() && !venceu(ultimoUso_);
}

std::chrono::seconds SessaoViva::restante() const {
    std::lock_guard<std::mutex> g(mtx_);
    if (!sessao_ || !sessao_->autenticado()) return std::chrono::seconds{0};
    if (ultimoUso_.time_since_epoch().count() == 0) return std::chrono::seconds{0};

    const auto decorrido = std::chrono::steady_clock::now() - ultimoUso_;
    return std::chrono::duration_cast<std::chrono::seconds>(kValidade - decorrido);
}

} // namespace sigaa::http
