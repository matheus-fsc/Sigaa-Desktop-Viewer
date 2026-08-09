// Cofre no Windows: Gerenciador de Credenciais (wincred), que é DPAPI com
// armazenamento e ciclo de vida resolvidos.
//
// Por que não DPAPI cru (CryptProtectData) num arquivo nosso: daria o mesmo
// nível de proteção e ainda nos deixaria com um arquivo para versionar,
// migrar e apagar. O Gerenciador de Credenciais também aparece no Painel de
// Controle, então o usuário consegue ver e remover o que guardamos sem depender
// de nós — o que é a diferença entre guardar a senha dele e sequestrá-la.

#include "platform/Credenciais.h"

#include <windows.h>
#include <wincred.h>

#include <vector>

namespace sigaa::plat {
namespace {

// CRED_TYPE_GENERIC + este alvo. Prefixo com barra é a convenção para
// credencial de aplicativo, e evita colidir com credencial de rede.
const wchar_t* kAlvo = L"sigaa-viewer/sigaa.unifei.edu.br";

// Campo de usuário deliberadamente genérico: o CPF vai CIFRADO dentro do blob,
// não aqui. `cmdkey /list` mostra UserName em claro.
const wchar_t* kUsuarioVisivel = L"SIGAA";

std::string ultimoErro(const char* oque) {
    return std::string(oque) + " (erro " + std::to_string(GetLastError()) + ")";
}

std::wstring paraUtf16(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string paraUtf8(const wchar_t* p, size_t len) {
    if (len == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, p, static_cast<int>(len),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, p, static_cast<int>(len), s.data(), n,
                        nullptr, nullptr);
    return s;
}

void zerar(std::wstring& w) {
    if (!w.empty()) SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t));
    w.clear();
}

} // namespace

std::string backendCofre() {
    return "Gerenciador de Credenciais do Windows (DPAPI)";
}

bool cofreDisponivel() { return true; }

bool guardarNoCofre(const std::string& login, const std::string& senha,
                    std::string* erro) {
    // login e senha no MESMO blob cifrado, separados por \n. O CPF é dado
    // pessoal e não pode ficar no campo de usuário, que é legível em claro.
    std::wstring blob = paraUtf16(login + "\n" + senha);

    if (blob.size() * sizeof(wchar_t) > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        if (erro) *erro = "credencial longa demais para o cofre";
        zerar(blob);
        return false;
    }

    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<wchar_t*>(kAlvo);
    c.UserName = const_cast<wchar_t*>(kUsuarioVisivel);
    c.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    c.CredentialBlobSize = static_cast<DWORD>(blob.size() * sizeof(wchar_t));
    // LOCAL_MACHINE e não ENTERPRISE: ENTERPRISE sincroniza a credencial com o
    // domínio/roaming profile, o que espalharia a senha por outras máquinas —
    // exatamente o oposto de "guardado localmente".
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.Comment = const_cast<wchar_t*>(L"Login do SIGAA usado pelo sigaa-viewer");

    const bool ok = CredWriteW(&c, 0) != FALSE;
    if (!ok && erro) *erro = ultimoErro("CredWriteW falhou");
    zerar(blob);
    return ok;
}

std::optional<Credenciais> lerDoCofre(std::string* erro) {
    PCREDENTIALW c = nullptr;
    if (!CredReadW(kAlvo, CRED_TYPE_GENERIC, 0, &c)) {
        // Não encontrado não é erro: é o estado de quem ainda não fez onboarding.
        if (GetLastError() != ERROR_NOT_FOUND && erro) {
            *erro = ultimoErro("CredReadW falhou");
        }
        return std::nullopt;
    }

    Credenciais out;
    if (c->CredentialBlob && c->CredentialBlobSize >= sizeof(wchar_t)) {
        const auto* p = reinterpret_cast<const wchar_t*>(c->CredentialBlob);
        const size_t n = c->CredentialBlobSize / sizeof(wchar_t);
        const std::string junto = paraUtf8(p, n);

        if (const auto q = junto.find('\n'); q != std::string::npos) {
            out.login = junto.substr(0, q);
            out.senha = junto.substr(q + 1);
        } else {
            out.login = junto;
        }
        // Apaga a cópia em claro que o próprio Windows nos entregou, antes de
        // devolver a memória.
        SecureZeroMemory(c->CredentialBlob, c->CredentialBlobSize);
    }
    CredFree(c);

    if (out.login.empty() || out.senha.empty()) return std::nullopt;
    return out;
}

bool apagarDoCofre(std::string* erro) {
    if (CredDeleteW(kAlvo, CRED_TYPE_GENERIC, 0)) return true;
    // Já não existir satisfaz a pós-condição.
    if (GetLastError() == ERROR_NOT_FOUND) return true;
    if (erro) *erro = ultimoErro("CredDeleteW falhou");
    return false;
}

} // namespace sigaa::plat
