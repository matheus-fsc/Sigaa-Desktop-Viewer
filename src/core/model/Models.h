#pragma once
// Modelo de domínio. Structs puras — sem lógica de rede, sem UI.
// Nomes de domínio ficam em português porque é o vocabulário do SIGAA.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace sigaa {

// Data/hora ingênua (sem timezone). O SIGAA opera em horário de Brasília e
// nunca envia offset, então guardamos o que ele diz e resolvemos no display.
struct DateTime {
    int year{}, month{}, day{};
    int hour{}, minute{};
    bool hasTime{false};

    bool valid() const { return year > 0 && month > 0 && day > 0; }

    // ISO-8601 sem timezone, ou "" se inválida.
    std::string toIso() const {
        if (!valid()) return {};
        char buf[32];
        if (hasTime) {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d", year, month, day,
                          hour, minute);
        } else {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
        }
        return buf;
    }

    // Ordenação cronológica — usada para montar a timeline.
    bool operator<(const DateTime& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        if (day != o.day) return day < o.day;
        if (hour != o.hour) return hour < o.hour;
        return minute < o.minute;
    }
};

enum class StatusAtividade {
    Pendente,    // sem ícone específico
    NaSemana,    // prova_semana.png
    Concluida,   // check.png
    Desconhecido
};

// Uma turma. Os dados vêm de DUAS telas (ver docs/RECON.md §1.5 e §1.10):
//   - catálogo inline (#formTurma): codigo, nome, cargaHoraria, periodo, frontEndId
//   - tabela do portal:             local, horario
struct Turma {
    std::string idTurma;        // "88094"  — usado por atualizações e chat
    std::string frontEndId;     // hash de 40 hex — ÚNICO jeito de entrar na turma virtual
    std::string codigo;         // "EPRIB001"
    std::string nome;           // "ADMINISTRAÇÃO"
    std::string periodo;        // "2026.2"
    int cargaHoraria{0};        // 32, 64...
    std::string local;          // "Anexo 3 - Sala 19"
    std::string horario;        // "6T34" (código SIGAA)
};

struct Atividade {
    std::string atividadeId;    // "130174755"
    std::string idTurma;        // "89151"      -> chave: (atividadeId, idTurma)
    std::string turmaNome;
    std::string periodo;
    std::string tipo;           // "Tarefa", "Prova", ...
    std::string titulo;
    DateTime prazo;
    StatusAtividade status{StatusAtividade::Desconhecido};
};

struct Atualizacao {
    std::string hash;           // sha1(data|idTurma|texto) — não há ID no HTML
    std::string idTurma;
    std::string turmaNome;
    DateTime data;              // só a data, sem hora
    std::string texto;          // "Nova tarefa: ...", "Novo Arquivo: ..."
};

// Um item publicado DENTRO de um tópico de aula (docs/RECON.md §1.6.2).
//
// A chave é o mesmo `id` avulso do jsfcljs que a aba Arquivos usa (§1.6.1).
// É isso que permite casar um material de tópico com o `ArquivoTurma` da aba e
// saber que ele é baixável — em vez de adivinhar pelo id do componente, que é
// posicional e muda quando a instituição recompila o JSP.
struct MaterialTopico {
    std::string id;          // "130160443" — parâmetro `id` avulso do jsfcljs
    // Nome do ícone que o SIGAA desenha ao lado: "tarefa", "arquivo",
    // "video"... É o único sinal de tipo que a página oferece de forma estável.
    // Vazio quando não há ícone; não inventamos um padrão.
    std::string tipo;
    std::string titulo;
    std::string descricao;   // "Inicia em 04/08/2026 às 0h 0 e finaliza em ..."
};

struct TopicoAula {
    std::string idTurma;
    std::string titulo;
    DateTime inicio;
    DateTime fim;
    std::string conteudo;
    std::vector<MaterialTopico> materiais;
};

