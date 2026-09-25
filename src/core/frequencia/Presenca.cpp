#include "core/frequencia/Presenca.h"

#include <algorithm>
#include <map>

#include "core/calendar/Calendario.h"

namespace sigaa::frequencia {
namespace {

// A chave de um dia. ISO, porque é o que ordena e o que o banco guarda.
std::string chave(const DateTime& d) { return d.toIso(); }

bool mesmoLancamento(SituacaoDia a, int fa, SituacaoDia b, int fb) {
    if (a != b) return false;
    // O número de faltas importa: o professor pode lançar 1 onde o aluno
    // registrou 2, e isso é uma discordância que ele precisa ver.
    if (a == SituacaoDia::Falta) return fa == fb;
    return true;
}

} // namespace

std::string_view toString(TipoMudanca t) {
    switch (t) {
        case TipoMudanca::AlunoMarcou:    return "aluno-marcou";
        case TipoMudanca::AlunoDesfez:    return "aluno-desfez";
        case TipoMudanca::SigaaRegistrou: return "sigaa-registrou";
        case TipoMudanca::SigaaAtropelou: return "sigaa-atropelou";
    }
    return "?";
}

std::string descrever(SituacaoDia s, int faltas) {
    switch (s) {
        case SituacaoDia::Presente:      return "presente";
        case SituacaoDia::Falta:         return std::to_string(faltas) + " falta(s)";
        case SituacaoDia::NaoRegistrada: return "nao registrada";
    }
    return "?";
}

std::vector<DiaEfetivo> efetivos(const Frequencia& f,
                                 const std::vector<Marcacao>& marcacoes) {
    std::map<std::string, const Marcacao*> porDia;
    for (const auto& m : marcacoes) {
        if (!m.ativo || !m.data.valid()) continue;
        porDia[chave(m.data)] = &m;
    }

    std::vector<DiaEfetivo> out;
    out.reserve(f.dias.size() + marcacoes.size());

    for (const auto& d : f.dias) {
        DiaEfetivo e;
        e.data = d.data;
        e.situacao = d.situacao;
        e.faltas = d.faltas;
        e.situacaoSigaa = d.situacao;
        e.estado = d.situacao == SituacaoDia::NaoRegistrada ? EstadoDia::NaoRegistrada
                                                            : EstadoDia::DoSigaa;

        if (const auto it = porDia.find(chave(d.data)); it != porDia.end()) {
            const Marcacao& m = *it->second;
            e.nota = m.nota;

            if (d.situacao == SituacaoDia::NaoRegistrada) {
                // O caso que este módulo existe para atender: o professor não
                // registrou, e vale o que o aluno guardou.
                e.situacao = m.situacao;
                e.faltas = m.faltas;
                e.estado = EstadoDia::MarcadaPeloAluno;
            } else if (!mesmoLancamento(d.situacao, d.faltas, m.situacao, m.faltas)) {
                // O professor registrou diferente. A dele vale — mas o dia
                // fica marcado para o aluno ver, porque é exatamente aqui que
                // a marcação dele pode ser necessária.
                e.estado = EstadoDia::Conflitada;
            }
        }
        out.push_back(std::move(e));
    }

    // Dias que o aluno marcou e que NÃO aparecem no mapa do SIGAA — houve aula
    // e o professor nunca lançou linha nenhuma para ela. Some seria perder o
    // registro justamente do caso mais desamparado.
    for (const auto& m : marcacoes) {
        if (!m.ativo || !m.data.valid()) continue;
        const bool noMapa = std::any_of(
            f.dias.begin(), f.dias.end(),
            [&m](const DiaFrequencia& d) { return chave(d.data) == chave(m.data); });
        if (noMapa) continue;

        DiaEfetivo e;
        e.data = m.data;
        e.situacao = m.situacao;
        e.faltas = m.faltas;
        e.estado = EstadoDia::MarcadaPeloAluno;
        e.nota = m.nota;
        e.situacaoSigaa = SituacaoDia::NaoRegistrada;
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const DiaEfetivo& a, const DiaEfetivo& b) {
        return a.data < b.data;
    });
    return out;
}

int aulasDoEncontro(const Turma& turma, const Frequencia& f, const DateTime& data) {
    if (const int daGrade = calendario::aulasNoDia(turma.horario, data); daGrade > 0) {
        return daGrade;
    }

    // Sem grade para o dia: a média do que o SIGAA já lançou. Só os dias que
    // ele REGISTROU entram na conta — "não registrada" não consumiu aula
    // nenhuma, e incluí-la dividiria o total por dias que não contam,
    // encolhendo o encontro.
    int diasComRegistro = 0;
    for (const auto& d : f.dias) {
        if (d.situacao != SituacaoDia::NaoRegistrada) ++diasComRegistro;
    }
    if (diasComRegistro > 0 && f.aulasComRegistro > 0) {
        // Arredonda para o mais perto: 20/5 = 4 exato, e uma turma com um
        // encontro menor no meio (19/5 = 3,8) devolve 4 em vez de 3.
        return (f.aulasComRegistro + diasComRegistro / 2) / diasComRegistro;
    }

    return 2;
}

Reconciliacao reconciliar(const Frequencia& f, std::vector<Marcacao> marcacoes) {
    Reconciliacao r;

    // Mapa sem lançamento nenhum não é resposta, é ausência de coleta. Tratar
    // como "o professor apagou tudo" aposentaria todas as marcações de uma vez.
    if (!f.temDados) {
        r.marcacoes = std::move(marcacoes);
        return r;
    }

    std::map<std::string, const DiaFrequencia*> agora;
    for (const auto& d : f.dias) agora[chave(d.data)] = &d;

    for (auto& m : marcacoes) {
        if (!m.ativo || !m.data.valid()) continue;

        const auto it = agora.find(chave(m.data));
        if (it == agora.end()) continue;          // o SIGAA não fala deste dia
        const DiaFrequencia& d = *it->second;

        if (d.situacao == SituacaoDia::NaoRegistrada) continue;   // segue não lançado

        // O professor lançou. Concordando, a marcação cumpriu o papel e se
        // aposenta em silêncio; discordando, é alarme.
        if (!mesmoLancamento(d.situacao, d.faltas, m.situacao, m.faltas)) {
            r.conflitos.push_back({m.idTurma, f.turmaNome, m.data, m.situacao,
                                   m.faltas, d.situacao, d.faltas});
        }
        m.ativo = false;
        m.situacaoSigaaNaEpoca = d.situacao;
        r.mudou = true;
    }

    r.marcacoes = std::move(marcacoes);
    return r;
}

} // namespace sigaa::frequencia
