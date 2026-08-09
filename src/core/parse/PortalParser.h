#pragma once
// Extrai o conteúdo do portal do discente (discente.jsf).
//
// Três blocos independentes, cada um com sua idiossincrasia (docs/RECON.md):
//   - tabela "Turmas do Semestre" -> Turma  (local, horário, frontEndIdTurma)
//   - #formAtualizacoesTurmas     -> Atualizacao (sem ID; a chave é um hash)
//   - #formAtividades             -> Atividade   (chave: atividadeId + idTurma)

#include <string>
#include <string_view>
#include <vector>

#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::parse {

std::vector<Turma> parseTurmas(const html::Document& doc);
std::vector<Atualizacao> parseAtualizacoes(const html::Document& doc);
std::vector<Atividade> parseAtividades(const html::Document& doc);

// Conveniência: roda os três e devolve tudo junto.
Snapshot parsePortal(const html::Document& doc);

// Expostos para teste.
// Aceita "11/08/2026", "11/08/2026 23:59" e "07/08/2026 - ...".
DateTime parseDataHora(std::string_view s);
// Hash estável de uma atualização, já que o SIGAA não fornece ID.
std::string hashAtualizacao(std::string_view data, std::string_view idTurma,
                            std::string_view texto);

} // namespace sigaa::parse
