#include "core/sync/DiffEngine.h"

#include "core/parse/NoticiaParser.h"

#include <map>
#include <set>

namespace sigaa::sync {
namespace {

std::string chaveAtividade(const Atividade& a) {
    return a.atividadeId + "|" + a.idTurma;
}

} // namespace

std::string_view toString(TipoEvento t) {
    switch (t) {
        case TipoEvento::TurmaNova:          return "turma-nova";
        case TipoEvento::AtividadeNova:      return "atividade-nova";
        case TipoEvento::PrazoMudou:         return "prazo-mudou";
        case TipoEvento::AtividadeConcluida: return "atividade-concluida";
        case TipoEvento::AtualizacaoNova:    return "atualizacao-nova";
        case TipoEvento::AvaliacaoNova:      return "avaliacao-nova";
        case TipoEvento::AvaliacaoRemarcada: return "avaliacao-remarcada";
        case TipoEvento::MaterialNovo:       return "material-novo";
        case TipoEvento::NoticiaNova:        return "noticia-nova";
        case TipoEvento::ColetaSuspeita:     return "coleta-suspeita";
    }
    return "?";
}

ResultadoDiff diff(const Snapshot& anterior, const Snapshot& novo,
                   bool primeiraExecucao) {
    ResultadoDiff res;
    res.primeiraExecucao = primeiraExecucao;

    // --- guarda: a coleta parece ter falhado? --------------------------------
    // Só vale a partir da segunda execução, e só quando havia algo antes.
    const bool sumiuTudoTurmas = !anterior.turmas.empty() && novo.turmas.empty();
    const bool sumiuTudoAtiv   = !anterior.atividades.empty() && novo.atividades.empty();

    if (!primeiraExecucao && (sumiuTudoTurmas || sumiuTudoAtiv)) {
        res.suspeito = true;
        std::string det;
        if (sumiuTudoTurmas) {
            det += "turmas: " + std::to_string(anterior.turmas.size()) + " -> 0. ";
        }
        if (sumiuTudoAtiv) {
            det += "atividades: " + std::to_string(anterior.atividades.size()) + " -> 0. ";
        }
        det += "O mais provavel e o parser ter quebrado (SIGAA mudou o HTML), "
               "nao os dados terem sumido. Nao confie na ausencia de prazos.";
        res.eventos.push_back({TipoEvento::ColetaSuspeita, "coleta-suspeita",
                               "A coleta parece incompleta", det, "", ""});
        // Sai cedo: comparar contra uma coleta provavelmente quebrada só
        // produziria ruído.
        return res;
    }

    // --- primeira execução: estabelece a linha de base, sem alarme ------------
    if (primeiraExecucao) return res;

    // --- turmas --------------------------------------------------------------
    std::set<std::string> turmasAntes;
    for (const auto& t : anterior.turmas) turmasAntes.insert(t.idTurma);
    for (const auto& t : novo.turmas) {
        if (turmasAntes.count(t.idTurma)) continue;
        res.eventos.push_back({TipoEvento::TurmaNova, t.idTurma, t.nome,
                               t.codigo + " " + t.horario + " " + t.local,
                               t.idTurma, t.nome});
    }

    // --- atividades ----------------------------------------------------------
    std::map<std::string, const Atividade*> antes;
    for (const auto& a : anterior.atividades) antes[chaveAtividade(a)] = &a;

    for (const auto& a : novo.atividades) {
        const std::string k = chaveAtividade(a);
        const auto it = antes.find(k);

        if (it == antes.end()) {
            res.eventos.push_back({TipoEvento::AtividadeNova, k, a.titulo,
                                   a.tipo + " - prazo " + a.prazo.toIso(),
                                   a.idTurma, a.turmaNome});
            continue;
        }

        const Atividade& velha = *it->second;

        // Prazo mudou: importante, porque o aluno pode ter se planejado pela
        // data antiga.
        if (velha.prazo.toIso() != a.prazo.toIso() && a.prazo.valid()) {
            res.eventos.push_back({TipoEvento::PrazoMudou, k, a.titulo,
                                   "de " + velha.prazo.toIso() + " para " + a.prazo.toIso(),
                                   a.idTurma, a.turmaNome});
        }

        if (velha.status != StatusAtividade::Concluida &&
            a.status == StatusAtividade::Concluida) {
            res.eventos.push_back({TipoEvento::AtividadeConcluida, k, a.titulo,
                                   "marcada como concluida", a.idTurma, a.turmaNome});
        }
    }

    // --- avaliações ----------------------------------------------------------
    // Só compara se a coleta desta rodada incluiu turmas. Um `sync` sem
    // `--turmas` não visita a turma virtual e traz zero avaliações — tratar
    // isso como "as provas sumiram" encheria o usuário de alarme falso.
    if (!novo.avaliacoes.empty()) {
        std::map<std::string, const Avaliacao*> avAntes;
        for (const auto& a : anterior.avaliacoes) {
            avAntes[a.idTurma + "|" + a.descricao] = &a;
        }
        for (const auto& a : novo.avaliacoes) {
            const std::string k = a.idTurma + "|" + a.descricao;
            const auto it = avAntes.find(k);
            if (it == avAntes.end()) {
                res.eventos.push_back({TipoEvento::AvaliacaoNova, k, a.descricao,
                                       "marcada para " + a.quando.toIso(), a.idTurma,
                                       a.turmaNome});
            } else if (it->second->quando.toIso() != a.quando.toIso() &&
                       a.quando.valid()) {
                res.eventos.push_back({TipoEvento::AvaliacaoRemarcada, k, a.descricao,
                                       "de " + it->second->quando.toIso() + " para " +
                                           a.quando.toIso(),
                                       a.idTurma, a.turmaNome});
            }
        }
    }

    // --- material publicado nas turmas ---------------------------------------
    // Mesma cautela das avaliações: uma coleta sem `--turmas` traz zero
    // arquivos, e comparar contra o histórico faria "nada coletado" virar
    // silêncio quando na verdade nem olhamos.
    //
    // A chave é o `idArquivo` (RECON §1.6.1), não o título: o título é texto
    // livre e o professor renomeia "Aula 3" para "Aula 03 - revisada" sem
    // publicar nada — isso viraria um material novo por renomeação, que é
    // exatamente o alarme falso que ensina alguém a ignorar as notificações.
    if (!novo.arquivos.empty()) {
        std::set<std::string> arqAntes;
        for (const auto& a : anterior.arquivos) arqAntes.insert(a.idArquivo);

        // Só compara turma a turma que ESTA rodada visitou. Se a coleta falhou
        // ao entrar numa turma, os arquivos dela não vieram — e sem este filtro
        // a próxima rodada bem-sucedida anunciaria o acervo inteiro dela como
        // novidade.
        std::set<std::string> visitadas;
        for (const auto& a : novo.arquivos) visitadas.insert(a.idTurma);

        std::set<std::string> conhecidas;
        for (const auto& a : anterior.arquivos) {
            if (visitadas.count(a.idTurma)) conhecidas.insert(a.idTurma);
        }

        for (const auto& a : novo.arquivos) {
            if (arqAntes.count(a.idArquivo)) continue;
            // Turma cujo material nunca vimos antes: é a linha de base dela,
            // não uma publicação de hoje.
            if (!conhecidas.count(a.idTurma)) continue;
            res.eventos.push_back({TipoEvento::MaterialNovo, a.idArquivo, a.titulo,
                                   a.topico.empty() ? "novo arquivo na turma"
                                                    : "em " + a.topico,
                                   a.idTurma, a.turmaNome});
        }
    }

    // --- notícias ------------------------------------------------------------
    // As mesmas duas guardas dos arquivos, pelo mesmo motivo:
    //   - só compara turma que ESTA rodada trouxe notícia (coleta que não
    //     entrou na turma, ou falhou na aba, não pode fazer a próxima anunciar
    //     o histórico inteiro dela);
    //   - turma que nunca teve notícia guardada é a linha de base: a primeira
    //     coleta de uma turma com vinte notícias antigas não é vinte novidades.
    // A chave é parse::chaveNoticia (turma, título, dia): a mesma notícia chega
    // três vezes na mesma rodada (a última da página, a lista, o detalhe), e
    // anunciá-la três vezes seria o alarme repetido que ensina a ignorar.
    if (!novo.noticias.empty()) {
        std::set<std::string> antes;
        std::set<std::string> turmasComNoticia;
        for (const auto& n : anterior.noticias) {
            antes.insert(parse::chaveNoticia(n.idTurma, n.titulo, n.data));
            turmasComNoticia.insert(n.idTurma);
        }
        // O texto pode ter vindo em qualquer uma das três cópias; o aviso usa
        // a primeira que tiver.
        std::map<std::string, const Noticia*> novas;
        std::vector<std::string> ordem;
        for (const auto& n : novo.noticias) {
            if (!turmasComNoticia.count(n.idTurma)) continue;
            const std::string chave = parse::chaveNoticia(n.idTurma, n.titulo, n.data);
            if (antes.count(chave)) continue;
            auto [it, inserida] = novas.emplace(chave, &n);
            if (inserida) ordem.push_back(chave);
            else if (it->second->conteudoHtml.empty() && !n.conteudoHtml.empty()) it->second = &n;
        }
        for (const auto& chave : ordem) {
            const Noticia& n = *novas[chave];
            std::string trecho = parse::textoDaNoticia(n.conteudoHtml);
            constexpr std::size_t kTrecho = 140;
            if (trecho.size() > kTrecho) {
                // Corta em fronteira de caractere UTF-8, nunca no meio de um.
                std::size_t corte = kTrecho;
                while (corte > 0 && (static_cast<unsigned char>(trecho[corte]) & 0xC0) == 0x80) --corte;
                trecho = trecho.substr(0, corte) + "…";
            }
            res.eventos.push_back({TipoEvento::NoticiaNova, chave, n.titulo,
                                   trecho.empty() ? "nova notícia na turma" : trecho,
                                   n.idTurma, n.turmaNome});
        }
    }

    // --- atualizações --------------------------------------------------------
    std::set<std::string> hashesAntes;
    for (const auto& u : anterior.atualizacoes) hashesAntes.insert(u.hash);
    for (const auto& u : novo.atualizacoes) {
        if (hashesAntes.count(u.hash)) continue;
        res.eventos.push_back({TipoEvento::AtualizacaoNova, u.hash, u.texto,
                               u.data.toIso(), u.idTurma, u.turmaNome});
    }

    return res;
}

} // namespace sigaa::sync
