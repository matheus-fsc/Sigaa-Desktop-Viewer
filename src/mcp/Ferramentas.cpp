#include "mcp/Ferramentas.h"

#include "mcp/Escrita.h"

namespace sigaa::mcp {

const std::vector<Ferramenta>& todasAsFerramentas() {
    static const std::vector<Ferramenta> todas = [] {
        std::vector<Ferramenta> v = ferramentasDeLeitura();
        for (const auto& f : ferramentasDeEscrita()) v.push_back(f);
        return v;
    }();
    return todas;
}

const Ferramenta* ferramenta(const std::string& nome) {
    for (const Ferramenta& f : todasAsFerramentas()) {
        if (f.nome == nome) return &f;
    }
    return nullptr;
}

} // namespace sigaa::mcp
