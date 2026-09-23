#include "core/servico/Servico.h"

#include <filesystem>
#include <fstream>
#include <ctime>

#include "core/calendar/Calendario.h"
#include "core/http/SigaaSession.h"
#include "core/report/HtmlReport.h"
#include "core/store/Database.h"
#include "core/sync/Baixador.h"
#include "core/sync/Crawler.h"
#include "core/sync/Materiais.h"

namespace sigaa::servico {
namespace {

// Zera a senha da nossa cópia. Mitigação parcial e assumida: std::string pode
// ter realocado antes, deixando o valor antigo no heap. Mesma limitação de
// app::limpar — o remédio de verdade é o keychain.
void limpar(std::string& s) {
    volatile char* p = const_cast<volatile char*>(s.data());
    for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

void diz(const Log& log, Nivel n, std::string_view msg) {
    if (log) log(n, msg);
}

// Caminho absoluto: o clique na notificação abre o arquivo por fora do nosso
// processo, e o shell não herda o diretório de trabalho.
std::string absoluto(const std::string& caminho) {
    std::error_code ec;
    const auto p = std::filesystem::absolute(caminho, ec);
    return ec ? caminho : p.string();
}

std::string trocarParaIcs(const std::string& relatorio) {
    const auto ponto = relatorio.find_last_of('.');
    const auto barra = relatorio.find_last_of("/\\");
    // Só corta se o ponto for do nome do arquivo, não de um diretório como
    // "./saida" ou "../x/relatorio".
    const bool pontoNoNome =
        ponto != std::string::npos &&
        (barra == std::string::npos || ponto > barra);
    return (pontoNoNome ? relatorio.substr(0, ponto) : relatorio) + ".ics";
}

Resultado falhar(Falha f, std::string erro) {
    Resultado r;
    r.falha = f;
    r.erro = std::move(erro);
    return r;
}

// Baixa o material que ainda não está no disco, turma por turma.
//
// POR QUE AQUI, e não dentro do Crawler: a sessão que o crawler usou já saiu da
// turma quando ele terminou, mas re-entrar custa 2 requisições — e só pagamos
// isso nas turmas que TÊM algo pendente. Em regime, isso é quase sempre zero ou
// uma turma; a checagem do que falta é local e não custa rede nenhuma. Misturar
// download no meio da coleta pouparia essas 2 requisições e faria o crawler
// deixar de ser "ler o SIGAA" para virar "ler e escrever no disco".
//
// SEQUENCIAL, uma sessão só: aqui já estamos autenticados e o `Baixador`
// paralelo custaria um login por canal. Ninguém está olhando a tela durante um
// sync — o que importa é não parecer um ataque, não terminar rápido.
void baixarPendentes(http::SigaaSession& sess, const Snapshot& snap,
                     const std::string& base, Resultado& r, const Log& log) {
    for (const auto& turma : snap.turmas) {
        std::vector<sync::PedidoDownload> faltando;

        const std::string pasta = sync::pastaDaTurma(base, turma.nome);
        sync::CacheLocal cache(pasta);
        for (const auto& a : snap.arquivos) {
            if (a.idTurma != turma.idTurma || a.idArquivo.empty()) continue;
            if (cache.temNoDisco(a.idArquivo)) continue;
            faltando.push_back({a.idArquivo, a.titulo});
        }
        if (faltando.empty()) continue;

        r.materiaisPendentes += static_cast<int>(faltando.size());
        diz(log, Nivel::Passo, "baixando " + std::to_string(faltando.size()) +
                                   " arquivo(s) de " + turma.nome);

        // Falha aqui NÃO derruba o ciclo: o relatório e o banco já valem, e um
        // professor que removeu um material não pode fazer o sync inteiro
        // parecer quebrado.
        sync::SessaoTurma st(sess);
        std::string erro;
        if (!st.entrar(turma, &erro) || !st.abrirArquivos(&erro)) {
            diz(log, Nivel::Aviso, "nao consegui abrir " + turma.nome + ": " + erro);
            continue;
        }
        for (const auto& p : faltando) {
            std::string e;
            if (const auto caminho = st.baixar(p.idArquivo, pasta, &e)) {
                // O manifesto é o que faz a janela da turma mostrar "✓ offline"
                // sem ir à rede, e o que impede o próximo ciclo de baixar tudo
                // outra vez. Gravar a cada arquivo, e não no fim: um sync
                // interrompido no meio não pode perder o que já veio.
                cache.registrar(p.idArquivo, *caminho);
                ++r.materiaisBaixados;
            } else {
                diz(log, Nivel::Aviso, "falhou: " + p.nome + " (" + e + ")");
            }
        }
    }
}

// Confronta as correções do aluno com o que o SIGAA acabou de trazer.
//
// DEPOIS de gravar o snapshot, e só quando a coleta entrou nas turmas: um sync
// sem `--turmas` traz zero avaliações, e `reconciliar` trataria isso como "o
// professor apagou todas as provas" — aposentando de uma vez toda correção que
// o aluno tinha feito.
//
// A POLÍTICA está em uma linha só: o SIGAA vence. Quando o professor finalmente
// atualiza a plataforma com uma data diferente da corrigida, a dele passa a
// valer. Mas o ajuste é APOSENTADO, nunca apagado — fica no histórico, e é dele
// que a UI tira o botão "restaurar minha data". O alarme é obrigatório: esta é
// a única coisa que o ciclo faz que descarta algo que o usuário digitou.
void reconciliarAjustes(store::Database& db, Resultado& r, std::int64_t agora,
                        const Log& log) {
    if (r.snapshot.avaliacoes.empty()) return;

    auto rec = avaliacao::reconciliar(r.snapshot.avaliacoes, db.carregarAjustes());
    if (!rec.mudou) return;

    if (!db.gravarAjustes(rec.ajustes)) {
        diz(log, Nivel::Aviso, "falha ao atualizar as correcoes (" + db.erro() + ")");
    }

    for (const auto& c : rec.conflitos) {
        avaliacao::Mudanca m;
        m.idTurma = c.idTurma;
        m.turmaNome = c.turmaNome;
        m.descricao = c.descricao;
        m.tipo = avaliacao::TipoMudanca::SigaaAtropelou;
        m.de = c.doAluno.toIso();
        m.para = c.doSigaaAgora.toIso();
        m.nota = "o professor atualizou a plataforma";
        m.quando = agora;
        db.registrarMudanca(m);

        diz(log, Nivel::Aviso, "a data que voce corrigiu em \"" + c.descricao +
                                   "\" foi substituida pelo SIGAA: " +
                                   c.doAluno.toIso() + " -> " + c.doSigaaAgora.toIso());
    }
    r.conflitosAvaliacao = std::move(rec.conflitos);
}

} // namespace

Resultado executar(Opcoes op, const Log& log) {
    if (op.login.empty() || op.senha.empty()) {
        limpar(op.senha);
        return falhar(Falha::Credenciais, "login ou senha vazios");
    }

    // --- sessão --------------------------------------------------------------
    // Reusa a sessão de quem chamou, quando há uma. Login é a operação mais
    // cara do ciclo, e repeti-la a cada tarefa era o que fazia o app parecer
    // travado — o SIGAA não responde ao segundo login com outra sessão aberta.
    http::SessaoViva propria;
    http::SessaoViva& cofre = op.sessao ? *op.sessao : propria;

    const bool jaTinha = cofre.viva();
    diz(log, Nivel::Passo, jaTinha ? "reaproveitando a sessao aberta"
                                   : "autenticando...");

    std::string erroLogin;
    http::SigaaSession* psess = cofre.obter(op.login, op.senha, &erroLogin);
    limpar(op.senha);   // fora de escopo pelo resto do ciclo

    if (!psess) return falhar(Falha::Login, std::move(erroLogin));
    http::SigaaSession& sess = *psess;

    // --- coleta --------------------------------------------------------------
    // Participantes: decidido ANTES de coletar, e por isso o banco é aberto
    // aqui num escopo próprio — a conexão de baixo, que faz o diff e grava, só
    // nasce depois da rede. Duas conexões ao mesmo arquivo SQLite em WAL
    // convivem sem problema, e abrir de novo custa microssegundos contra os
    // ~1,5 s de UMA requisição que esta pergunta evita por turma.
    bool buscarParticipantes = false;
    if (op.incluirTurmas && op.participantesSeBancoVazio) {
        store::Database espia(op.caminhoBanco);
        // Banco indisponível NÃO liga a coleta: sem onde gravar, as
        // requisições seriam gastas para preencher uma tela e jogar fora.
        buscarParticipantes = espia.aberto() && espia.migrar() &&
                              espia.participantesGuardados() == 0;
        if (buscarParticipantes) {
            diz(log, Nivel::Passo,
                "banco sem participantes: esta coleta tambem le quem esta em cada turma");
        }
    }

    sync::OpcoesColeta oc;
    oc.incluirTurmas = op.incluirTurmas;
    oc.incluirArquivos = op.incluirArquivos;
    oc.incluirParticipantes = buscarParticipantes;
    oc.incluirFrequencia = op.incluirFrequencia;
    oc.apenasTurmas = op.apenasTurmas;
    if (log) {
        oc.progresso = [&log](const std::string& m) { diz(log, Nivel::Passo, m); };
    }

    const auto col = sync::coletar(sess, oc);
    if (!col.ok) return falhar(Falha::Coleta, col.erro);

    Resultado r;
    r.snapshot = col.snapshot;
    r.turmasVisitadas = col.turmasVisitadas;
    r.turmasComFalha = col.turmasComFalha;
    // Coleta pode devolver ok=true com erro preenchido: turma que falhou não
    // invalida o ciclo, mas o usuário precisa saber que a foto está incompleta.
    if (!col.erro.empty()) diz(log, Nivel::Aviso, col.erro);

    if (!op.dumpHtml.empty()) {
        if (std::ofstream d(op.dumpHtml, std::ios::binary); d) {
            d << col.htmlPortal;
            diz(log, Nivel::Aviso,
                "html cru salvo em " + op.dumpHtml +
                    " — CONTEM DADOS PESSOAIS, rode tools/redact.py");
        }
        if (!col.htmlPrimeiraTurma.empty()) {
            const std::string t = op.dumpHtml + ".turma.html";
            if (std::ofstream dt(t, std::ios::binary); dt) dt << col.htmlPrimeiraTurma;
        }
    }

    // --- diff e persistência -------------------------------------------------
    store::Database db(op.caminhoBanco);
    if (!db.aberto() || !db.migrar()) {
        // Sem banco não há base de comparação: o relatório desta rodada ainda
        // vale, mas "o que mudou?" fica sem resposta. Seguir em frente é melhor
        // que abortar — o usuário pelo menos vê os prazos.
        diz(log, Nivel::Aviso, "banco indisponivel (" + db.erro() +
                                   "); seguindo sem historico");
    } else {
        r.bancoDisponivel = true;

        const bool primeira = db.ciclos() == 0;
        const auto anterior = db.carregarUltimo();
        r.diff = sync::diff(anterior, r.snapshot, primeira);
        r.aviso = notify::montarAviso(r.diff);

        if (r.diff.suspeito) {
            // Não grava: sobrescrever o histórico bom com uma coleta
            // provavelmente quebrada destruiria a única base de comparação
            // confiável — e o erro seguinte seria silencioso.
            //
            // `relatorio` e `ics` ficam vazios (ver Servico.h): nada foi
            // escrito, e não há para onde mandar o usuário nesta rodada.
            r.falha = Falha::ColetaSuspeita;
            r.erro = "a coleta veio incompleta; nada foi gravado";
            return r;
        }

        const auto agora = static_cast<std::int64_t>(std::time(nullptr));
        if (db.gravar(r.snapshot, agora)) {
            db.registrarCiclo(agora);
            r.gravou = true;
        } else {
            diz(log, Nivel::Aviso, "falha ao gravar (" + db.erro() + ")");
        }

        reconciliarAjustes(db, r, agora, log);
    }

    // --- material offline ----------------------------------------------------
    // Depois de gravar, e nunca antes: baixar arquivo de uma coleta que o diff
    // classificou como suspeita encheria o disco com material de uma leitura
    // em que nem nós confiamos (o `return` do ramo suspeito já saiu acima).
    if (!op.pastaMateriais.empty() && !r.snapshot.arquivos.empty()) {
        baixarPendentes(sess, r.snapshot, op.pastaMateriais, r, log);
    }

    // NAO baixamos mais retrato de participante. O bloco que ficava aqui
    // copiava para o disco do aluno a foto de cada colega da turma — dado
    // pessoal de 31 pessoas que nenhuma delas escolheu espalhar. Removido em
    // 18/09/2026; a lista mostra as iniciais, que resolvem o mesmo problema
    // (reconhecer alguem de relance) sem copiar o rosto de ninguem.

    // --- relatório -----------------------------------------------------------
    {
        std::ofstream f(op.caminhoRelatorio, std::ios::binary);
        if (!f) {
            return falhar(Falha::Escrita,
                          "nao consegui escrever " + op.caminhoRelatorio);
        }
        f << report::renderHtml(r.snapshot);
    }   // fecha antes de anunciar: o clique tem que achar o arquivo completo
    r.relatorio = absoluto(op.caminhoRelatorio);

    // .ics ao lado do relatório: prazos e provas no calendário do celular.
    // Falhar aqui não derruba o ciclo — o relatório, que é o essencial, existe.
    {
        // Relidas do banco, e não reaproveitadas da reconciliação: ela só roda
        // quando a coleta entrou nas turmas, e o .ics é escrito em toda rodada.
        // Sem esta leitura, um sync leve exportaria o calendário sem nenhuma
        // das correções do aluno — apagando-as do celular dele.
        const std::vector<avaliacao::Ajuste> ajustesParaIcs =
            db.aberto() ? db.carregarAjustes() : std::vector<avaliacao::Ajuste>{};
        const std::string ics =
            op.caminhoIcs.empty() ? trocarParaIcs(op.caminhoRelatorio) : op.caminhoIcs;
        if (std::ofstream fi(ics, std::ios::binary); fi) {
            // Com as correções do aluno: o .ics é o canal em que a data
            // vira alarme no celular, e é onde ela será obedecida sem
            // ninguém reconferir.
            fi << calendario::gerarIcs(r.snapshot, {}, ajustesParaIcs);
            r.ics = absoluto(ics);
        } else {
            diz(log, Nivel::Aviso, "nao consegui escrever o calendario " + ics);
        }
    }

    return r;
}

} // namespace sigaa::servico
