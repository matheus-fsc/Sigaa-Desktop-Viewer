#include "core/avaliacao/Ajustes.h"

#include <algorithm>
#include <map>

#include "core/calendar/Calendario.h"

namespace sigaa::avaliacao {
namespace {

std::string chave(const std::string& idTurma, const std::string& descricao) {
    // A mesma chave do DiffEngine e do banco. Ver a nota no cabeçalho sobre
    // por que ter três chaves para a mesma prova seria um desastre silencioso.
    return idTurma + "|" + descricao;
}

bool mesmaData(const DateTime& a, const DateTime& b) {
    return a.toIso() == b.toIso();
}

} // namespace

std::string_view toString(TipoMudanca t) {
    switch (t) {
        case TipoMudanca::SigaaMarcou:     return "sigaa-marcou";
        case TipoMudanca::SigaaRemarcou:   return "sigaa-remarcou";
        case TipoMudanca::AlunoCorrigiu:   return "aluno-corrigiu";
        case TipoMudanca::AlunoConfirmou:  return "aluno-confirmou";
        case TipoMudanca::AlunoCriou:      return "aluno-criou";
        case TipoMudanca::AlunoDesfez:     return "aluno-desfez";
        case TipoMudanca::SigaaAtropelou:  return "sigaa-atropelou";
    }
    return "?";
}

std::vector<Efetiva> efetivas(const std::vector<Avaliacao>& doSigaa,
                              const std::vector<Ajuste>& ajustes) {
    // A mescla painel/tópico continua sendo a de sempre: aplicar ajuste sobre
    // uma lista não mesclada faria a mesma prova aparecer duas vezes, uma
    // corrigida e outra não.
    const std::vector<Avaliacao> base = calendario::mesclarAvaliacoes(doSigaa);

    std::map<std::string, const Ajuste*> porChave;
    for (const auto& aj : ajustes) {
        if (!aj.ativo) continue;   // aposentado: é histórico, não é dado vivo
        porChave[chave(aj.idTurma, aj.descricao)] = &aj;
    }

    std::vector<Efetiva> out;
    out.reserve(base.size() + ajustes.size());

    for (const auto& av : base) {
        Efetiva e;
        e.av = av;
        e.quandoSigaa = av.quando;
        e.estado = av.fonte == FonteAvaliacao::TopicoAula ? Estado::Inferida
                                                          : Estado::DoSigaa;

        const auto it = porChave.find(chave(av.idTurma, av.descricao));
        if (it != porChave.end()) {
            const Ajuste& aj = *it->second;
            e.nota = aj.nota;

            if (aj.quando.valid() && !mesmaData(aj.quando, av.quando)) {
                // O aluno corrigiu e o SIGAA ainda não mexeu: a correção vale.
                e.av.quando = aj.quando;
                if (!aj.horarioBruto.empty()) e.av.horarioBruto = aj.horarioBruto;
                e.estado = Estado::Editada;
            } else if (aj.confirmada) {
                // Confirmar não muda data nenhuma — muda o quanto se confia
                // nela, e é isso que tira a prova da lista de "confirme com o
                // professor" sem apagá-la do app.
                e.estado = Estado::Confirmada;
            }
        }
        out.push_back(std::move(e));
    }

    // As criadas pelo aluno: não têm contrapartida no SIGAA, então entram
    // inteiras em vez de corrigir alguém.
    for (const auto& aj : ajustes) {
        if (!aj.ativo || !aj.criadaPeloAluno || !aj.quando.valid()) continue;

        Efetiva e;
        e.av.idTurma = aj.idTurma;
        e.av.turmaNome = aj.turmaNome;
        e.av.descricao = aj.descricao;
        e.av.quando = aj.quando;
        e.av.horarioBruto = aj.horarioBruto;
        e.av.fonte = FonteAvaliacao::PainelAvaliacoes;
        e.estado = Estado::Criada;
        e.nota = aj.nota;
        // `quandoSigaa` fica inválida de propósito: o SIGAA não diz nada sobre
        // esta prova, e fingir uma data aqui daria um conflito inventado.
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const Efetiva& a, const Efetiva& b) {
        return a.av.quando < b.av.quando;
    });
    return out;
}

Reconciliacao reconciliar(const std::vector<Avaliacao>& doSigaa,
                          std::vector<Ajuste> ajustes) {
    Reconciliacao r;

    // A lista mesclada precisa de nome: os ponteiros do mapa apontam para
    // dentro dela, e um temporário morreria antes do laço seguinte.
    const std::vector<Avaliacao> mescladas = calendario::mesclarAvaliacoes(doSigaa);
    std::map<std::string, const Avaliacao*> agora;
    for (const auto& av : mescladas) agora[chave(av.idTurma, av.descricao)] = &av;

    for (auto& aj : ajustes) {
        if (!aj.ativo || aj.criadaPeloAluno) continue;

        const auto it = agora.find(chave(aj.idTurma, aj.descricao));
        if (it == agora.end()) continue;   // o SIGAA não falou desta prova agora

        const DateTime& sigaaAgora = it->second->quando;
        if (!sigaaAgora.valid()) continue;

        // A pergunta certa não é "a data do aluno difere da do SIGAA?" — ela
        // difere desde o instante da correção, por definição. É "o SIGAA mudou
        // DESDE que o aluno corrigiu?".
        if (mesmaData(sigaaAgora, aj.quandoSigaaNaEpoca)) continue;

        // O professor mexeu. Se ele chegou na mesma data que o aluno já tinha
        // posto, não há conflito nenhum: os dois concordam, e a correção
        // simplesmente deixou de ser necessária.
        if (mesmaData(sigaaAgora, aj.quando)) {
            aj.ativo = false;
            aj.quandoSigaaNaEpoca = sigaaAgora;
            r.mudou = true;
            continue;
        }

        r.conflitos.push_back({aj.idTurma, it->second->turmaNome, aj.descricao,
                               aj.quando, aj.quandoSigaaNaEpoca, sigaaAgora});
        aj.ativo = false;
        aj.quandoSigaaNaEpoca = sigaaAgora;
        r.mudou = true;
    }

    r.ajustes = std::move(ajustes);
    return r;
}

} // namespace sigaa::avaliacao
