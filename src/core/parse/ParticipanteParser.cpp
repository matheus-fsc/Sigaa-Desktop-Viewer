#include "core/parse/ParticipanteParser.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sigaa::parse {
namespace {

// Todos os rotulos dos dois blocos, em ASCII: sao PREFIXOS do que o SIGAA
// escreve ("Matr" casa "Matricula:" com ou sem acento, "Forma" casa
// "Formacao:"). O projeto nao usa literal acentuado em codigo de comparacao —
// o texto chega decodificado pelo lexbor e a fonte fica ASCII puro.
//
// A lista faz dois papeis: reconhecer o campo que se quer, e DELIMITAR o valor
// dele, que vai ate o proximo rotulo qualquer.
constexpr std::array<std::string_view, 5> kRotulos{
    "Curso", "Matr", "Departamento", "Forma", "E-mail",
};

char baixa(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// find() ignorando caixa. Existe por causa de "E-Mail:" (docente) e "E-mail:"
// (discente) — a mesma pagina escreve o rotulo das duas formas.
std::size_t buscaCI(const std::string& palheiro, std::string_view agulha,
                    std::size_t desde = 0) {
    if (agulha.empty() || agulha.size() > palheiro.size()) return std::string::npos;
    const auto ultimo = palheiro.size() - agulha.size();
    for (std::size_t i = desde; i <= ultimo; ++i) {
        std::size_t j = 0;
        while (j < agulha.size() && baixa(palheiro[i + j]) == baixa(agulha[j])) ++j;
        if (j == agulha.size()) return i;
    }
    return std::string::npos;
}

} // namespace

std::string campoRotulado(const std::string& texto, std::string_view rotulo) {
    const auto inicio = buscaCI(texto, rotulo);
    if (inicio == std::string::npos) return {};

    // Do fim do rotulo ate o ':' — o SIGAA as vezes mete espaco antes dele.
    auto p = texto.find(':', inicio + rotulo.size());
    if (p == std::string::npos) return {};
    ++p;

    // O valor termina no proximo rotulo, venha ele qual vier. Procurar so pelo
    // rotulo "seguinte" numa ordem fixa quebraria na linha em que um campo
    // falta — e falta: o discente sem e-mail cadastrado existe.
    std::size_t fim = texto.size();
    for (const auto& outro : kRotulos) {
        const auto q = buscaCI(texto, outro, p);
        if (q != std::string::npos) fim = std::min(fim, q);
    }

    return html::trim(texto.substr(p, fim - p));
}

int contagemDoLegend(std::string_view legend) {
    const auto abre = legend.find('(');
    if (abre == std::string_view::npos) return 0;
    int n = 0;
    bool viuDigito = false;
    for (auto i = abre + 1; i < legend.size() && legend[i] != ')'; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(legend[i]))) return viuDigito ? n : 0;
        n = n * 10 + (legend[i] - '0');
        viuDigito = true;
    }
    return viuDigito ? n : 0;
}

ListaParticipantes parseParticipantes(const html::Document& doc,
                                      const std::string& idTurma,
                                      const std::string& turmaNome) {
    ListaParticipantes out;

    for (const auto& l : doc.select("fieldset legend")) {
        const auto t = l.text();
        if (t.find("Docente") != std::string::npos) {
            out.pareceAbaParticipantes = true;
            out.docentesDeclarados = contagemDoLegend(t);
        } else if (t.find("Discente") != std::string::npos) {
            out.pareceAbaParticipantes = true;
            out.discentesDeclarados = contagemDoLegend(t);
        }
    }

    // A unidade e a CELULA, nao a linha: o bloco de discentes vem em DUAS
    // COLUNAS de participantes, entao cada <tr> guarda duas pessoas (6 <td>).
    // Iterar por linha juntava os dois numa so, e o e-mail do primeiro saia
    // com o nome do segundo grudado no fim.
    //
    // O seletor e ancorado em `table.participantes` de proposito. A pagina tem
    // OUTRO "Matricula:" fora dela — num formulario de busca, com o <em> vazio.
    // Varrer o documento inteiro colheria aquele campo como se fosse gente.
    //
    // A FOTO NAO E LIDA, e a ausencia e deliberada. A pagina traz, num <td>
    // proprio ao lado de cada pessoa, uma URL `/shared/verFoto?...&key=...`
    // cujo `key` e um token de acesso ao retrato de um terceiro. O app chegou a
    // baixar e guardar essas imagens (core/sync/Fotos.h, removido em
    // 18/09/2026): e dado pessoal de 31 colegas que nenhum deles escolheu
    // espalhar pela maquina dos outros.
    //
    // Nao extrair e mais forte que extrair e nao usar — o campo nao existe, o
    // token nao passa pela memoria do app, e quem for reintroduzir a coleta
    // tera de reescrever isto de proposito em vez de so ligar uma flag.
    // Na lista, o rosto foi trocado pelas iniciais (ui/Modelos.cpp).

    for (const auto& celula : doc.select("table.participantes td")) {
        // `textoVisivel` e nao `text`: cada celula carrega o <script> do A4J do
        // botao de perfil, e o corpo dele entraria no meio do nome.
        const std::string texto = celula.textoVisivel();

        Participante p;
        p.matricula = campoRotulado(texto, "Matr");
        p.curso = campoRotulado(texto, "Curso");
        p.departamento = campoRotulado(texto, "Departamento");
        p.formacao = campoRotulado(texto, "Forma");
        p.email = campoRotulado(texto, "E-mail");

        // Nem toda celula e gente: ha a da foto, a do botao de mensagem e a
        // `.subFormulario` que separa os grupos. Os rotulos sao o que
        // distingue — e por isso a foto pode faltar (eram 19 para 32 pessoas)
        // sem que ninguem suma da lista.
        const bool ehDocente = !p.departamento.empty() || !p.formacao.empty();
        const bool ehDiscente = !p.matricula.empty() || !p.curso.empty();
        if (!ehDocente && !ehDiscente) continue;

        // O nome esta no <strong> nos DOIS blocos: no discente como texto
        // solto, no docente dentro do link para a pagina publica. `text()`
        // devolve os dois casos sem que o parser precise saber a diferenca.
        const auto forte = celula.selectFirst("strong");
        if (!forte) continue;
        p.nome = html::trim(forte.text());
        if (p.nome.empty()) continue;

        p.papel = ehDocente ? PapelParticipante::Docente : PapelParticipante::Discente;
        p.idTurma = idTurma;
        p.turmaNome = turmaNome;
        out.participantes.push_back(std::move(p));
    }

    return out;
}

} // namespace sigaa::parse
