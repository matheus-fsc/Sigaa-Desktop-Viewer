#include "core/parse/NoticiaParser.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <regex>

#include "core/jsf/JsfForm.h"
#include "core/parse/PortalParser.h"

namespace sigaa::parse {
namespace {

// O `id` avulso do jsfcljs do ícone "Visualizar" — a chave da notícia no
// SIGAA. O outro par do literal é o id do componente JSF, que é posicional.
std::string idDoOnclick(const std::string& onclick) {
    for (const auto& [k, v] : jsf::parseJsfcljsParams(onclick)) {
        if (k == "id") return v;
    }
    return {};
}

bool contem(const std::string& s, std::string_view parte) {
    return s.find(parte) != std::string::npos;
}

// "Notícias" chega decodificado pelo lexbor; o teste por pedaços ASCII ("Not"
// e "cias") não depende de como o acento veio (entidade, UTF-8 ou Latin-1).
bool ehRotuloNoticias(const std::string& s) {
    return contem(s, "Not") && contem(s, "cias");
}

std::string minusculasAscii(std::string s) {
    for (char& c : s) {
        if (static_cast<unsigned char>(c) < 0x80) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return s;
}

} // namespace

std::string limparHtmlNoticia(std::string_view html) {
    std::string entrada(html);

    // Script e style saem inteiros, com o conteúdo: o texto de um <style> não
    // é texto da notícia.
    static const std::regex blocos(R"(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)",
                                   std::regex::icase);
    entrada = std::regex_replace(entrada, blocos, "");

    static const std::regex tag(R"(<(/?)([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    static const std::regex href(R"(href\s*=\s*["']([^"']*)["'])", std::regex::icase);

    std::string saida;
    saida.reserve(entrada.size());
    int linksAbertos = 0;
    auto it = std::sregex_iterator(entrada.begin(), entrada.end(), tag);
    std::size_t ultimo = 0;
    for (; it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        saida.append(entrada, ultimo, static_cast<std::size_t>(m.position()) - ultimo);
        ultimo = static_cast<std::size_t>(m.position() + m.length());

        const bool fecha = !m[1].str().empty();
        std::string nome = minusculasAscii(m[2].str());
        // Sinônimos antigos viram a forma de sempre: o QTextBrowser lê os dois,
        // mas uma forma só deixa a saída previsível para teste.
        if (nome == "b") nome = "strong";
        if (nome == "i") nome = "em";

        if (nome == "p" || nome == "strong" || nome == "em" || nome == "u" ||
            nome == "ul" || nome == "ol" || nome == "li") {
            saida += fecha ? "</" + nome + ">" : "<" + nome + ">";
        } else if (nome == "br") {
            saida += "<br>";
        } else if (nome == "a") {
            if (fecha) {
                if (linksAbertos > 0) {
                    saida += "</a>";
                    --linksAbertos;
                }
            } else {
                std::smatch h;
                const std::string atributos = m[3].str();
                // Só http(s): um href="javascript:..." colado no editor viraria
                // um link clicável que roda código ao ser aberto.
                if (std::regex_search(atributos, h, href) &&
                    (h[1].str().rfind("http://", 0) == 0 ||
                     h[1].str().rfind("https://", 0) == 0)) {
                    std::string destino = h[1].str();
                    // O href vai entre aspas duplas na saída; uma aspa dentro
                    // dele encerraria o atributo.
                    std::string limpo;
                    for (char c : destino) limpo += (c == '"') ? std::string("%22") : std::string(1, c);
                    saida += "<a href=\"" + limpo + "\">";
                    ++linksAbertos;
                }
            }
        }
        // Qualquer outra tag (span, font, div, img, table...) cai; o texto
        // dentro dela fica, porque foi copiado antes de cada tag.
    }
    saida.append(entrada, ultimo, std::string::npos);
    while (linksAbertos-- > 0) saida += "</a>";

    // Parágrafos vazios que o editor deixa entre os de verdade ("<p>&nbsp;</p>")
    // viram só espaço em branco na tela; tirá-los mantém o texto compacto.
    static const std::regex pVazio(R"(<p>(?:\s|&nbsp;|\xC2\xA0)*</p>)");
    saida = std::regex_replace(saida, pVazio, "");
    return html::trim(saida);
}

std::string textoDaNoticia(std::string_view htmlLimpo) {
    static const std::regex tag(R"(<[^>]*>)");
    std::string t = std::regex_replace(std::string(htmlLimpo), tag, " ");
    const std::pair<const char*, const char*> entidades[] = {
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"\xC2\xA0", " "}};
    for (const auto& [de, para] : entidades) {
        for (std::size_t p = t.find(de); p != std::string::npos; p = t.find(de, p)) {
            t.replace(p, std::strlen(de), para);
            p += std::strlen(para);
        }
    }
    return html::collapseWhitespace(t);
}

std::string chaveNoticia(const std::string& idTurma, const std::string& titulo,
                         const DateTime& data) {
    char dia[16] = "";
    if (data.valid()) {
        std::snprintf(dia, sizeof dia, "%04d-%02d-%02d", data.year, data.month, data.day);
    }
    return idTurma + "|" + minusculasAscii(html::collapseWhitespace(titulo)) + "|" + dia;
}

ListaNoticias parseListaNoticias(const html::Document& doc) {
    ListaNoticias out;

    // "Notícias" no legend é o que separa esta aba das outras, que usam a
    // mesma moldura (fieldset + table.listing).
    for (const auto& l : doc.select("fieldset legend")) {
        if (ehRotuloNoticias(l.text())) out.pareceAbaNoticias = true;
    }
    if (!doc.select("p.empty-listing").empty()) out.vazioConfirmado = true;
    if (!out.pareceAbaNoticias) return out;

    for (const auto& linha : doc.select("table.listing tbody tr")) {
        const auto tds = linha.select("td");
        if (tds.size() < 3) continue;

        const auto a = linha.selectFirst("a[onclick]");
        if (!a) continue;
        ItemNoticia it;
        it.id = idDoOnclick(a.attr("onclick"));
        it.titulo = html::collapseWhitespace(tds[0].text());
        it.data = parseDataHora(html::collapseWhitespace(tds[1].text()));
        // Sem id não há como abrir o texto; sem título não há o que mostrar.
        if (it.id.empty() || it.titulo.empty()) continue;
        out.itens.push_back(std::move(it));
    }
    return out;
}

DetalheNoticia parseDetalheNoticia(const html::Document& doc) {
    DetalheNoticia out;
    for (const auto& l : doc.select("fieldset legend")) {
        const std::string t = l.text();
        // "Visualização de Notícia", no singular.
        if (contem(t, "Visualiza") && contem(t, "Not")) out.pareceDetalhe = true;
    }
    if (!out.pareceDetalhe) return out;

    // "Título:" e "Data:" são <li> com <label> e o valor num <span>.
    for (const auto& li : doc.select("ul.form li")) {
        const auto label = li.selectFirst("label");
        const auto valor = li.selectFirst("span");
        if (!label || !valor) continue;
        const std::string rotulo = label.text();
        const std::string texto = html::collapseWhitespace(valor.text());
        if (contem(rotulo, "tulo")) out.titulo = texto;          // "Título:"
        else if (contem(rotulo, "Data")) out.data = parseDataHora(texto);
    }
    if (const auto c = doc.selectFirst("td.conteudoNoticia")) {
        out.conteudoHtml = limparHtmlNoticia(c.innerHtml());
    }
    return out;
}

std::optional<Noticia> parseUltimaNoticia(const html::Document& docTurma,
                                          const std::string& idTurma,
                                          const std::string& turmaNome) {
    const auto bloco = docTurma.selectFirst("div#ultimaNoticia");
    if (!bloco) return std::nullopt;
    const auto h4 = bloco.selectFirst("h4");
    if (!h4) return std::nullopt;

    // "Última Notícia <br> TÍTULO - 30/09/2026 14:51". O título pode ter " - "
    // no meio, então a data é o ÚLTIMO trecho depois de " - ".
    std::string cab = html::collapseWhitespace(h4.text());
    const auto posNoticia = cab.find("cia ");   // fim de "Notícia "
    if (posNoticia != std::string::npos && posNoticia < 24) cab.erase(0, posNoticia + 4);

    static const std::regex fim(R"(^(.*\S)\s+-\s+(\d{2}/\d{2}/\d{4}(?:\s+\d{1,2}:\d{2})?)\s*$)");
    std::smatch m;
    if (!std::regex_match(cab, m, fim)) return std::nullopt;

    Noticia n;
    n.idTurma = idTurma;
    n.turmaNome = turmaNome;
    n.titulo = m[1].str();
    n.data = parseDataHora(m[2].str());
    if (const auto c = bloco.selectFirst("span.conteudoNoticia")) {
        n.conteudoHtml = limparHtmlNoticia(c.innerHtml());
    }
    if (const auto autor = bloco.selectFirst("small i")) {
        n.autor = html::collapseWhitespace(autor.text());
    }
    if (n.titulo.empty() || !n.data.valid()) return std::nullopt;
    return n;
}

} // namespace sigaa::parse
