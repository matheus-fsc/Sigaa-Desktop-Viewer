#include "core/parse/Html.h"

#include <lexbor/css/css.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>

#include <cctype>

namespace sigaa::html {
namespace {

inline const lxb_char_t* uc(std::string_view s) {
    return reinterpret_cast<const lxb_char_t*>(s.data());
}

std::string fromLx(const lxb_char_t* p, size_t len) {
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p), len);
}

struct CollectCtx {
    std::vector<Node>* out;
    bool firstOnly;
};

lxb_status_t collectCb(lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* ctx) {
    auto* c = static_cast<CollectCtx*>(ctx);
    c->out->emplace_back(node);
    return c->firstOnly ? LXB_STATUS_STOP : LXB_STATUS_OK;
}

// Executa um seletor CSS a partir de um nó-raiz arbitrário.
std::vector<Node> runSelector(lxb_dom_node_t* root, std::string_view sel, bool firstOnly) {
    std::vector<Node> out;
    if (!root || sel.empty()) return out;

    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK) {
        lxb_css_parser_destroy(parser, true);
        return out;
    }

    lxb_selectors_t* selectors = lxb_selectors_create();
    if (lxb_selectors_init(selectors) != LXB_STATUS_OK) {
        lxb_selectors_destroy(selectors, true);
        lxb_css_parser_destroy(parser, true);
        return out;
    }

    lxb_css_selector_list_t* list =
        lxb_css_selectors_parse(parser, uc(sel), sel.size());

    if (list) {
        CollectCtx ctx{&out, firstOnly};
        lxb_selectors_find(selectors, root, list, collectCb, &ctx);
        lxb_css_selector_list_destroy_memory(list);
    }

    lxb_selectors_destroy(selectors, true);
    lxb_css_parser_destroy(parser, true);
    return out;
}

} // namespace

