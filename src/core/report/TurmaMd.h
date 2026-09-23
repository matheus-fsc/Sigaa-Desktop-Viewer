#pragma once
// Um resumo da turma em Markdown, gravado na pasta dela.
//
// PARA QUE SERVE: ser colado num assistente de IA. O aluno já tem os PDFs da
// turma numa pasta; o que falta é o FIO — quais aulas houve, em que datas, que
// conteúdo cada uma cobriu, quando são as provas. Sem isso, a IA recebe vinte
// arquivos soltos e nenhuma ordem, e "me ajude a estudar para a prova de
// quinta" vira adivinhação.
//
// Markdown, e não JSON ou HTML: é o formato que um modelo de linguagem lê sem
// intermediário, que uma pessoa lê sem ferramenta, e que o editor de qualquer
// um renderiza. O arquivo fica ao lado dos PDFs, na mesma pasta que o app já
// mantém — quem arrasta a pasta para o chat leva o índice junto.
//
// O QUE NÃO ENTRA: nome de colega, e-mail, matrícula. A lista de participantes
// é dado de outras pessoas e não ajuda ninguém a estudar (ver docs/RECON.md
// §5.1). Entram as aulas, os materiais, as avaliações e as faltas do próprio
// aluno.

#include <string>
#include <vector>

#include "core/avaliacao/Ajustes.h"
#include "core/model/Models.h"

namespace sigaa::report {

struct DadosTurmaMd {
    Turma turma;
    std::vector<TopicoAula> topicos;
    std::vector<ArquivoTurma> arquivos;
    std::vector<avaliacao::Efetiva> provas;
    const Frequencia* frequencia{nullptr};   // opcional
};

// O conteúdo do `turma.md`. Determinístico: a mesma entrada dá sempre o mesmo
// texto, para que regravar não produza diferença espúria no disco de quem
// versiona a pasta.
std::string gerarTurmaMd(const DadosTurmaMd& d);

// Nome do arquivo dentro da pasta da turma.
inline constexpr const char* kNomeTurmaMd = "turma.md";

} // namespace sigaa::report
