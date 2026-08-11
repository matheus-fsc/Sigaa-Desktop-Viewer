#pragma once
// Um ciclo de sincronização completo, do login ao relatório no disco.
//
// POR QUE ISTO EXISTE: até a Fase 2 esta orquestração morava dentro de
// `cmdSync()`, em src/app/main.cpp — 200 linhas amarrando login, coleta, diff,
// gravação, relatório e .ics. Enquanto o CLI era o único consumidor isso não
// incomodava; com a UI da Fase 3 ela teria que duplicar a sequência inteira ou
// chamar o executável por processo. As duas saídas apodrecem em direções
// diferentes, e a regra que importa (coleta suspeita não grava) ficaria escrita
// duas vezes.
//
// O QUE ESTE MÓDULO NÃO FAZ, DE PROPÓSITO:
//
//   - NÃO pergunta credenciais. Prompt de terminal é decisão de interface; a UI
//     vai ler do keychain. Quem chama entrega login e senha já resolvidos.
//   - NÃO notifica. `plat::notificar` vive em src/platform, e core/ não conhece
//     plataforma. O serviço devolve o `aviso` pronto e quem chama decide se
//     isso vira toast (CLI agendado) ou banner na janela (UI).
//   - NÃO escreve em std::cerr. Todo texto sai pelo callback `Log`, senão a UI
//     herdaria mensagens destinadas a um terminal que ela não tem.
//
// Erro não é log: falha vai no `Resultado`, para quem chama ter que olhar.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/model/Models.h"
#include "core/notify/Aviso.h"
#include "core/sync/DiffEngine.h"

namespace sigaa::servico {

struct Opcoes {
    std::string login;
    std::string senha;

    bool incluirTurmas{false};

    // Dentro de cada turma visitada, abre também a aba Arquivos. É o que faz o
    // app perceber o PDF que o professor subiu hoje — o portal não conta isso.
    // Custa uma requisição por turma e só tem efeito com `incluirTurmas`.
    bool incluirArquivos{true};

    std::string caminhoRelatorio{"relatorio.html"};
    // Vazio = deriva do relatório trocando a extensão por .ics.
    std::string caminhoIcs;
    std::string caminhoBanco{"sigaa-viewer.db"};

    // Pasta raiz onde o material das turmas fica salvo (uma subpasta por
    // turma). VAZIO = não baixa nada, que é o padrão.
    //
    // Quando preenchida, o ciclo baixa o que ainda não está no disco depois de
    // coletar. Só faz sentido com `incluirTurmas`: sem entrar nas turmas, o
    // ciclo não sabe que existe arquivo nenhum.
    //
    // Barato no dia a dia e caro uma vez: o cache é por `idArquivo`, então a
    // primeira execução baixa o semestre inteiro e as seguintes baixam o PDF
    // da aula da semana. Quem chama decide se aceita essa primeira conta — a
    // UI liga no "Atualizar tudo", o CLI só com `--materiais`.
    std::string pastaMateriais;

    // HTML cru da rede, para gerar fixture de parser (RECON §1.7). Vazio =
    // não grava. CONTÉM DADOS PESSOAIS — passar por tools/redact.py.
    std::string dumpHtml;
};

enum class Nivel {
    Passo,   // progresso normal: "autenticando...", "buscando o portal"
    Aviso,   // degradou mas seguiu em frente: banco indisponível, turma falhou
};

using Log = std::function<void(Nivel, std::string_view)>;

enum class Falha {
    Nenhuma,
    Credenciais,      // login ou senha vazios — nem tentou
    Login,
    Coleta,
    ColetaSuspeita,   // coletou, mas o resultado não é confiável; NÃO gravou
    Escrita,          // não conseguiu escrever o relatório
};

struct Resultado {
    Falha falha{Falha::Nenhuma};
    std::string erro;

    Snapshot snapshot;
    sync::ResultadoDiff diff;

    // O que dizer ao usuário, se é que há algo. nullopt = silêncio é a resposta
    // certa (ver core/notify/Aviso.h).
    std::optional<notify::Aviso> aviso;

    bool bancoDisponivel{false};
    bool gravou{false};

    int turmasVisitadas{0};
    int turmasComFalha{0};

    // Material baixado nesta rodada (só com `pastaMateriais`). `pendentes` é o
    // que faltava no disco quando o ciclo começou — a diferença entre os dois
    // é o que falhou.
    int materiaisPendentes{0};
    int materiaisBaixados{0};

    // Caminhos ABSOLUTOS do que foi escrito, ou vazio. O clique na notificação
    // precisa de caminho absoluto: o shell não herda nosso diretório de
    // trabalho.
    //
    // Em ColetaSuspeita ficam vazios de propósito — não houve relatório nesta
    // rodada, e apontar o usuário para o relatório velho o faria confiar em
    // dado vencido. Quem chama não precisa saber dessa regra: basta não montar
    // link quando o caminho vem vazio.
    std::string relatorio;
    std::string ics;

    bool ok() const { return falha == Falha::Nenhuma; }
};

// Roda o ciclo inteiro. Bloqueia (um ciclo com --turmas leva ~25 s); a UI deve
// chamar fora da thread de interface.
//
// `op` vem por valor porque a senha é zerada da cópia local assim que o login
// termina. Quem chama continua responsável por zerar a sua — ver app::limpar.
Resultado executar(Opcoes op, const Log& log = {});

} // namespace sigaa::servico
