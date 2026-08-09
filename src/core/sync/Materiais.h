#pragma once
// "Entrar na turma" como no SIGAA web: abrir a aba Arquivos e baixar material.
//
// Por que uma CLASSE e não funções soltas: a navegação do SIGAA é uma máquina
// de estados no servidor. O ViewState vale para a view corrente, e o comando de
// baixar só existe dentro da página de Arquivos que acabou de ser renderizada.
// Guardar esse estado num objeto é o que evita o padrão "volta ao portal e
// refaz tudo" a cada arquivo — que custaria 3 requisições por download.
//
// Custo: entrar 2 (turma + aba), depois 1 por arquivo.
//
// LIMITE CONHECIDO: o arquivo inteiro passa pela memória antes de ir ao disco,
// porque o libcurl aqui escreve num std::string. Serve para PDF de aula; um
// vídeo de 1 GB publicado pelo professor faria o app inchar. Trocar por escrita
// em streaming é uma mudança no SigaaSession, não aqui.

#include <optional>
#include <string>
#include <vector>

#include "core/http/SigaaSession.h"
#include "core/model/Models.h"
#include "core/parse/Html.h"

namespace sigaa::sync {

class SessaoTurma {
public:
    explicit SessaoTurma(http::SigaaSession& sessao) : sessao_(sessao) {}

    // Portal -> página da turma. `turma.frontEndId` é obrigatório.
    bool entrar(const Turma& turma, std::string* erro = nullptr);

    // Turma -> aba Arquivos. Só depois de `entrar`.
    bool abrirArquivos(std::string* erro = nullptr);

    const std::vector<ArquivoTurma>& arquivos() const { return arquivos_; }

    // O professor não publicou nada (o SIGAA disse isso, não deduzimos).
    bool semArquivos() const { return vazioConfirmado_; }

    // Rótulos do menu lateral, como o SIGAA os escreve. Serve para a UI
    // oferecer as outras abas sem cravar uma lista que o SIGAA pode mudar.
    const std::vector<std::string>& menu() const { return menu_; }

    // Baixa para dentro de `diretorio` e devolve o caminho final.
    //
    // O nome sai do Content-Disposition, não do título da tabela: o título é
    // texto livre do professor ("Notas de Aula - Aula 01") e não tem extensão,
    // então salvar com ele daria um arquivo que o Windows não sabe abrir.
    std::optional<std::string> baixar(const std::string& idArquivo,
                                      const std::string& diretorio,
                                      std::string* erro = nullptr);

private:
    bool recarregarAbaArquivos(std::string* erro);

    http::SigaaSession& sessao_;
    html::Document docTurma_;
    html::Document docArquivos_;
    Turma turma_;
    std::vector<ArquivoTurma> arquivos_;
    std::vector<std::string> menu_;
    bool vazioConfirmado_{false};
    bool naTurma_{false};
};

} // namespace sigaa::sync
