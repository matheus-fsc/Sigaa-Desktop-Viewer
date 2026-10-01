#include "core/util/Texto.h"

#include <cctype>

namespace sigaa::util {
namespace {

// O segundo byte de "Ã?" (U+00C0..U+00FF em UTF-8 é C3 80..C3 BF) para a
// letra base minúscula. 0 = não é letra que se dobre (×, ÷).
char baseLatina(unsigned char b) {
    static const char* tabela =
        // C3 80..8F: À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
        "aaaaaaaceeeeiiii"
        // C3 90..9F: Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
        "dnooooo\0ouuuuyts"
        // C3 A0..AF: à á â ã ä å æ ç è é ê ë ì í î ï
        "aaaaaaaceeeeiiii"
        // C3 B0..BF: ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
        "dnooooo\0ouuuuyty";
    if (b < 0x80 || b > 0xBF) return 0;
    return tabela[b - 0x80];
}

} // namespace

std::string dobrar(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool espaco = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        char saida = 0;
        if (c == 0xC3 && i + 1 < s.size()) {
            const char b = baseLatina(static_cast<unsigned char>(s[i + 1]));
            if (b) {
                saida = b;
                ++i;
            }
        }
        if (!saida) {
            if (c < 0x80) {
                saida = static_cast<char>(std::tolower(c));
            } else {
                // Outro UTF-8: copia o byte como está.
                if (espaco && !out.empty()) out += ' ';
                espaco = false;
                out += static_cast<char>(c);
                continue;
            }
        }
        if (std::isspace(static_cast<unsigned char>(saida))) {
            espaco = true;
            continue;
        }
        if (espaco && !out.empty()) out += ' ';
        espaco = false;
        out += saida;
    }
    return out;
}

bool contemDobrado(std::string_view palheiro, std::string_view agulha) {
    return dobrar(palheiro).find(dobrar(agulha)) != std::string::npos;
}

} // namespace sigaa::util