// --------------------------------------------------------------------------
// utilitários de texto
// --------------------------------------------------------------------------

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    auto isSp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (b < e && isSp(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isSp(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string collapseWhitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool inSpace = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            inSpace = true;
        } else {
            if (inSpace && !out.empty()) out.push_back(' ');
            inSpace = false;
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// --------------------------------------------------------------------------
// encoding
// --------------------------------------------------------------------------

bool isValidUtf8(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const auto* end = p + s.size();
    while (p < end) {
        if (*p < 0x80) { ++p; continue; }
        int extra;
        unsigned cp;
        if ((*p & 0xE0) == 0xC0) { extra = 1; cp = *p & 0x1Fu; }
        else if ((*p & 0xF0) == 0xE0) { extra = 2; cp = *p & 0x0Fu; }
        else if ((*p & 0xF8) == 0xF0) { extra = 3; cp = *p & 0x07u; }
        else return false;

        if (p + extra >= end) return false;
        for (int i = 1; i <= extra; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        // rejeita overlong e surrogates
        if (extra == 1 && cp < 0x80) return false;
        if (extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
        if (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
        p += extra + 1;
    }
    return true;
}

namespace {

// windows-1252 difere de latin-1 apenas em 0x80–0x9F.
constexpr unsigned kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string cp1252ToUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0xA0) appendUtf8(out, kCp1252High[c - 0x80]);
        else appendUtf8(out, c);   // 0xA0–0xFF == latin-1 == mesmo code point
    }
    return out;
}

} // namespace

std::string toUtf8(std::string_view htmlText) {
    // Bytes válidos em UTF-8 são deixados como estão. A checagem é barata e
    // evita corromper páginas que realmente venham em UTF-8 — o SIGAA pode
    // mudar isso num upgrade, e aí nada quebra.
    if (isValidUtf8(htmlText)) return std::string(htmlText);
    return cp1252ToUtf8(htmlText);
}

// --------------------------------------------------------------------------
// Node
// --------------------------------------------------------------------------

std::string Node::rawText() const {
    if (!raw_) return {};
    size_t len = 0;
    lxb_char_t* txt =
        lxb_dom_node_text_content(static_cast<lxb_dom_node_t*>(raw_), &len);
    if (!txt) return {};
    std::string s = fromLx(txt, len);
    lxb_dom_document_destroy_text(static_cast<lxb_dom_node_t*>(raw_)->owner_document, txt);
    return s;
}

std::string Node::text() const { return collapseWhitespace(rawText()); }

std::string Node::textoVisivel() const {
    if (!raw_) return {};

    // Percurso manual em vez de lxb_dom_node_text_content: é o único jeito de
    // pular uma subárvore inteira. Iterativo porque um tópico de aula chega a
    // ter dezenas de níveis de <span> aninhado gerado pelo JSF.
    std::string out;
    std::vector<lxb_dom_node_t*> pilha;
    for (auto* c = static_cast<lxb_dom_node_t*>(raw_)->last_child; c; c = c->prev) {
        pilha.push_back(c);
    }
    while (!pilha.empty()) {
        lxb_dom_node_t* n = pilha.back();
        pilha.pop_back();

        if (n->type == LXB_DOM_NODE_TYPE_TEXT) {
            const lexbor_str_t& d = lxb_dom_interface_text(n)->char_data.data;
            out += fromLx(d.data, d.length);
            continue;
        }
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;

        const std::string tag = Node(n).tagName();
        if (tag == "script" || tag == "style") continue;

        for (auto* c = n->last_child; c; c = c->prev) pilha.push_back(c);
    }
    return collapseWhitespace(out);
}

std::string Node::attr(std::string_view name) const {
    if (!raw_) return {};
    auto* node = static_cast<lxb_dom_node_t*>(raw_);
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return {};
    auto* el = lxb_dom_interface_element(node);
    size_t len = 0;
    const lxb_char_t* v = lxb_dom_element_get_attribute(el, uc(name), name.size(), &len);
    return fromLx(v, len);
}

bool Node::hasAttr(std::string_view name) const {
    if (!raw_) return false;
    auto* node = static_cast<lxb_dom_node_t*>(raw_);
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return false;
    return lxb_dom_element_has_attribute(lxb_dom_interface_element(node),
                                         uc(name), name.size());
}

std::string Node::tagName() const {
    if (!raw_) return {};
    auto* node = static_cast<lxb_dom_node_t*>(raw_);
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return {};
    size_t len = 0;
    const lxb_char_t* n =
        lxb_dom_element_qualified_name(lxb_dom_interface_element(node), &len);
    return fromLx(n, len);
}

std::string Node::innerHtml() const {
    if (!raw_) return {};
    lexbor_str_t str{};
    // 4096: chute inicial; lexbor cresce o buffer conforme necessário.
    if (lexbor_str_init(&str, static_cast<lxb_dom_node_t*>(raw_)->owner_document->text,
                        4096) == nullptr) {
        return {};
    }
    lxb_html_serialize_deep_str(static_cast<lxb_dom_node_t*>(raw_), &str);
    std::string out = fromLx(str.data, str.length);
    lexbor_str_destroy(&str, static_cast<lxb_dom_node_t*>(raw_)->owner_document->text, false);
    return out;
}

std::vector<Node> Node::select(std::string_view sel) const {
    return runSelector(static_cast<lxb_dom_node_t*>(raw_), sel, false);
}

Node Node::selectFirst(std::string_view sel) const {
    auto v = runSelector(static_cast<lxb_dom_node_t*>(raw_), sel, true);
    return v.empty() ? Node{} : v.front();
}

// --------------------------------------------------------------------------
// Document
// --------------------------------------------------------------------------

struct Document::Impl {
    lxb_html_document_t* doc{nullptr};

    ~Impl() {
        if (doc) lxb_html_document_destroy(doc);
    }
};

Document::Document() : impl_(std::make_unique<Impl>()) {}
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

bool Document::parse(std::string_view htmlText) {
    if (impl_->doc) {
        lxb_html_document_destroy(impl_->doc);
        impl_->doc = nullptr;
    }
    impl_->doc = lxb_html_document_create();
    if (!impl_->doc) return false;

    // O SIGAA serve windows-1252; sem isto os acentos viram U+FFFD.
    const std::string utf8 = toUtf8(htmlText);
    return lxb_html_document_parse(impl_->doc, uc(utf8), utf8.size()) == LXB_STATUS_OK;
}

Node Document::root() const {
    if (!impl_->doc) return {};
    return Node{lxb_dom_interface_node(impl_->doc)};
}

std::vector<Node> Document::select(std::string_view sel) const {
    if (!impl_->doc) return {};
    return runSelector(lxb_dom_interface_node(impl_->doc), sel, false);
}

Node Document::selectFirst(std::string_view sel) const {
    if (!impl_->doc) return {};
    auto v = runSelector(lxb_dom_interface_node(impl_->doc), sel, true);
    return v.empty() ? Node{} : v.front();
}

std::string Document::text() const {
    auto body = selectFirst("body");
    return body ? body.text() : std::string{};
}

} // namespace sigaa::html
