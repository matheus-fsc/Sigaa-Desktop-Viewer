#include "core/estudo/Registros.h"

namespace sigaa::estudo {

const char* nomeTipo(TipoProposta t) {
    switch (t) {
        case TipoProposta::Horas:       return "horas";
        case TipoProposta::Dificuldade: return "dificuldade";
        case TipoProposta::Sessao:      return "sessao";
        case TipoProposta::Foco:        return "foco";
    }
    return "horas";
}

const char* nomeEstado(EstadoProposta e) {
    switch (e) {
        case EstadoProposta::Pendente: return "pendente";
        case EstadoProposta::Aceita:   return "aceita";
        case EstadoProposta::Aplicada: return "aplicada";
        case EstadoProposta::Recusada: return "recusada";
        case EstadoProposta::Desfeita: return "desfeita";
    }
    return "pendente";
}

TipoProposta tipoPorNome(const std::string& s) {
    for (auto t : {TipoProposta::Horas, TipoProposta::Dificuldade, TipoProposta::Sessao,
                   TipoProposta::Foco}) {
        if (s == nomeTipo(t)) return t;
    }
    return TipoProposta::Horas;
}

EstadoProposta estadoPorNome(const std::string& s) {
    for (auto e : {EstadoProposta::Pendente, EstadoProposta::Aceita, EstadoProposta::Aplicada,
                   EstadoProposta::Recusada, EstadoProposta::Desfeita}) {
        if (s == nomeEstado(e)) return e;
    }
    return EstadoProposta::Pendente;
}

} // namespace sigaa::estudo
