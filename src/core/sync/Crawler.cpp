#include "core/sync/Crawler.h"

#include "core/calendar/Calendario.h"
#include "core/jsf/JsfForm.h"
#include "core/parse/ArquivoParser.h"
#include "core/parse/FrequenciaParser.h"
#include "core/parse/Html.h"
#include "core/parse/ParticipanteParser.h"
#include "core/parse/PortalParser.h"
#include "core/parse/TurmaParser.h"
#include "core/sync/Materiais.h"

namespace sigaa::sync {
namespace {

void avisar(const OpcoesColeta& op, const std::string& msg) {
    if (op.progresso) op.progresso(msg);
}

// Localiza o comando que entra na turma, pelo frontEndIdTurma — que é estável
// entre sessões (RECON §1.2), ao contrário do texto do link, que pode variar
// em acentuação e abreviação.
const jsf::Command* comandoDaTurma(const std::vector<jsf::Command>& cmds,
                                   const std::string& frontEndId) {
    for (const auto& c : cmds) {
        for (const auto& [k, v] : c.params) {
            if (k == "frontEndIdTurma" && v == frontEndId) return &c;
        }
    }
    return nullptr;
}

// Abre a aba Arquivos da turma corrente e junta o material ao snapshot.
//
// Falhar aqui NÃO conta como turma com falha: várias turmas simplesmente não
// têm a aba, e inflar `turmasComFalha` faria o serviço classificar uma coleta
// perfeitamente boa como suspeita — que é o estado em que ele se recusa a
// gravar. O preço de errar para este lado é não ver um arquivo novo; para o
// outro lado, é o app parar de guardar qualquer coisa.
void coletarArquivos(http::SigaaSession& sessao, const html::Document& docTurma,
                     const Turma& t, ResultadoColeta& res, const OpcoesColeta& op) {
    html::Document doc;
    if (!abrirAbaPorRotulo(sessao, docTurma, "Arquivos", &doc, nullptr)) return;

    const auto lista = parse::parseArquivos(doc, t.idTurma, t.nome);
    if (!lista.pareceAbaArquivos) return;

    res.snapshot.arquivos.insert(res.snapshot.arquivos.end(), lista.arquivos.begin(),
                                 lista.arquivos.end());
    if (!lista.arquivos.empty()) {
        avisar(op, "  " + std::to_string(lista.arquivos.size()) + " arquivo(s)");
    }
}

// Abre a aba Participantes da turma corrente e junta a lista ao snapshot.
//
// Mesma regra de `coletarArquivos`: falhar aqui NÃO conta como turma com
// falha. A aba pode nem estar no menu, e inflar `turmasComFalha` faria o
// serviço classificar uma coleta boa como suspeita — o estado em que ele se
// recusa a gravar, e aí o aluno perde também os prazos.
void coletarParticipantes(http::SigaaSession& sessao, const html::Document& docTurma,
                          const Turma& t, ResultadoColeta& res, const OpcoesColeta& op) {
    html::Document doc;
    if (!abrirAbaPorRotulo(sessao, docTurma, "Participantes", &doc, nullptr)) return;

    const auto lista = parse::parseParticipantes(doc, t.idTurma, t.nome);
    // Turma sem ninguém não existe — sempre há ao menos o professor. Aceitar a
    // lista vazia aqui gravaria "coletei e não tem gente", que é falso, e o
    // upsert acumulativo do banco esconderia o erro até alguém abrir a aba.
    if (!lista.pareceAbaParticipantes || lista.participantes.empty()) return;

    res.snapshot.participantes.insert(res.snapshot.participantes.end(),
                                      lista.participantes.begin(),
                                      lista.participantes.end());
    avisar(op, "  " + std::to_string(lista.participantes.size()) + " participante(s)");
}

// Mapa de frequência da turma. Mesma regra das duas acima: falhar aqui NÃO
// conta como turma com falha — nem toda turma tem o item no menu, e inflar
// `turmasComFalha` faria o serviço classificar uma coleta boa como suspeita.
//
// Ao contrário dos participantes, a frequência VAZIA é guardada: "o professor
// ainda não lançou nada" é informação que o aluno precisa ver, e é diferente
// de "não fui olhar". Quem distingue é `temDados`.
void coletarFrequencia(http::SigaaSession& sessao, const html::Document& docTurma,
                       const Turma& t, ResultadoColeta& res, const OpcoesColeta& op) {
    html::Document doc;
    // "Frequ" e não "Frequência": o rótulo vem como "Frequ&#234;ncia", e o
    // prefixo ASCII é único no menu da turma.
    if (!abrirAbaPorRotulo(sessao, docTurma, "Frequ", &doc, nullptr)) return;

    const auto r = parse::parseFrequencia(doc, t.idTurma, t.nome);
    if (!r.pareceMapaDeFrequencia) return;

    res.snapshot.frequencias.push_back(r.frequencia);
    if (r.frequencia.temDados) {
        avisar(op, "  frequencia: " + std::to_string(r.frequencia.faltas()) +
                       " falta(s) de " + std::to_string(r.frequencia.limiteFaltas()));
    } else {
        avisar(op, "  frequencia: o professor ainda nao lancou nada");
    }
}

} // namespace

ResultadoColeta coletar(http::SigaaSession& sessao, const OpcoesColeta& op) {
    ResultadoColeta res;

    avisar(op, "buscando o portal");
    auto r = sessao.irParaPortal();
    if (!r.ok()) {
        res.erro = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        return res;
    }
    if (http::SigaaSession::classify(r.body) != http::PageKind::Portal) {
        res.erro = "esperava o portal, veio " +
                   std::string(http::toString(http::SigaaSession::classify(r.body)));
        return res;
    }

    html::Document portal;
    if (!portal.parse(r.body)) {
        res.erro = "parse do portal falhou";
        return res;
    }

    res.snapshot = parse::parsePortal(portal);
    res.snapshot.minutosSessaoRestantes =
        http::SigaaSession::minutosSessaoRestantes(r.body);
    res.htmlPortal = r.body;
    res.ok = true;

    if (!op.incluirTurmas) return res;

    // --- visita cada turma ---------------------------------------------------
    // Copia a lista antes: `res.snapshot.turmas` cresce/reordena durante o loop
    // em versões futuras, e iterar sobre o que estamos mutando é pedir bug.
    const auto turmas = res.snapshot.turmas;

    for (const auto& t : turmas) {
        if (t.frontEndId.empty()) continue;
        // O filtro é por id e não por nome: nome de turma repete entre períodos
        // ("ADMINISTRAÇÃO" de 2025.2 e de 2026.2) e o aluno escolheu uma.
        if (!op.apenasTurmas.empty() && !op.apenasTurmas.count(t.idTurma)) continue;
        avisar(op, "turma: " + t.nome);

        // O ViewState do portal envelhece a cada navegação, então relemos os
        // comandos da página corrente em vez de guardá-los de antes.
        const auto cmds = jsf::findCommands(portal);
        const auto* cmd = comandoDaTurma(cmds, t.frontEndId);
        if (!cmd) {
            ++res.turmasComFalha;
            continue;
        }
        const auto form = jsf::parseForm(portal, cmd->formId);
        if (!form) {
            ++res.turmasComFalha;
            continue;
        }

        auto rt = sessao.postForm(form->action, form->buildPostBody(cmd->params));
        if (!rt.ok()) {
            ++res.turmasComFalha;
        } else {
            if (res.htmlPrimeiraTurma.empty()) res.htmlPrimeiraTurma = rt.body;
            html::Document dt;
            if (dt.parse(rt.body)) {
                const auto c =
                    parse::parseTurmaVirtual(dt, t.idTurma, t.nome, t.periodo);
                res.snapshot.topicos.insert(res.snapshot.topicos.end(),
                                            c.topicos.begin(), c.topicos.end());
                res.snapshot.avaliacoes.insert(res.snapshot.avaliacoes.end(),
                                               c.avaliacoes.begin(),
                                               c.avaliacoes.end());
                ++res.turmasVisitadas;

                if (op.incluirArquivos) coletarArquivos(sessao, dt, t, res, op);
                if (op.incluirFrequencia) coletarFrequencia(sessao, dt, t, res, op);
                if (op.incluirParticipantes) {
                    coletarParticipantes(sessao, dt, t, res, op);
                }
            } else {
                ++res.turmasComFalha;
            }
        }

        // Volta ao portal — barato e devolve ViewState novo (RECON §1.1).
        auto rv = sessao.irParaPortal();
        if (!rv.ok() || !portal.parse(rv.body)) {
            res.erro = "perdi o portal ao voltar da turma " + t.nome;
            res.snapshot.avaliacoes =
                calendario::mesclarAvaliacoes(std::move(res.snapshot.avaliacoes));
            return res;   // ok continua true: o que já foi coletado é válido
        }
    }

    // O snapshot sai CANÔNICO: as duas fontes de avaliação já mescladas.
    //
    // Sem isto, a mesma prova entra duas vezes — do painel (com hora) e do
    // tópico (sem hora) — colidindo na chave (id_turma, descricao) do banco.
    // Uma sobrescreve a outra em ordem não determinística, e todo ciclo acusa
    // "prova remarcada" comparando 15:45 com sem-hora. Alarme falso recorrente
    // é o jeito mais rápido de ensinar o usuário a ignorar as notificações.
    res.snapshot.avaliacoes =
        calendario::mesclarAvaliacoes(std::move(res.snapshot.avaliacoes));

    return res;
}

} // namespace sigaa::sync
