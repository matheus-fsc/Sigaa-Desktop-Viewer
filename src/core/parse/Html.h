#pragma once
// Wrapper fino sobre lexbor. Existe para que nenhum outro arquivo do projeto
// precise incluir cabeçalhos de lexbor — se um dia trocarmos de parser, só
// este arquivo muda.
//
// Por que lexbor e não libxml2/regex: o HTML do SIGAA é JSF mal-formado
// (tags não fechadas, atributos duplicados). Precisamos de um parser HTML5
// tolerante, e de seletores CSS para expressar coisas como
// "div.rich-stglpanel > ul.menu-direita > li".

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sigaa::html {

class Document;

// Handle não-dono para um nó do documento. Válido enquanto o Document viver.
class Node {
public:
    Node() = default;
    explicit Node(void* raw) : raw_(raw) {}

    bool valid() const { return raw_ != nullptr; }
    explicit operator bool() const { return valid(); }

    // Texto concatenado do nó e descendentes, com espaços colapsados.
    std::string text() const;
    // Texto cru, sem colapsar espaços.
    std::string rawText() const;

    // Como text(), mas SEM o conteúdo de <script> e <style>.
    //
    // textContent do DOM inclui o corpo do <script> — é o comportamento
    // correto do padrão, e é exatamente o errado para nós. Cada tópico de aula
    // do SIGAA carrega um <script> de drag-and-drop do RichFaces embutido, e
    // `text()` devolve `var elt = $("formAva:...")` grudado na descrição que o
    // professor escreveu. Enquanto ninguém exibia esse campo, dava para não
    // notar; ao pintá-lo na tela, vira lixo visível.
    std::string textoVisivel() const;
    std::string attr(std::string_view name) const;
    bool hasAttr(std::string_view name) const;
    std::string tagName() const;
    std::string innerHtml() const;

    // Busca dentro deste nó.
    std::vector<Node> select(std::string_view cssSelector) const;
    Node selectFirst(std::string_view cssSelector) const;

    void* raw() const { return raw_; }

private:
    void* raw_{nullptr};
};

class Document {
public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Retorna false se o parse falhar de forma irrecuperável (raro — lexbor
    // é tolerante por design).
    bool parse(std::string_view utf8Html);

    std::vector<Node> select(std::string_view cssSelector) const;
    Node selectFirst(std::string_view cssSelector) const;
    Node root() const;

    // Texto de toda a página, para heurísticas do PageClassifier.
    std::string text() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Utilitários de texto usados pelos parsers.
std::string collapseWhitespace(std::string_view s);
std::string trim(std::string_view s);

// --- encoding -------------------------------------------------------------
// O SIGAA serve **windows-1252**, não UTF-8 (<meta charset=windows-1252>).
// O lexbor assume UTF-8, e bytes cp1252 como 0xC7 ("Ç") são UTF-8 inválido —
// viram U+FFFD e o acento se perde de forma irrecuperável. Por isso todo HTML
// passa por aqui antes do parse.

bool isValidUtf8(std::string_view s);

// Devolve UTF-8. Se o conteúdo já for UTF-8 válido, retorna cópia inalterada;
// caso contrário, transcodifica de windows-1252.
std::string toUtf8(std::string_view html);

} // namespace sigaa::html