// Origem da avaliação — importa porque as duas fontes divergem por professor
// e o merge precisa saber em quem confiar (ver docs/RECON.md §1.6).
enum class FonteAvaliacao {
    PainelAvaliacoes,   // div.rich-stglpanel "Avaliações" — alta confiança
    TopicoAula          // regex sobre título de tópico — fallback
};

struct Avaliacao {
    std::string idTurma;
    std::string turmaNome;
    std::string descricao;      // "1ª Avaliação"
    DateTime quando;
    // Texto cru do horário. Formato VARIA: "6T34" (código SIGAA) ou "15:45".
    std::string horarioBruto;
    FonteAvaliacao fonte{FonteAvaliacao::PainelAvaliacoes};
};

// Um material publicado pelo professor na aba "Arquivos" da Turma Virtual.
//
// A chave é `idArquivo`, o parâmetro `id` avulso do jsfcljs (RECON §1.6.1). O
// id do COMPONENTE que aparece ao lado dele é posicional e muda quando a
// instituição recompila a página — guardar aquele daria um botão de download
// que aponta para o arquivo errado depois de um upgrade do SIGAA.
struct ArquivoTurma {
    std::string idTurma;
    std::string turmaNome;
    std::string idArquivo;      // "526919"
    std::string titulo;         // "LIVRO: Algoritmos - Thomas H. Cormen"
    std::string descricao;      // quase sempre vazia na prática
    std::string topico;         // "Materiais auxiliares para Estudo"
};

// Quem está na turma: o professor e os colegas (aba "Participantes").
//
// O SIGAA desenha os dois grupos em `table.participantes` separadas, com
// markup DIFERENTE — docente traz Departamento/Formação e o nome dentro de um
// link para a página pública; discente traz Curso/Matrícula e o nome como
// texto solto. Um struct só, com os campos de cada papel vazios no outro, em
// vez de dois tipos: a tela mostra uma lista única, e duplicar o tipo
// espalharia o `if` por toda a UI em troca de nada.
enum class PapelParticipante { Docente, Discente };

// CPF e idPessoa NÃO entram aqui, de propósito. A página traz os dois para cada
// participante — `Mensagem.show(1, '<CPF>', ...)` e `'idPessoa':194699` — e o
// app não tem nenhuma tela que precise deles. Extrair identificador de terceiro
// só porque ele está no HTML é aumentar a superfície de vazamento em troca de
// campo que ninguém lê; a chave (idTurma, nome) resolve o que precisamos.
struct Participante {
    std::string idTurma;
    std::string turmaNome;
    PapelParticipante papel{PapelParticipante::Discente};
    std::string nome;
    std::string email;

    // Só discente.
    std::string matricula;      // "2022004556"
    std::string curso;          // "ENGENHARIA DE COMPUTAÇÃO/ICT"

    // Só docente.
    std::string departamento;   // "INSTITUTO DE CIÊNCIAS TECNOLÓGICAS"
    std::string formacao;       // "DOUTORADO"

    // NÃO HÁ CAMPO DE FOTO, e a ausência é deliberada.
    //
    // O app já guardou retrato de colega em disco (`core/sync/Fotos.h`,
    // removido em 18/09/2026). A imagem de uma pessoa é dado pessoal dela, e
    // ela não escolheu nos dar cópia — cadastrou a foto no SIGAA, para o
    // SIGAA. Espalhar isso pela máquina de cada colega de turma é uma decisão
    // que nenhum dos 31 tomou.
    //
    // O ganho era reconhecer alguém de relance na lista. As INICIAIS em um
    // quadrado colorido resolvem o mesmo problema (ver `avatarDe` em
    // ui/Modelos.cpp) sem copiar o rosto de ninguém, e ainda funcionam para
    // quem não cadastrou retrato — 13 dos 32 na captura de rede.
    //
    // O parser também não extrai mais a URL: ela carrega um `key` que é token
    // de acesso ao retrato de um terceiro, e não coletar é mais forte que
    // coletar e não usar.
};

// Snapshot completo de um ciclo de coleta.
// Um dia no diário de classe do professor.
enum class SituacaoDia {
    Presente,
    Falta,           // o SIGAA escreve "N Falta(s)"; N está em `faltas`
    NaoRegistrada,   // o professor não lançou este dia
};

