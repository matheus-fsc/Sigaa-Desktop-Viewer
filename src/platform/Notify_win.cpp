// Notificação no Windows via Shell_NotifyIcon.
//
// Por que balão de bandeja e não toast WinRT: toast exige AppUserModelID
// registrado por um atalho no Menu Iniciar, ou seja, um instalador. Balão
// funciona de um .exe solto, e no Windows 10/11 o shell já o renderiza como
// toast. O custo é a limitação documentada em Notify.h.

#include "platform/Notify.h"

#include <windows.h>
#include <shellapi.h>

namespace sigaa::plat {
namespace {

constexpr UINT kMsgBandeja = WM_APP + 1;
constexpr UINT kIdIcone = 1;
constexpr UINT kIdTimer = 1;

// Teto de espera. O balão some sozinho antes disso; o timer só existe para o
// processo não ficar preso caso nenhuma notificação NIN_* chegue.
constexpr UINT kEsperaMs = 20000;

// Limites do NOTIFYICONDATAW. Estourar faz o shell truncar sem avisar.
constexpr size_t kMaxTitulo = 63;
constexpr size_t kMaxCorpo = 255;

std::wstring paraUtf16(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

// Corta em unidades UTF-16 sem partir um par substituto no meio — partir
// deixaria um caractere quebrado visível para o usuário.
void limitar(std::wstring& w, size_t max) {
    if (w.size() <= max) return;
    w.resize(max);
    if (!w.empty() && w.back() >= 0xD800 && w.back() <= 0xDBFF) w.pop_back();
    if (w.size() > 1) w.replace(w.size() - 1, 1, L"\x2026");   // reticências
}

void copiar(wchar_t* destino, size_t capacidade, const std::wstring& origem) {
    const size_t n = origem.size() < capacidade - 1 ? origem.size() : capacidade - 1;
    wmemcpy(destino, origem.c_str(), n);
    destino[n] = L'\0';
}

std::wstring g_aoClicar;

LRESULT CALLBACK janelaProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kMsgBandeja) {
        switch (LOWORD(lp)) {
            case NIN_BALLOONUSERCLICK:
                if (!g_aoClicar.empty()) {
                    ShellExecuteW(nullptr, L"open", g_aoClicar.c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                }
                PostQuitMessage(0);
                return 0;
            case NIN_BALLOONTIMEOUT:
                PostQuitMessage(0);
                return 0;
            default:
                return 0;
        }
    }
    if (msg == WM_TIMER && wp == kIdTimer) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

std::string backendNotificacao() { return "Shell_NotifyIcon (balao/toast)"; }

bool notificar(const Notificacao& n, std::string* erro) {
    const auto falhar = [erro](const char* m) {
        if (erro) *erro = m;
        return false;
    };

    const HINSTANCE inst = GetModuleHandleW(nullptr);
    static const wchar_t* kClasse = L"SigaaViewerNotify";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = janelaProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClasse;
    // Já registrada numa chamada anterior não é erro.
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return falhar("nao consegui registrar a classe de janela");
    }

    const HWND hwnd = CreateWindowExW(0, kClasse, L"", 0, 0, 0, 0, 0, nullptr,
                                      nullptr, inst, nullptr);
    if (!hwnd) return falhar("nao consegui criar a janela oculta");

    std::wstring titulo = paraUtf16(n.titulo);
    std::wstring corpo = paraUtf16(n.corpo);
    limitar(titulo, kMaxTitulo);
    limitar(corpo, kMaxCorpo);
    g_aoClicar = paraUtf16(n.aoClicar);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kIdIcone;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO | NIF_MESSAGE;
    nid.uCallbackMessage = kMsgBandeja;
    nid.hIcon = LoadIconW(nullptr, n.urgente ? IDI_WARNING : IDI_INFORMATION);
    nid.dwInfoFlags = n.urgente ? NIIF_WARNING : NIIF_INFO;
    copiar(nid.szTip, ARRAYSIZE(nid.szTip), L"sigaa-viewer");
    copiar(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), titulo);
    copiar(nid.szInfo, ARRAYSIZE(nid.szInfo), corpo);

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        DestroyWindow(hwnd);
        return falhar("Shell_NotifyIcon(NIM_ADD) falhou");
    }

    SetTimer(hwnd, kIdTimer, kEsperaMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, kIdTimer);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);
    return true;
}

} // namespace sigaa::plat
