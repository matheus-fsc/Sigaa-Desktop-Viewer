#pragma once
// Baixar o material de uma turma inteira de uma vez, e saber o que já está no
// disco para nunca baixar duas vezes.
//
// SOBRE PARALELISMO — e por que ele NÃO é "uma sessão, várias requisições":
//
//   O invariante nº 1 do SigaaSession continua valendo: uma requisição por vez
//   dentro de uma sessão. O SIGAA guarda a view no servidor, e duas navegações
//   simultâneas no mesmo JSESSIONID invalidam-na — o segundo download voltaria
//   como página de erro salva com extensão .pdf, que é o pior tipo de falha
//   porque só aparece quando o aluno tenta abrir o arquivo.
//
//   O que dá para paralelizar é a SESSÃO. Cada canal aqui tem o próprio
//   SigaaSession, o próprio JSESSIONID e a própria view; entre eles não há
//   estado compartilhado no servidor. Isso é o mesmo que a JanelaTurma já faz
//   desde que passou a abrir uma sessão separada da sincronização.
//
//   O preço é um login por canal. Para dois PDFs de 200 KB isso é perda pura;
//   para uma turma com dez materiais e um vídeo de 40 MB é o que transforma
//   "deixa baixando e volta depois" em alguns segundos. Quem chama escolhe o
//   número de canais — `canaisRecomendados()` embute essa conta.
//
// LIMITE HERDADO: cada arquivo passa inteiro pela memória antes de ir ao disco
// (ver core/sync/Materiais.h). N canais significam N arquivos na memória ao
// mesmo tempo. É mais uma razão para o teto ser baixo.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/model/Models.h"

namespace sigaa::sync {

// O que já foi baixado desta turma, segundo um manifesto ao lado dos arquivos.
//
// Por que um arquivo na pasta e não uma tabela no banco: a pasta é o que o
// aluno mexe. Ele copia para o pendrive, renomeia, apaga. Um manifesto que
// viaja junto continua verdadeiro depois disso; uma linha no SQLite viraria
// mentira em silêncio — e "já está offline" mentiroso é pior que não ter o
// recurso, porque leva a pessoa a fechar o app achando que tem o arquivo.
//
// Por isso toda leitura confere se o arquivo ainda existe no disco.
class CacheLocal {
public:
    explicit CacheLocal(std::string diretorio);

    // Caminho absoluto do arquivo já baixado, ou "" se não houver.
    std::string caminho(const std::string& idArquivo) const;
    bool temNoDisco(const std::string& idArquivo) const {
        return !caminho(idArquivo).empty();
    }

    // Registra e grava o manifesto. `caminho` é absoluto, em UTF-8.
    void registrar(const std::string& idArquivo, const std::string& caminho);

    // Quantos ids do manifesto ainda têm arquivo no disco.
    int quantosNoDisco() const;

    // Nome do manifesto dentro da pasta da turma.
    static const char* nomeArquivo();

private:
    std::string diretorio_;
    std::vector<std::pair<std::string, std::string>> itens_;   // id -> nome
};

// Onde o material de uma turma mora: <base>/<nome da turma saneado>.
//
// UMA definição, usada pelo sync e pela janela da turma. Se as duas
// calculassem o caminho por conta própria e discordassem num acento, o sync
// baixaria para uma pasta e a janela procuraria noutra — o app mostraria
// "ainda não baixado" para arquivos que estão no disco, e baixaria tudo de
// novo. Falha silenciosa e cara.
std::string pastaDaTurma(const std::string& base, const std::string& nomeTurma);

struct PedidoDownload {
    std::string idArquivo;
    std::string nome;      // só para a mensagem de progresso
};

struct ItemBaixado {
    std::string idArquivo;
    std::string caminho;        // vazio em falha
    std::string erro;           // vazio em sucesso
    bool reaproveitado{false};  // já estava no disco; não houve rede

    bool ok() const { return !caminho.empty(); }
};

class Baixador {
public:
    // Uma sessão já aberta na aba Arquivos de uma turma. A interface existe
    // para que a fila seja testável sem rede — a concorrência é a parte que
    // erra em silêncio, e ela não pode depender do SIGAA estar no ar.
    class Canal {
    public:
        virtual ~Canal() = default;
        virtual std::optional<std::string> baixar(const std::string& idArquivo,
                                                  const std::string& diretorio,
                                                  std::string* erro) = 0;
    };

    // Chamada UMA VEZ POR CANAL, já dentro da thread do canal. Devolve nullptr
    // e preenche `erro` se o login/navegação falhar.
    using AbrirCanal = std::function<std::unique_ptr<Canal>(std::string* erro)>;

    // (prontos, total). Chamada de threads diferentes, mas serializada por um
    // mutex interno — quem implementa não precisa se preocupar com corrida,
    // mas TAMBÉM não pode tocar em UI direto (use fila de eventos).
    using Progresso = std::function<void(int prontos, int total)>;

    Baixador(AbrirCanal abrir, std::string diretorio, int canais);

    // Espera entre a abertura de um canal e a do próximo. N logins disparados
    // no mesmo milissegundo parecem ataque para qualquer WAF, e o custo é
    // irrelevante ao lado do próprio login. Existe como ajuste para que o
    // teste de concorrência não dependa de dormir de verdade.
    void setAtrasoEntreCanais(std::chrono::milliseconds ms) { atrasoCanal_ = ms; }

    // Ignora o que já está no disco e baixa de novo.
    //
    // Existe porque a chave é o `id` do SIGAA, e o id não muda quando o
    // professor SUBSTITUI o conteúdo mantendo o mesmo material — se é que ele
    // consegue; nunca vimos acontecer. É a única brecha do cache, e a resposta
    // certa para ela é um botão, não desligar o cache para todo mundo: o custo
    // de reabrir um arquivo velho é um clique, e o de rebaixar tudo sempre é
    // cada abertura do app puxando a turma inteira de novo.
    void setForcar(bool sim) { forcar_ = sim; }

    // Bloqueia até terminar. O resultado sai NA ORDEM DOS PEDIDOS, não na de
    // conclusão — a lista é mostrada para uma pessoa, e ordem de conclusão
    // muda a cada execução.
    //
    // Pedidos já presentes no cache local não geram rede: voltam com
    // `reaproveitado=true` e o caminho que já existia.
    std::vector<ItemBaixado> baixar(const std::vector<PedidoDownload>& pedidos,
                                    const Progresso& progresso = {});

private:
    AbrirCanal abrir_;
    std::string diretorio_;
    int canais_;
    std::chrono::milliseconds atrasoCanal_{400};
    bool forcar_{false};
};

// Quantos canais valem a pena para `n` arquivos.
//
// Um canal por arquivo seria absurdo: o login custa mais que o download de um
// PDF de aula. A conta é conservadora de propósito — o ganho real aparece em
// arquivo grande, e o custo (logins simultâneos na conta do aluno) é o tipo de
// coisa que a universidade pode achar suspeita.
int canaisRecomendados(int n);

// A fábrica de verdade: login + entrar na turma + abrir a aba Arquivos.
// `baseUrl` vazio usa a instituição selecionada.
Baixador::AbrirCanal canalSigaa(std::string baseUrl, std::string login,
                                std::string senha, Turma turma);

} // namespace sigaa::sync
