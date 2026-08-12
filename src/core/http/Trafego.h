#pragma once
// Registro de TODA requisição que sai para o SIGAA.
//
// POR QUE ISTO EXISTE, e por que não é só um log de depuração: o risco número
// um deste app não é mostrar um prazo errado, é **bater no servidor da
// universidade em looping** com a conta de um aluno de verdade. As três
// invariantes do SigaaSession (uma requisição por vez, intervalo mínimo, teto
// de tentativas de login) protegem contra isso — mas nenhuma delas é visível de
// fora. Um bug num laço de navegação, ou dois ciclos disparando juntos,
// aparecia como "o app está lento", nunca como "estou fazendo 40 requisições
// por minuto na conta do usuário".
//
// Este módulo torna esse número olhável, na hora, sem depurador: a UI abre uma
// janela com o tráfego ao vivo e o CLI imprime com `--log-http`. É a única
// forma honesta de responder "por que o SIGAA me bloqueou?".
//
// Não fica em core/util porque é conhecimento de HTTP: o que se registra é
// método, URL, status, tentativa e quanto tempo o rate limit segurou.
//
// SEM SEGREDO AQUI, POR REGRA: nunca entra corpo de requisição (o POST de login
// tem a senha), nunca entra cookie (o JSESSIONID é a sessão inteira) e a URL é
// guardada como veio — o SIGAA não põe credencial em query string, e se um dia
// puser, o filtro entra aqui. A janela é copiável para colar num issue, e é
// isso que torna a regra obrigatória em vez de recomendável.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sigaa::http {

struct EventoRequisicao {
    std::int64_t sequencia{0};   // 1, 2, 3... desde que o processo subiu
    std::chrono::system_clock::time_point quando{};

    std::string metodo;    // "GET" ou "POST"
    std::string url;
    std::string urlFinal;  // depois dos redirects; vazio se não mudou

    long status{0};
    std::size_t bytes{0};
    std::string erro;      // vazio em sucesso

    int tentativas{1};     // > 1 = houve retry de erro transitório
    int esperaMs{0};       // quanto o rate limit segurou antes de sair
    int duracaoMs{0};      // tempo na rede

    // O que voltou, classificado pelo próprio SigaaSession. É o campo que
    // denuncia sessão expirada no meio de um crawl: uma sequência de
    // TurmaVirtual que vira Login explica um sync "vazio" sem erro nenhum.
    std::string pagina;

    // Uma linha, para o terminal e para colar em issue.
    std::string linha() const;
};

// Chamado A PARTIR DA THREAD QUE FEZ A REQUISIÇÃO, e não da thread de UI: quem
// se inscreve é responsável por atravessar para a sua (na UI, sinal com
// conexão em fila). Registrar de dentro do observador é proibido — daria
// recursão sob o mesmo mutex.
using Observador = std::function<void(const EventoRequisicao&)>;

// Devolve um id para remover depois. Remover é obrigatório antes de destruir
// quem capturou `this` no observador.
int registrarObservador(Observador o);
void removerObservador(int id);

// Últimos eventos, do mais antigo para o mais novo. O histórico é um anel
// limitado: a janela de diagnóstico precisa abrir mostrando o que já passou,
// mas um app aberto o dia todo não pode crescer sem fim.
std::vector<EventoRequisicao> historico();
std::int64_t totalRequisicoes();
void limparHistorico();

// Chamado pelo SigaaSession. Numera, guarda no anel e avisa os observadores.
void registrar(EventoRequisicao ev);

} // namespace sigaa::http
