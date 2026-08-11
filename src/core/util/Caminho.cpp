#include "core/util/Caminho.h"

#include "core/parse/Html.h"

namespace sigaa::util {

std::filesystem::path deUtf8(std::string_view s) {
    // `toUtf8` devolve a entrada intacta quando ela já é UTF-8, então o caminho
    // normal (a UI, que sempre manda UTF-8) não paga nada por esta defesa.
    const std::string u = html::toUtf8(s);
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(u.data()), u.size()));
}

std::string paraUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string nomeSeguro(std::string_view s) {
    static constexpr std::string_view kProibidos = "/\\:*?\"<>|";
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        // Bytes de continuação UTF-8 (>= 0x80) passam intactos: comparar
        // `char` com os proibidos ASCII nunca casa com eles.
        out.push_back(kProibidos.find(c) == std::string_view::npos ? c : '-');
    }
    return html::trim(out);
}

std::filesystem::path caminhoLivre(const std::filesystem::path& dir,
                                   const std::string& nomeUtf8) {
    const std::filesystem::path p = dir / deUtf8(nomeUtf8);
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

} // namespace sigaa::util
