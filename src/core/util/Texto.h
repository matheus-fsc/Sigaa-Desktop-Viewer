#pragma once
// Comparar texto como o aluno compara: sem acento e sem caixa.
//
// O aluno escreve "compiladores", "edo", "analise"; o SIGAA guarda
// "COMPILADORES", "EQUAÇÕES DIFERENCIAIS ORDINÁRIAS", "ANÁLISE E...". E o
// professor registra "Revisão", "REVISAO", "revisão" na mesma turma. Sem uma
// forma única de comparar, cada lugar do código inventaria a sua.

#include <string>
#include <string_view>

namespace sigaa::util {

// Minúsculas e sem acento, para COMPARAR (nunca para mostrar): "Análise" →
// "analise", "AÇÃO" → "acao". Só o latim do português é dobrado; o resto do
// UTF-8 passa intacto. Espaços em sequência viram um, e as pontas somem.
std::string dobrar(std::string_view s);

// `agulha` aparece em `palheiro`, os dois dobrados.
bool contemDobrado(std::string_view palheiro, std::string_view agulha);

} // namespace sigaa::util
