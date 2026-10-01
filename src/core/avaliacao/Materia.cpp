#include "core/avaliacao/Materia.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

#include "core/parse/Html.h"
#include "core/util/Texto.h"

namespace sigaa::avaliacao {
namespace {

// O dia, sem a hora: aaaammdd. Zero quando inválida.
int dia(const DateTime& d) { return d.valid() ? d.year * 10000 + d.month * 100 + d.day : 0; }

} // namespace

MateriaDaProva materiaDaProva(const Snapshot& s, const Efetiva& prova,
                              const std::vector<Efetiva>& todas) {
    MateriaDaProva m;
    const int dataProva = dia(prova.av.quando);
    if (!dataProva) return m;

    // A anterior da mesma turma: a mais recente estritamente antes desta.
    // `todas` já está em ordem cronológica, então a última que passar vale.
    int desde = 0;
    for (const auto& p : todas) {
        const int d = dia(p.av.quando);
        if (p.av.idTurma != prova.av.idTurma || !d || d >= dataProva) continue;
        desde = d;
        m.desde = p.av.quando;
        m.desde.hasTime = false;
        m.desde.hour = m.desde.minute = 0;
        m.provaAnterior = html::collapseWhitespace(p.av.descricao);
    }

    std::vector<const TopicoAula*> escolhidos;
    for (const auto& t : s.topicos) {
        if (t.idTurma != prova.av.idTurma) continue;
        m.coletada = true;
        const int ini = dia(t.inicio);
        // A janela da prova: DEPOIS do dia da anterior e ANTES do dia desta.
        // Estritamente antes — o tópico registrado no próprio dia da prova é a
        // aula depois dela (em EDO, "Noções de sequências e séries" caiu em
        // 01/10, dia da Avaliação 1, e é matéria da Avaliação 2).
        // E o do dia da anterior ENTRA: pela mesma regra, ele já é matéria
        // desta. O anúncio da anterior, que cai no mesmo dia, sai pelo filtro
        // de "não é matéria" abaixo.
        if (!ini || ini >= dataProva) continue;
        if (desde && ini < desde) continue;
        escolhidos.push_back(&t);
    }
    // Estável: tópicos do mesmo dia ficam na ordem em que o professor os
    // registrou, que é a ordem da aula.
    std::stable_sort(escolhidos.begin(), escolhidos.end(),
                     [](const TopicoAula* a, const TopicoAula* b) { return a->inicio < b->inicio; });

    // O que NÃO é matéria, mesmo registrado como tópico: o anúncio de prova
    // ("Primeira avaliação", "Revisão para a P1"), aula de dúvidas ou de
    // exercícios, "Não haverá aula", a apresentação da disciplina. Entram na
    // conta dos ARQUIVOS (a lista da aula de exercícios é material de estudo),
    // mas não na lista de tópicos, onde seriam só ruído. Sobre o título
    // dobrado (sem acento), para a regex ser ASCII.
    static const std::regex naoEhMateria(
        R"(\b(prova|avalia|revis|duvida|exercicio|nao havera|sem aula|feriado|recesso|apresentacao da disciplina))");

    std::set<std::string> titulos;      // para casar arquivos: todos os da janela
    std::set<std::string> jaListados;   // para não repetir o mesmo título
    std::set<std::string> ids;
    for (const TopicoAula* t : escolhidos) {
        const std::string titulo = html::collapseWhitespace(t->titulo);
        titulos.insert(titulo);
        for (const auto& mat : t->materiais) {
            if (!mat.id.empty()) ids.insert(mat.id);
        }
        const std::string dobrado = util::dobrar(titulo);
        if (std::regex_search(dobrado, naoEhMateria)) continue;
        // Professor que registra a mesma aula duas vezes (ou repete o título
        // na aula seguinte) contava em dobro. A chave ignora caixa, acento e
        // pontuação.
        std::string semPontuacao = dobrado;
        for (char& c : semPontuacao) {
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x80 && !std::isalnum(u)) c = ' ';
        }
        const std::string chave = util::dobrar(semPontuacao);
        if (!jaListados.insert(chave).second) continue;
        m.topicos.push_back(titulo);
    }
    // Os da aba Arquivos, casados pelo título do tópico.
    for (const auto& a : s.arquivos) {
        if (a.idTurma == prova.av.idTurma && titulos.count(html::collapseWhitespace(a.topico))) {
            ids.insert(a.idArquivo);
        }
    }
    m.idsArquivos.assign(ids.begin(), ids.end());
    return m;
}

} // namespace sigaa::avaliacao
