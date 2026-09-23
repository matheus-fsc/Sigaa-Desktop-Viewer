#include "core/parse/FrequenciaParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace sigaa::parse {
namespace {

// "07/08/2026" -> DateTime. O mapa nunca traz hora.
DateTime dataBr(const std::string& s) {
    DateTime d;
    if (s.size() < 10) return d;
    d.day = std::atoi(s.substr(0, 2).c_str());
    d.month = std::atoi(s.substr(3, 2).c_str());
    d.year = std::atoi(s.substr(6, 4).c_str());
    return d;
}

// O primeiro inteiro depois de `rotulo` no texto corrido do rodapé.
//
// Por posição no texto, e não por seletor: os três números moram soltos dentro
// do MESMO `div.botoes-show`, separados por <b> e <br/>, sem um nó próprio
// para cada um. Ancorar no rótulo é o que sobra — e é estável, porque o rótulo
// é a frase que o SIGAA mostra ao aluno.
//
// OS RÓTULOS SÃO ASCII DE PROPÓSITO. O SIGAA escreve "Presen&#231;as" e
// "Frequ&#234;ncia"; casar a palavra acentuada obrigaria este arquivo a saber
// se o `text()` já decodificou a entidade — um acoplamento que quebraria em
// silêncio no dia em que a resposta viesse noutra codificação. Prefixos ASCII
// não têm essa dúvida, e os três são únicos dentro do rodapé.
//
// A JANELA existe para não capturar o número do rótulo DE BAIXO: se este vier
// sem número, varrer até achar um dígito devolveria um total que pertence a
// outra pergunta. Devolve -1 nesse caso — "não achei" não pode virar "zero".
int inteiroApos(const std::string& texto, const std::string& rotulo) {
    constexpr size_t kJanela = 40;   // cabe ":" + o resto do rótulo + espaços

    const size_t i = texto.find(rotulo);
    if (i == std::string::npos) return -1;

    const size_t inicio = i + rotulo.size();
    const size_t fim = std::min(texto.size(), inicio + kJanela);

    size_t j = inicio;
    while (j < fim && !std::isdigit(static_cast<unsigned char>(texto[j]))) ++j;
    if (j >= fim) return -1;

    size_t k = j;
    while (k < texto.size() && std::isdigit(static_cast<unsigned char>(texto[k]))) ++k;
    return std::atoi(texto.substr(j, k - j).c_str());
}

// "2 Falta(s)" -> 2. "Falta(s)" sem número -> 1.
int quantasFaltas(const std::string& s) {
    const int n = std::atoi(s.c_str());
    return n > 0 ? n : 1;
}

} // namespace

ResultadoFrequencia parseFrequencia(const html::Document& doc,
                                    const std::string& idTurma,
                                    const std::string& turmaNome) {
    ResultadoFrequencia out;
    out.frequencia.idTurma = idTurma;
    out.frequencia.turmaNome = turmaNome;

    // O <legend> é o que identifica a tela. Serve de guarda contra tratar uma
    // página de login expirado como "turma sem frequência lançada".
    for (const auto& l : doc.select("fieldset legend")) {
        if (l.text().find("Frequ") != std::string::npos) {
            out.pareceMapaDeFrequencia = true;
        }
    }

    // --- linhas ------------------------------------------------------------
    for (const auto& linha : doc.select("table.listing tbody tr")) {
        const auto tds = linha.select("td");
        if (tds.size() < 2) continue;   // cabeçalho ou linha de paginação

        const std::string dataTxt = html::trim(tds[0].text());
        const std::string sitTxt = html::collapseWhitespace(tds[1].text());

        DiaFrequencia dia;
        dia.data = dataBr(dataTxt);
        if (!dia.data.valid()) continue;

        if (sitTxt.find("Falta") != std::string::npos) {
            dia.situacao = SituacaoDia::Falta;
            dia.faltas = quantasFaltas(sitTxt);
        } else if (sitTxt.find("Presente") != std::string::npos) {
            dia.situacao = SituacaoDia::Presente;
        } else {
            // "Não Registrada" — e qualquer coisa que o SIGAA invente e não
            // reconheçamos. Chamar de presença o que não entendemos seria o
            // erro caro: o aluno fecharia o app achando que está em dia.
            dia.situacao = SituacaoDia::NaoRegistrada;
        }
        out.frequencia.dias.push_back(dia);
    }

    // --- totais ------------------------------------------------------------
    std::string rodape;
    for (const auto& d : doc.select("div.botoes-show")) rodape += d.text();
    rodape = html::collapseWhitespace(rodape);

    const int presencas = inteiroApos(rodape, "Registradas");
    const int comRegistro = inteiroApos(rodape, "Aulas com Registro");
    const int pelaCH = inteiroApos(rodape, "Aulas definidas pela CH");

    if (presencas >= 0) out.frequencia.presencas = presencas;
    if (comRegistro >= 0) out.frequencia.aulasComRegistro = comRegistro;
    if (pelaCH >= 0) out.frequencia.aulasPelaCH = pelaCH;

    // Só há dados quando o SIGAA contou alguma aula. Uma turma cujo professor
    // nunca abriu o diário devolve a página com a tabela vazia e o rodapé
    // zerado — e "zero faltas" ali seria uma mentira tranquilizadora.
    out.frequencia.temDados = comRegistro > 0 || !out.frequencia.dias.empty();

    return out;
}

} // namespace sigaa::parse
