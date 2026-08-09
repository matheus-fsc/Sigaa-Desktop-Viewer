#include "core/jsf/JsfForm.h"

#include <algorithm>
#include <cctype>

namespace sigaa::jsf {
namespace {

// Substitui/insere um par, preservando a ordem original quando já existe.
void upsert(Params& p, const std::string& k, const std::string& v) {
    auto it = std::find_if(p.begin(), p.end(),
                           [&](const auto& kv) { return kv.first == k; });
    if (it != p.end()) it->second = v;
    else p.emplace_back(k, v);
}

// Extrai o conteúdo entre o primeiro '{' e seu '}' correspondente, a partir
// de `from`. Retorna string vazia se não houver par balanceado.
std::string extractBraceBlock(std::string_view s, size_t from) {
    size_t open = s.find('{', from);
    if (open == std::string_view::npos) return {};
    int depth = 0;
    bool inStr = false;
    char quote = 0;
    for (size_t i = open; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == quote) inStr = false;
            continue;
        }
        if (c == '\'' || c == '"') { inStr = true; quote = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return std::string(s.substr(open + 1, i - open - 1));
        }
    }
    return {};
}

// Divide "'a':'b','c':'d'" em pares, respeitando aspas.
Params splitObjectLiteral(std::string_view body) {
    Params out;
    std::string key, val;
    std::string* cur = &key;
    bool inStr = false;
    char quote = 0;

    auto flush = [&] {
        if (!key.empty()) out.emplace_back(key, val);
        key.clear();
        val.clear();
        cur = &key;
    };

    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (inStr) {
            if (c == '\\' && i + 1 < body.size()) { cur->push_back(body[++i]); continue; }
            if (c == quote) { inStr = false; continue; }
            cur->push_back(c);
            continue;
        }
        switch (c) {
            case '\'':
            case '"': inStr = true; quote = c; break;
            case ':': cur = &val; break;
            case ',': flush(); break;
            default:
                if (!std::isspace(static_cast<unsigned char>(c))) cur->push_back(c);
                break;
        }
    }
    flush();
    return out;
}

} // namespace

// --------------------------------------------------------------------------

std::string urlEncode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3 / 2);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string Form::buildPostBody(const Params& extra) const {
    Params all = fields;
    for (const auto& [k, v] : extra) upsert(all, k, v);

    // Garante o ViewState mesmo que o form não o tenha listado.
    if (!viewState.empty()) upsert(all, "javax.faces.ViewState", viewState);

    std::string body;
    for (const auto& [k, v] : all) {
        if (!body.empty()) body.push_back('&');
        body += urlEncode(k);
        body.push_back('=');
        body += urlEncode(v);
    }
    return body;
}

// --------------------------------------------------------------------------

std::optional<Form> parseForm(const html::Node& formNode) {
    if (!formNode) return std::nullopt;

    Form f;
    f.id = formNode.attr("id");
    f.action = formNode.attr("action");

    // O SIGAA às vezes emite action absoluto; normaliza para caminho.
    if (auto pos = f.action.find("://"); pos != std::string::npos) {
        auto slash = f.action.find('/', pos + 3);
        f.action = (slash == std::string::npos) ? "/" : f.action.substr(slash);
    }

    for (const auto& el : formNode.select("input, select, textarea")) {
        std::string name = el.attr("name");
        if (name.empty()) continue;

        // Checkbox/radio só contam quando marcados — igual ao browser.
        std::string type = el.attr("type");
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if ((type == "checkbox" || type == "radio") && !el.hasAttr("checked")) continue;
        if (type == "submit" || type == "button" || type == "image") continue;

        std::string value = el.attr("value");
        if (name == "javax.faces.ViewState") f.viewState = value;
        upsert(f.fields, name, value);
    }

    return f.valid() ? std::optional<Form>{std::move(f)} : std::nullopt;
}

std::optional<Form> parseForm(const html::Document& doc, std::string_view formId) {
    // Ids do JSF contêm ':' — inválido em seletor CSS sem escape. Varremos
    // os forms e comparamos o atributo, que é mais simples e mais robusto.
    for (const auto& node : doc.select("form")) {
        if (node.attr("id") == formId) return parseForm(node);
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------

Params parseJsfcljsParams(std::string_view onclick) {
    size_t call = onclick.find("jsfcljs(");
    if (call == std::string_view::npos) return {};
    std::string block = extractBraceBlock(onclick, call);
    if (block.empty()) return {};
    return splitObjectLiteral(block);
}

std::string parseJsfcljsFormId(std::string_view onclick) {
    size_t call = onclick.find("jsfcljs(");
    if (call == std::string_view::npos) return {};
    size_t g = onclick.find("getElementById(", call);
    if (g == std::string_view::npos) return {};
    size_t q1 = onclick.find_first_of("'\"", g);
    if (q1 == std::string_view::npos) return {};
    char quote = onclick[q1];
    size_t q2 = onclick.find(quote, q1 + 1);
    if (q2 == std::string_view::npos) return {};
    return std::string(onclick.substr(q1 + 1, q2 - q1 - 1));
}

std::vector<Command> findCommands(const html::Document& doc) {
    std::vector<Command> out;
    for (const auto& a : doc.select("a[onclick]")) {
        std::string oc = a.attr("onclick");
        if (oc.find("jsfcljs(") == std::string::npos) continue;

        Command c;
        c.formId = parseJsfcljsFormId(oc);
        c.params = parseJsfcljsParams(oc);
        c.label = a.text();
        if (!c.formId.empty() && !c.params.empty()) out.push_back(std::move(c));
    }
    return out;
}

std::optional<Command> findCommandByLabel(const html::Document& doc,
                                          std::string_view substring) {
    for (auto& c : findCommands(doc)) {
        if (c.label.find(substring) != std::string::npos) return c;
    }
    return std::nullopt;
}

} // namespace sigaa::jsf
