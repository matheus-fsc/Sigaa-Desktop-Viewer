#include "mcp/Ferramentas.h"

namespace sigaa::mcp {

const std::vector<Ferramenta>& todasAsFerramentas() {
    static const std::vector<Ferramenta> todas = [] {
        std::vector<Ferramenta> v = ferramentasDeLeitura();
        return v;
    }();
    return todas;
}

} // namespace sigaa::mcp
