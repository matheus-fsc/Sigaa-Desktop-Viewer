#include "core/estudo/Situacao.h"

#include <algorithm>

namespace sigaa::estudo {

SituacaoFaltas situacaoFaltas(const Frequencia* f) {
    SituacaoFaltas s;
    if (!f || !f->temDados) return s;

    s.faltas = f->faltas();
    s.limite = f->limiteFaltas();
    s.restam = s.limite - s.faltas;
    s.aulasRestantes = std::max(0, f->aulasPelaCH - f->aulasComRegistro);
    for (const auto& d : f->dias) {
        if (d.situacao == SituacaoDia::NaoRegistrada) ++s.diasNaoRegistrados;
    }

    // Um quarto do limite, no mínimo 2: em turma de 2 aulas por encontro, um
    // único dia faltado gasta 2, e "restam 2" já é a última vez.
    const int margem = std::max(2, s.limite / 4);
    if (f->reprovado()) {
        s.risco = RiscoFalta::Reprovado;
    } else if (s.restam <= 0) {
        s.risco = RiscoFalta::NoLimite;
    } else if (s.restam <= margem) {
        s.risco = RiscoFalta::Atencao;
    } else {
        s.risco = RiscoFalta::Folgado;
    }
    return s;
}

SituacaoNotas situacaoNotas(const Notas* n, double mediaMinima) {
    SituacaoNotas s;
    if (!n) return s;

    s.unidades = static_cast<int>(n->unidades.size());
    double soma = 0;
    bool algumaNaoZero = false;
    for (const auto& u : n->unidades) {
        for (const auto& a : u.avaliacoes) {
            if (!a.nota) continue;
            if (*a.nota == 0) {
                s.zeros.push_back(a.abrev + " (Unid. " + std::to_string(u.numero) + ")");
            } else {
                algumaNaoZero = true;
            }
        }
        if (u.nota) {
            ++s.lancadas;
            soma += *u.nota;
            if (*u.nota > 0) algumaNaoZero = true;
            // Unidade sem avaliação cadastrada e com 0,0: o zero é a própria
            // unidade.
            if (*u.nota == 0 && u.avaliacoes.empty()) {
                s.zeros.push_back("Unid. " + std::to_string(u.numero));
            }
        }
    }
    s.soZeros = !s.zeros.empty() && !algumaNaoZero;

    if (s.lancadas == 0) return s;
    s.mediaParcial = soma / s.lancadas;

    if (s.lancadas == s.unidades) {
        const double final = n->resultado.value_or(*s.mediaParcial);
        s.risco = final >= mediaMinima ? RiscoNota::Aprovado : RiscoNota::Abaixo;
        return s;
    }

    const double precisa = (mediaMinima * s.unidades - soma) / (s.unidades - s.lancadas);
    s.precisa = std::max(0.0, precisa);
    if (precisa <= mediaMinima) {
        s.risco = RiscoNota::Folgado;
    } else if (precisa <= 8) {
        s.risco = RiscoNota::Atencao;
    } else if (precisa <= 10) {
        s.risco = RiscoNota::Dificil;
    } else {
        s.risco = RiscoNota::SoReposicao;
    }
    return s;
}

const char* nomeRisco(RiscoFalta r) {
    switch (r) {
        case RiscoFalta::SemDados:  return "sem_dados";
        case RiscoFalta::Folgado:   return "folgado";
        case RiscoFalta::Atencao:   return "atencao";
        case RiscoFalta::NoLimite:  return "no_limite";
        case RiscoFalta::Reprovado: return "reprovado_por_falta";
    }
    return "sem_dados";
}

const char* nomeRisco(RiscoNota r) {
    switch (r) {
        case RiscoNota::SemNota:     return "sem_nota";
        case RiscoNota::Folgado:     return "folgado";
        case RiscoNota::Atencao:     return "atencao";
        case RiscoNota::Dificil:     return "dificil";
        case RiscoNota::SoReposicao: return "so_com_reposicao";
        case RiscoNota::Aprovado:    return "aprovado";
        case RiscoNota::Abaixo:      return "abaixo_da_media";
    }
    return "sem_nota";
}

} // namespace sigaa::estudo