struct DiaFrequencia {
    DateTime data;
    SituacaoDia situacao{SituacaoDia::NaoRegistrada};
    // Aulas perdidas nesse dia. Zero quando presente ou não registrada.
    //
    // NÃO é "1 por dia": um encontro de dois horários seguidos lança 2 faltas,
    // e é assim que o SIGAA conta — a unidade da frequência é a HORA-AULA, não
    // o dia. A própria página diz: "a porcentagem é calculada levando em
    // consideração que uma aula dura 60 minutos".
    int faltas{0};
};

// O mapa de frequência de uma turma — /sigaa/ava/FrequenciaAluno/mapa.jsf
//
// OS TOTAIS VÊM DO SIGAA, não da soma das linhas, e isso é deliberado: a
// tabela diz "Presente" sem dizer de quantas aulas aquele encontro foi feito.
// Somar as linhas exigiria adivinhar isso (dividir o total pelo número de
// encontros dá uma média que quebra na turma com um encontro de 1 aula). O
// rodapé da página já traz os três números exatos.
//
// PODE NÃO EXISTIR: se o professor nunca abriu o diário eletrônico, não há
// tabela nem rodapé. `temDados` distingue isso de "zero faltas" — e a
// diferença importa, porque "você não faltou" e "ninguém registrou nada" levam
// o aluno a decisões opostas.
struct Frequencia {
    std::string idTurma;
    std::string turmaNome;

    int presencas{0};          // "Presenças Registradas: 18"
    int aulasComRegistro{0};   // "Número de Aulas com Registro de Frequência: 24"
    int aulasPelaCH{0};        // "Número de Aulas definidas pela CH do Componente: 64"

    bool temDados{false};

    std::vector<DiaFrequencia> dias;

    // Aulas perdidas. A subtração é a do próprio SIGAA: das aulas que ele
    // registrou, as que não foram presença.
    int faltas() const {
        const int f = aulasComRegistro - presencas;
        return f > 0 ? f : 0;
    }

    // Quantas aulas dá para perder antes de reprovar por falta.
    //
    // 25% da carga horária, que é o complemento da regra que a própria página
    // enuncia: "aprovado [...] se tiver presença em um número de aulas igual
    // ou superior a 75.0% da carga horária do componente curricular". Em 64
    // aulas dá 16; em 32, dá 8.
    //
    // Calculado sobre `aulasPelaCH` — a carga horária do componente — e não
    // sobre o que o professor já lançou: o limite é do semestre inteiro e não
    // encolhe porque o diário está atrasado.
    int limiteFaltas() const { return aulasPelaCH / 4; }

    // Já reprovado por falta. `>=` e não `>`: com 16 de 64, a presença caiu
    // para 75% — e a regra exige "igual ou superior a 75%" de PRESENÇA, então
    // o limite ainda passa. É a falta seguinte que reprova.
    bool reprovado() const { return limiteFaltas() > 0 && faltas() > limiteFaltas(); }
};

struct Snapshot {
    std::vector<Turma> turmas;
    std::vector<Atividade> atividades;
    std::vector<Atualizacao> atualizacoes;
    std::vector<TopicoAula> topicos;
    std::vector<Avaliacao> avaliacoes;
    // Material publicado nas turmas. Só é preenchido quando a coleta entra nas
    // turmas (`incluirTurmas`): o portal não sabe que estes arquivos existem.
    std::vector<ArquivoTurma> arquivos;
    // Colegas e professores das turmas. Como `arquivos`, só vem quando a coleta
    // entra nas turmas.
    std::vector<Participante> participantes;
    // Uma por turma, quando a coleta entra nas turmas E o professor lançou
    // alguma frequência. Turma sem diário aberto simplesmente não aparece.
    std::vector<Frequencia> frequencias;
    std::optional<int> minutosSessaoRestantes;  // lido do header do SIGAA
};

} // namespace sigaa
