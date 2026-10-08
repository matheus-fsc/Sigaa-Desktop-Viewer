#include "mcp/Propostas.h"

#include <algorithm>

#include "core/planejamento/Planejamento.h"
#include "core/util/Texto.h"

namespace sigaa::mcp {

using estudo::EstadoProposta;
using estudo::Proposta;
using estudo::TipoProposta;

namespace {

std::string chaveModo(TipoProposta t) { return std::string("mcp.modo.") + estudo::nomeTipo(t); }

Modo modoPadrao(TipoProposta t) {
    return t == TipoProposta::Horas || t == TipoProposta::Dificuldade ? Modo::Propoe : Modo::Aplica;
}

// Duas propostas sobre a mesma coisa: a nova substitui a pendente.
bool mesmoAlvo(const Proposta& a, const Proposta& b) {
    if (a.tipo != b.tipo) return false;
    switch (a.tipo) {
        case TipoProposta::Horas:       return a.diaSemana == b.diaSemana;
        case TipoProposta::Dificuldade: return a.idTurma == b.idTurma;
        case TipoProposta::Sessao:
            return a.idTurma == b.idTurma && a.dia.toIso() == b.dia.toIso() &&
                   util::dobrar(a.topico) == util::dobrar(b.topico);
        case TipoProposta::Foco:
            return a.idTurma == b.idTurma && util::dobrar(a.topico) == util::dobrar(b.topico);
    }
    return false;
}

// Grava `v` como o valor daquilo que a proposta muda.
bool gravarValor(store::Database& db, const Proposta& q, int v) {
    auto prefs = db.carregarPreferenciasEstudo();
    if (q.tipo == TipoProposta::Horas) {
        if (q.diaSemana < 0 || q.diaSemana > 6) return false;
        prefs.minutosPorDia[static_cast<size_t>(q.diaSemana)] = v;
    } else if (q.tipo == TipoProposta::Dificuldade) {
        const auto d = static_cast<planejamento::Dificuldade>(std::clamp(v, 1, 3));
        // Média é o padrão: só se guarda o que difere dele.
        if (d == planejamento::Dificuldade::Media) prefs.dificuldade.erase(q.idTurma);
        else prefs.dificuldade[q.idTurma] = d;
    } else {
        return true;
    }
    return db.gravarPreferenciasEstudo(prefs);
}

} // namespace

Modo modo(store::Database& db, TipoProposta t) {
    const auto v = db.lerMeta(chaveModo(t));
    if (!v) return modoPadrao(t);
    if (*v == "nao") return Modo::NaoPode;
    if (*v == "aplica") return Modo::Aplica;
    return Modo::Propoe;
}

bool gravarModo(store::Database& db, TipoProposta t, Modo m) {
    return db.gravarMeta(chaveModo(t), m == Modo::NaoPode ? "nao" : m == Modo::Aplica ? "aplica" : "propoe");
}

int tetoHoras(store::Database& db) {
    const auto v = db.lerMeta("mcp.teto");
    if (!v) return 8;
    try {
        const int h = std::stoi(*v);
        return h >= 1 && h <= 16 ? h : 8;
    } catch (...) {
        return 8;
    }
}

bool gravarTeto(store::Database& db, int horas) {
    return db.gravarMeta("mcp.teto", std::to_string(std::clamp(horas, 1, 16)));
}

int valorAtual(store::Database& db, const Proposta& q) {
    if (q.tipo == TipoProposta::Horas) {
        if (q.diaSemana < 0 || q.diaSemana > 6) return 0;
        return db.carregarPreferenciasEstudo().minutosPorDia[static_cast<size_t>(q.diaSemana)];
    }
    if (q.tipo == TipoProposta::Dificuldade) {
        return static_cast<int>(db.carregarPreferenciasEstudo().dificuldadeDe(q.idTurma));
    }
    return 0;
}

std::int64_t propor(store::Database& db, Proposta q, std::int64_t agora, std::string* erro,
                    bool* aplicada) {
    if (aplicada) *aplicada = false;
    auto falhar = [&](std::string m) -> std::int64_t {
        if (erro) *erro = std::move(m);
        return 0;
    };
    const Modo m = modo(db, q.tipo);
    if (m == Modo::NaoPode) {
        return falhar(std::string("o aluno não permite que agentes mudem ") +
                      (q.tipo == TipoProposta::Horas         ? "as horas de estudo"
                       : q.tipo == TipoProposta::Dificuldade ? "a dificuldade das matérias"
                       : q.tipo == TipoProposta::Sessao      ? "o plano com sessões extras"
                                                             : "os pontos de foco") +
                      " (Estudo > Agentes de IA > Permissões)");
    }
    if (q.tipo == TipoProposta::Horas && q.para > tetoHoras(db) * 60) {
        return falhar("passa do teto de " + std::to_string(tetoHoras(db)) +
                      " h por dia que o aluno definiu");
    }
    q.de = valorAtual(db, q);
    if ((q.tipo == TipoProposta::Horas || q.tipo == TipoProposta::Dificuldade) && q.de == q.para) {
        return falhar("o valor proposto já é o atual");
    }
    for (const auto& p : db.carregarPropostas()) {
        if (p.estado == EstadoProposta::Pendente && mesmoAlvo(p, q)) {
            db.apagarDoAgente(store::Database::TabelaAgente::Proposta, p.id);
        }
    }
    q.criadoEm = agora;
    q.estado = EstadoProposta::Pendente;
    q.respondidaEm = 0;
    q.id = db.inserirProposta(q);
    if (!q.id) return falhar("o banco recusou: " + db.erro());
    if (m == Modo::Aplica) {
        std::string e;
        if (!aceitar(db, q, q.para, EstadoProposta::Aplicada, agora, &e)) return falhar(e);
        if (aplicada) *aplicada = true;
    }
    return q.id;
}

bool aceitar(store::Database& db, Proposta& q, int valor, EstadoProposta como, std::int64_t agora,
             std::string* erro) {
    // O `de` de agora, e não o de quando o agente propôs: o aluno pode ter
    // mexido no meio, e desfazer tem de voltar ao que estava antes do aceite.
    q.de = valorAtual(db, q);
    if (q.tipo == TipoProposta::Foco) {
        estudo::PontoFoco f;
        f.origem = q.origem;
        f.criadoEm = f.atualizadoEm = agora;
        f.idTurma = q.idTurma;
        f.topico = q.topico;
        f.nivel = std::clamp(valor, 1, 3);
        f.motivo = q.motivo;
        q.ref = db.marcarFoco(f);
        if (!q.ref) {
            if (erro) *erro = "o banco recusou o ponto de foco: " + db.erro();
            return false;
        }
    } else if (!gravarValor(db, q, valor)) {
        if (erro) *erro = "não consegui gravar a mudança: " + db.erro();
        return false;
    }
    q.para = valor;
    q.estado = como;
    q.respondidaEm = agora;
    return db.responderProposta(q);
}

bool desfazer(store::Database& db, Proposta& q, std::int64_t agora) {
    if (!q.valendo()) return false;
    if (q.tipo == TipoProposta::Foco) {
        if (q.ref) db.apagarDoAgente(store::Database::TabelaAgente::Foco, q.ref);
        q.ref = 0;
    } else if (!gravarValor(db, q, q.de)) {
        return false;
    }
    q.estado = EstadoProposta::Desfeita;
    q.respondidaEm = agora;
    return db.responderProposta(q);
}

bool recusar(store::Database& db, Proposta& q, std::int64_t agora) {
    if (q.estado != EstadoProposta::Pendente) return false;
    q.estado = EstadoProposta::Recusada;
    q.respondidaEm = agora;
    return db.responderProposta(q);
}

bool reabrir(store::Database& db, Proposta& q) {
    if (q.estado != EstadoProposta::Recusada && q.estado != EstadoProposta::Desfeita) return false;
    q.estado = EstadoProposta::Pendente;
    q.respondidaEm = 0;
    return db.responderProposta(q);
}

int aplicarPendentes(store::Database& db, TipoProposta t, std::int64_t agora) {
    const int teto = tetoHoras(db) * 60;
    int n = 0;
    for (auto q : db.carregarPropostas()) {
        if (q.tipo != t || q.estado != EstadoProposta::Pendente) continue;
        if (t == TipoProposta::Horas && q.para > teto) continue;
        if (aceitar(db, q, q.para, EstadoProposta::Aplicada, agora)) ++n;
    }
    return n;
}

std::optional<Proposta> mudancaValendo(const std::vector<Proposta>& todas, TipoProposta t,
                                       const std::string& idTurma, int diaSemana, int valorAtual) {
    const Proposta* ultima = nullptr;
    for (const auto& q : todas) {
        if (q.tipo != t || !q.valendo()) continue;
        if (t == TipoProposta::Horas ? q.diaSemana != diaSemana : q.idTurma != idTurma) continue;
        if (!ultima || q.respondidaEm > ultima->respondidaEm) ultima = &q;
    }
    if (!ultima || ultima->para != valorAtual) return std::nullopt;
    return *ultima;
}

int pendentesVisiveis(store::Database& db) {
    int n = 0;
    for (const auto& q : db.carregarPropostas()) {
        if (q.estado == EstadoProposta::Pendente && modo(db, q.tipo) != Modo::NaoPode) ++n;
    }
    return n;
}

} // namespace sigaa::mcp
