#pragma once
// Extrai o conteúdo de uma página de Turma Virtual (/sigaa/ava/index.jsf).
//
// Duas fontes independentes de avaliação, que NÃO coincidem (docs/RECON.md §1.6):
//   - painel "Avaliações" na barra direita  -> fonte de alta confiança
//   - títulos dos tópicos de aula           -> fallback por regex
// Professores usam um, outro, ou os dois. O calendário precisa mesclar.

#include <string>
#include <vector>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

struct ConteudoTurma {
    std::vector<TopicoAula> topicos;
    std::vector<Avaliacao> avaliacoes;
    // O professor não cadastrou nada (bloco .intro-aval). Não é erro —
    // é o estado normal de várias turmas.
    bool semTopicos{false};
};

ConteudoTurma parseTurmaVirtual(const html::Document& doc,
                                const std::string& idTurma,
                                const std::string& turmaNome,
                                const std::string& periodo);

// --- expostos para teste ---------------------------------------------------

// "/sigaa/img/porta_arquivos/icones/tarefa.png" -> "tarefa". "" se não casar.
std::string tipoDoIcone(std::string_view src);

// Os `.item` de um `.conteudotopico`: os materiais que o professor pendurou
// naquela aula. Recebe o nó do conteúdo, não o do tópico.
std::vector<MaterialTopico> parseMateriais(const html::Node& conteudo);

// "Primeira avaliação (29/09/2026 - 29/09/2026)" -> título + as duas datas.
bool parseTituloTopico(std::string_view titulo, std::string& nome,
                       DateTime& inicio, DateTime& fim);

// O título do tópico indica uma avaliação?
bool pareceAvaliacao(std::string_view titulo);

// "04/09 6T34" ou "29/09 15:45" -> data (sem ano) + horário bruto.
// O ano NÃO vem no painel; é inferido do período letivo.
bool parseDataPainel(std::string_view s, int anoLetivo, DateTime& quando,
                     std::string& horarioBruto);

// "2026.2" -> 2026. Zero se não parsear.
int anoDoPeriodo(std::string_view periodo);

} // namespace sigaa::parse
