#include "core/store/Database.h"

#include <sqlite3.h>

#include <map>

namespace sigaa::store {
namespace {

constexpr const char* kEsquema = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS turma (
  id_turma       TEXT PRIMARY KEY,
  front_end_id   TEXT,
  codigo         TEXT,
  nome           TEXT,
  periodo        TEXT,
  carga_horaria  INTEGER,
  local          TEXT,
  horario        TEXT,
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER
);

-- chave composta: o mesmo atividade_id pode existir em turmas diferentes
CREATE TABLE IF NOT EXISTS atividade (
  atividade_id   TEXT NOT NULL,
  id_turma       TEXT NOT NULL,
  turma_nome     TEXT,
  tipo           TEXT,
  titulo         TEXT,
  prazo          TEXT,          -- ISO-8601 sem timezone
  status         TEXT,
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER,
  lido_em        INTEGER,
  PRIMARY KEY (atividade_id, id_turma)
);

-- o SIGAA não dá ID para atualização; a chave é o hash (data|turma|texto)
CREATE TABLE IF NOT EXISTS atualizacao (
  hash           TEXT PRIMARY KEY,
  id_turma       TEXT,
  turma_nome     TEXT,
  data           TEXT,
  texto          TEXT,
  primeiro_visto INTEGER,
  lido_em        INTEGER
);

-- Chave (id_turma, descricao): a descricao e estavel por turma ("1ª Avaliação",
-- "Prova N1"), enquanto a DATA e justamente o que pode mudar — e mudanca de
-- data de prova e das informacoes mais importantes que o app pode dar.
CREATE TABLE IF NOT EXISTS avaliacao (
  id_turma       TEXT NOT NULL,
  descricao      TEXT NOT NULL,
  turma_nome     TEXT,
  quando         TEXT,          -- ISO-8601
  horario_bruto  TEXT,          -- "6T34" ou "15:45"
  fonte          TEXT,          -- painel | topico
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER,
  PRIMARY KEY (id_turma, descricao)
);

-- Mapa de frequencia por turma (/sigaa/ava/FrequenciaAluno/mapa.jsf).
--
-- Os TOTAIS sao os que o SIGAA calculou, nao uma soma nossa: a tabela da
-- pagina escreve "Presente" sem dizer de quantas aulas foi feito o encontro.
-- Ver core/parse/FrequenciaParser.h.
--
-- `tem_dados` distingue "o professor nao lancou nada" de "zero faltas" — e a
-- diferenca importa, porque as duas levam o aluno a decisoes opostas.
CREATE TABLE IF NOT EXISTS frequencia (
  id_turma            TEXT PRIMARY KEY,
  turma_nome          TEXT,
  presencas           INTEGER NOT NULL DEFAULT 0,
  aulas_com_registro  INTEGER NOT NULL DEFAULT 0,
  aulas_pela_ch       INTEGER NOT NULL DEFAULT 0,
  tem_dados           INTEGER NOT NULL DEFAULT 0,
  ultimo_visto        INTEGER
);

-- Um dia do diario. Chave (turma, data): o professor corrige lancamento, e a
-- mesma data tem de virar UPDATE em vez de uma segunda linha.
CREATE TABLE IF NOT EXISTS frequencia_dia (
  id_turma   TEXT NOT NULL,
  data       TEXT NOT NULL,     -- ISO-8601
  situacao   TEXT NOT NULL,     -- presente | falta | nao_registrada
  faltas     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (id_turma, data)
);

-- Presenca que o ALUNO registrou nos dias que o professor deixou em branco
-- (core/frequencia/Presenca.h).
--
-- Tabela separada de `frequencia_dia` pelo mesmo motivo de `ajuste_avaliacao`:
-- a linha do SIGAA e reescrita a cada sync, e a marcacao guardada la seria
-- apagada pelo ciclo seguinte — perdendo justamente o registro que o aluno
-- fez para se proteger.
--
-- `ativo = 0` e marcacao que o professor atropelou. Fica: e a evidencia dele
-- para a conversa que vem.
CREATE TABLE IF NOT EXISTS marcacao_presenca (
  id_turma       TEXT NOT NULL,
  data           TEXT NOT NULL,     -- ISO-8601
  situacao       TEXT NOT NULL,     -- presente | falta
  faltas         INTEGER NOT NULL DEFAULT 0,
  nota           TEXT,
  ativo          INTEGER NOT NULL DEFAULT 1,
  situacao_sigaa TEXT,              -- o que o SIGAA dizia quando ele marcou
  editado_em     INTEGER,
  PRIMARY KEY (id_turma, data)
);

-- Historico das marcacoes, das duas origens. Sem chave de negocio: cada linha
-- e um fato datado, e fato nao tem upsert.
CREATE TABLE IF NOT EXISTS mudanca_presenca (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  id_turma   TEXT NOT NULL,
  turma_nome TEXT,
  data       TEXT NOT NULL,
  tipo       TEXT NOT NULL,
  de         TEXT,
  para       TEXT,
  nota       TEXT,
  quando     INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_mudanca_presenca
  ON mudanca_presenca(id_turma, data, quando);

-- Correcoes do aluno sobre as datas de prova (core/avaliacao/Ajustes.h).
--
-- TABELA SEPARADA, e nao colunas em `avaliacao`: a linha de `avaliacao` e
-- sobrescrita a cada sync pelo upsert, entao uma correcao guardada la seria
-- apagada pelo proximo ciclo — em silencio, que e o pior jeito de perder o
-- dado que o aluno digitou porque o professor errou.
--
-- `ativo = 0` nao e lixo: e um ajuste que o SIGAA atropelou, guardado para o
-- historico e para o botao "restaurar minha data".
CREATE TABLE IF NOT EXISTS ajuste_avaliacao (
  id_turma          TEXT NOT NULL,
  descricao         TEXT NOT NULL,
  turma_nome        TEXT,
  quando            TEXT,        -- ISO-8601: a data que passa a valer
  horario_bruto     TEXT,
  nota              TEXT,
  criada_pelo_aluno INTEGER NOT NULL DEFAULT 0,
  confirmada        INTEGER NOT NULL DEFAULT 0,
  ativo             INTEGER NOT NULL DEFAULT 1,
  -- O que o SIGAA dizia quando o aluno corrigiu. Sem isto nao da para
  -- distinguir "eu corrigi o professor" de "o professor acabou de me corrigir".
  quando_sigaa      TEXT,
  editado_em        INTEGER,
  PRIMARY KEY (id_turma, descricao)
);

-- Historico de mudancas de data de prova, das duas origens.
--
-- Sem chave de negocio: cada linha e um fato datado, e fato nao tem upsert.
-- E por isso que nao ha PRIMARY KEY natural aqui — duas correcoes da mesma
-- prova sao duas linhas, e e exatamente isso que o aluno quer ver.
CREATE TABLE IF NOT EXISTS mudanca_avaliacao (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  id_turma       TEXT NOT NULL,
  turma_nome     TEXT,
  descricao      TEXT NOT NULL,
  tipo           TEXT NOT NULL,   -- ver avaliacao::toString(TipoMudanca)
  de             TEXT,
  para           TEXT,
  nota           TEXT,
  quando         INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_mudanca_prova
  ON mudanca_avaliacao(id_turma, descricao, quando);

-- Material publicado pelo professor. A chave e o id_arquivo do SIGAA
-- (RECON §1.6.1) e NAO o titulo: titulo e texto livre, e o professor renomeia
-- "Aula 3" para "Aula 03 - revisada" sem publicar nada. Comparar por titulo
-- faria cada renomeacao virar "material novo" — o alarme falso que ensina
-- alguem a ignorar as notificacoes.
CREATE TABLE IF NOT EXISTS arquivo (
  id_arquivo     TEXT PRIMARY KEY,
  id_turma       TEXT NOT NULL,
  turma_nome     TEXT,
  titulo         TEXT,
  descricao      TEXT,
  topico         TEXT,
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER
);

-- Topicos de aula: a linha do tempo da turma. Ficam no banco porque a tela
-- inicial mostra "a aula de hoje" toda vez que o app abre, e coletar topico
-- exige entrar em cada turma (~25 s). Sem persistir, a tela ficaria vazia ate
-- o proximo ciclo completo terminar.
--
-- Chave (id_turma, titulo, inicio): o SIGAA nao da id para topico. Titulo
-- sozinho colide — "Revisao" aparece varias vezes no semestre.
CREATE TABLE IF NOT EXISTS topico (
  id_turma       TEXT NOT NULL,
  titulo         TEXT NOT NULL,
  inicio         TEXT NOT NULL,   -- ISO-8601, "" quando o professor nao datou
  fim            TEXT,
  conteudo       TEXT,
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER,
  PRIMARY KEY (id_turma, titulo, inicio)
);

-- Quem esta na turma (aba Participantes).
--
-- DADO DE TERCEIRO, e o unico do banco que nao e do proprio aluno: sao os
-- colegas, com nome, curso, matricula e e-mail institucional. Por isso o que
-- entra aqui e o MINIMO que a tela mostra — CPF e idPessoa, que a pagina
-- tambem traz para cada um, nao sao nem extraidos (ver core/model/Models.h).
--
-- Chave (id_turma, nome): o SIGAA nao da id publico ao participante, e a
-- matricula falta no docente. Homonimo na mesma turma colapsaria em uma linha,
-- o que e preferivel a versionar identificador de terceiro para desempatar.
CREATE TABLE IF NOT EXISTS participante (
  id_turma       TEXT NOT NULL,
  nome           TEXT NOT NULL,
  papel          TEXT,          -- docente | discente
  turma_nome     TEXT,
  email          TEXT,
  matricula      TEXT,
  curso          TEXT,
  departamento   TEXT,
  formacao       TEXT,
  primeiro_visto INTEGER,
  ultimo_visto   INTEGER,
  PRIMARY KEY (id_turma, nome)
);

CREATE TABLE IF NOT EXISTS meta (
  chave TEXT PRIMARY KEY,
  valor TEXT
);

CREATE INDEX IF NOT EXISTS idx_atividade_prazo ON atividade(prazo);
CREATE INDEX IF NOT EXISTS idx_topico_inicio ON topico(inicio);
CREATE INDEX IF NOT EXISTS idx_arquivo_turma ON arquivo(id_turma);
CREATE INDEX IF NOT EXISTS idx_participante_turma ON participante(id_turma);
)SQL";

std::string txt(sqlite3_stmt* st, int col) {
    const unsigned char* p = sqlite3_column_text(st, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

DateTime deIso(const std::string& s) {
    DateTime d;
    if (s.size() < 10) return d;
    d.year = std::atoi(s.substr(0, 4).c_str());
    d.month = std::atoi(s.substr(5, 2).c_str());
    d.day = std::atoi(s.substr(8, 2).c_str());
    if (s.size() >= 16 && s[10] == 'T') {
        d.hour = std::atoi(s.substr(11, 2).c_str());
        d.minute = std::atoi(s.substr(14, 2).c_str());
        d.hasTime = true;
    }
    return d;
}

const char* situacaoParaTexto(SituacaoDia s) {
    switch (s) {
        case SituacaoDia::Presente:      return "presente";
        case SituacaoDia::Falta:         return "falta";
        case SituacaoDia::NaoRegistrada: return "nao_registrada";
    }
    return "nao_registrada";
}

SituacaoDia textoParaSituacao(const std::string& s) {
    if (s == "presente") return SituacaoDia::Presente;
    if (s == "falta") return SituacaoDia::Falta;
    // Desconhecido cai em "não registrada", nunca em "presente": inventar uma
    // presença esconderia uma falta.
    return SituacaoDia::NaoRegistrada;
}

const char* statusParaTexto(StatusAtividade s) {
    switch (s) {
        case StatusAtividade::Pendente:  return "pendente";
        case StatusAtividade::NaSemana:  return "na_semana";
        case StatusAtividade::Concluida: return "concluida";
        default:                         return "desconhecido";
    }
}

StatusAtividade textoParaStatus(const std::string& s) {
    if (s == "pendente") return StatusAtividade::Pendente;
    if (s == "na_semana") return StatusAtividade::NaSemana;
    if (s == "concluida") return StatusAtividade::Concluida;
    return StatusAtividade::Desconhecido;
}

} // namespace

// ---------------------------------------------------------------------------

struct Database::Impl {
    sqlite3* db{nullptr};
    std::string erro;

    bool exec(const char* sql) {
        char* msg = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
            erro = msg ? msg : "erro sqlite desconhecido";
            sqlite3_free(msg);
            return false;
        }
        return true;
    }
};

Database::Database(const std::string& caminho) : impl_(std::make_unique<Impl>()) {
    if (sqlite3_open(caminho.c_str(), &impl_->db) != SQLITE_OK) {
        impl_->erro = impl_->db ? sqlite3_errmsg(impl_->db) : "falha ao abrir o banco";
        if (impl_->db) {
            sqlite3_close(impl_->db);
            impl_->db = nullptr;
        }
    }
}

Database::~Database() {
    if (impl_ && impl_->db) sqlite3_close(impl_->db);
}

bool Database::aberto() const { return impl_->db != nullptr; }
const std::string& Database::erro() const { return impl_->erro; }

bool Database::migrar() {
    if (!aberto()) return false;
    return impl_->exec(kEsquema);
}

// ---------------------------------------------------------------------------

Snapshot Database::carregarUltimo() {
    Snapshot s;
    if (!aberto()) return s;

    sqlite3_stmt* st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, front_end_id, codigo, nome, periodo, carga_horaria,"
            " local, horario FROM turma", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Turma t;
            t.idTurma = txt(st, 0);
            t.frontEndId = txt(st, 1);
            t.codigo = txt(st, 2);
            t.nome = txt(st, 3);
            t.periodo = txt(st, 4);
            t.cargaHoraria = sqlite3_column_int(st, 5);
            t.local = txt(st, 6);
            t.horario = txt(st, 7);
            s.turmas.push_back(std::move(t));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT atividade_id, id_turma, turma_nome, tipo, titulo, prazo, status"
            " FROM atividade", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Atividade a;
            a.atividadeId = txt(st, 0);
            a.idTurma = txt(st, 1);
            a.turmaNome = txt(st, 2);
            a.tipo = txt(st, 3);
            a.titulo = txt(st, 4);
            a.prazo = deIso(txt(st, 5));
            a.status = textoParaStatus(txt(st, 6));
            s.atividades.push_back(std::move(a));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT hash, id_turma, turma_nome, data, texto FROM atualizacao",
            -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Atualizacao u;
            u.hash = txt(st, 0);
            u.idTurma = txt(st, 1);
            u.turmaNome = txt(st, 2);
            u.data = deIso(txt(st, 3));
            u.texto = txt(st, 4);
            s.atualizacoes.push_back(std::move(u));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, descricao, turma_nome, quando, horario_bruto, fonte"
            " FROM avaliacao", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Avaliacao a;
            a.idTurma = txt(st, 0);
            a.descricao = txt(st, 1);
            a.turmaNome = txt(st, 2);
            a.quando = deIso(txt(st, 3));
            a.horarioBruto = txt(st, 4);
            a.fonte = (txt(st, 5) == "topico") ? FonteAvaliacao::TopicoAula
                                               : FonteAvaliacao::PainelAvaliacoes;
            s.avaliacoes.push_back(std::move(a));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_arquivo, id_turma, turma_nome, titulo, descricao, topico"
            " FROM arquivo", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            ArquivoTurma a;
            a.idArquivo = txt(st, 0);
            a.idTurma = txt(st, 1);
            a.turmaNome = txt(st, 2);
            a.titulo = txt(st, 3);
            a.descricao = txt(st, 4);
            a.topico = txt(st, 5);
            s.arquivos.push_back(std::move(a));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // Docente primeiro, depois os discentes em ordem alfabetica — a mesma
    // ordem em que o SIGAA desenha a pagina. Ordenar aqui, e nao na tela,
    // porque o .ics e o relatorio HTML leem o mesmo Snapshot.
    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, nome, papel, turma_nome, email, matricula, curso,"
            " departamento, formacao FROM participante"
            // papel DESC porque 'docente' > 'discente' em ordem alfabetica, e
            // e o professor que a tela precisa mostrar no topo.
            " ORDER BY id_turma, papel DESC, nome", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Participante p;
            p.idTurma = txt(st, 0);
            p.nome = txt(st, 1);
            p.papel = std::string(txt(st, 2)) == "docente" ? PapelParticipante::Docente
                                                           : PapelParticipante::Discente;
            p.turmaNome = txt(st, 3);
            p.email = txt(st, 4);
            p.matricula = txt(st, 5);
            p.curso = txt(st, 6);
            p.departamento = txt(st, 7);
            p.formacao = txt(st, 8);
            s.participantes.push_back(std::move(p));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // Ordenado pela data da aula: quem le isto quer a linha do tempo, e
    // ordenar depois em cada chamador seria repetir a mesma regra em N lugares.
    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, titulo, inicio, fim, conteudo FROM topico"
            " ORDER BY inicio", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            TopicoAula t;
            t.idTurma = txt(st, 0);
            t.titulo = txt(st, 1);
            t.inicio = deIso(txt(st, 2));
            t.fim = deIso(txt(st, 3));
            t.conteudo = txt(st, 4);
            // `materiais` NAO volta do banco: o que e arquivo ja esta em
            // `s.arquivos`, com a mesma chave, e duplicar a lista daria duas
            // fontes para a mesma verdade.
            s.topicos.push_back(std::move(t));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- frequência ---
    // Os dias entram num índice por turma primeiro, para não fazer uma
    // consulta por turma dentro do laço.
    std::map<std::string, std::vector<DiaFrequencia>> diasPorTurma;
    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, data, situacao, faltas FROM frequencia_dia"
            " ORDER BY data", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            DiaFrequencia d;
            d.data = deIso(txt(st, 1));
            d.situacao = textoParaSituacao(txt(st, 2));
            d.faltas = sqlite3_column_int(st, 3);
            diasPorTurma[txt(st, 0)].push_back(std::move(d));
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, turma_nome, presencas, aulas_com_registro,"
            " aulas_pela_ch, tem_dados FROM frequencia", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Frequencia f;
            f.idTurma = txt(st, 0);
            f.turmaNome = txt(st, 1);
            f.presencas = sqlite3_column_int(st, 2);
            f.aulasComRegistro = sqlite3_column_int(st, 3);
            f.aulasPelaCH = sqlite3_column_int(st, 4);
            f.temDados = sqlite3_column_int(st, 5) != 0;
            if (auto it = diasPorTurma.find(f.idTurma); it != diasPorTurma.end()) {
                f.dias = std::move(it->second);
            }
            s.frequencias.push_back(std::move(f));
        }
    }
    sqlite3_finalize(st);

    return s;
}

// ---------------------------------------------------------------------------

bool Database::gravar(const Snapshot& s, std::int64_t agora) {
    if (!aberto()) return false;
    if (!impl_->exec("BEGIN IMMEDIATE")) return false;

    auto rollback = [&] {
        impl_->exec("ROLLBACK");
        return false;
    };

    sqlite3_stmt* st = nullptr;

    // --- turmas ---
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO turma (id_turma, front_end_id, codigo, nome, periodo,"
            " carga_horaria, local, horario, primeiro_visto, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma) DO UPDATE SET"
            "   front_end_id=excluded.front_end_id, codigo=excluded.codigo,"
            "   nome=excluded.nome, periodo=excluded.periodo,"
            "   carga_horaria=excluded.carga_horaria, local=excluded.local,"
            "   horario=excluded.horario, ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& t : s.turmas) {
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, t.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, t.frontEndId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, t.codigo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, t.nome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, t.periodo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 6, t.cargaHoraria);
        sqlite3_bind_text(st, 7, t.local.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, t.horario.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 9, agora);
        sqlite3_bind_int64(st, 10, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- atividades ---
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO atividade (atividade_id, id_turma, turma_nome, tipo, titulo,"
            " prazo, status, primeiro_visto, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(atividade_id, id_turma) DO UPDATE SET"
            "   turma_nome=excluded.turma_nome, tipo=excluded.tipo,"
            "   titulo=excluded.titulo, prazo=excluded.prazo,"
            "   status=excluded.status, ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& a : s.atividades) {
        const std::string prazo = a.prazo.toIso();
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, a.atividadeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, a.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, a.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, a.tipo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, a.titulo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, prazo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, statusParaTexto(a.status), -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 8, agora);
        sqlite3_bind_int64(st, 9, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- atualizações ---
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO atualizacao (hash, id_turma, turma_nome, data, texto,"
            " primeiro_visto) VALUES (?,?,?,?,?,?)"
            " ON CONFLICT(hash) DO NOTHING",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& u : s.atualizacoes) {
        const std::string data = u.data.toIso();
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, u.hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, u.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, u.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, data.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, u.texto.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 6, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- avaliações ---
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO avaliacao (id_turma, descricao, turma_nome, quando,"
            " horario_bruto, fonte, primeiro_visto, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma, descricao) DO UPDATE SET"
            "   turma_nome=excluded.turma_nome, quando=excluded.quando,"
            "   horario_bruto=excluded.horario_bruto, fonte=excluded.fonte,"
            "   ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& a : s.avaliacoes) {
        const std::string quando = a.quando.toIso();
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, a.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, a.descricao.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, a.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, quando.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, a.horarioBruto.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6,
                          a.fonte == FonteAvaliacao::TopicoAula ? "topico" : "painel",
                          -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 7, agora);
        sqlite3_bind_int64(st, 8, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- arquivos publicados nas turmas ---
    //
    // Só grava o que ESTA coleta trouxe, sem apagar o resto: um sync sem
    // `--turmas` traz zero arquivos, e um DELETE aqui zeraria a memória do que
    // já foi visto — na coleta seguinte o acervo inteiro voltaria como
    // "material novo". A tabela acumula, como as outras.
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO arquivo (id_arquivo, id_turma, turma_nome, titulo,"
            " descricao, topico, primeiro_visto, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_arquivo) DO UPDATE SET"
            "   turma_nome=excluded.turma_nome, titulo=excluded.titulo,"
            "   descricao=excluded.descricao, topico=excluded.topico,"
            "   ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& a : s.arquivos) {
        if (a.idArquivo.empty()) continue;
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, a.idArquivo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, a.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, a.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, a.titulo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, a.descricao.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, a.topico.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 7, agora);
        sqlite3_bind_int64(st, 8, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- participantes das turmas ---
    //
    // Acumula, como `arquivo`: um sync sem `--turmas` traz zero participantes,
    // e um DELETE aqui apagaria a lista inteira para reconstruí-la na coleta
    // seguinte. O efeito colateral é que quem TRANCA a disciplina continua na
    // lista até alguém abrir a aba de novo — preferível a esvaziar a tela de
    // quem só rodou um sync rápido.
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO participante (id_turma, nome, papel, turma_nome, email,"
            " matricula, curso, departamento, formacao, primeiro_visto, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma, nome) DO UPDATE SET"
            "   papel=excluded.papel, turma_nome=excluded.turma_nome,"
            "   email=excluded.email, matricula=excluded.matricula,"
            "   curso=excluded.curso, departamento=excluded.departamento,"
            "   formacao=excluded.formacao, ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& p : s.participantes) {
        if (p.idTurma.empty() || p.nome.empty()) continue;
        const char* papel =
            p.papel == PapelParticipante::Docente ? "docente" : "discente";
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, p.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, p.nome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, papel, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, p.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, p.email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, p.matricula.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, p.curso.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, p.departamento.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 9, p.formacao.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 10, agora);
        sqlite3_bind_int64(st, 11, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- tópicos de aula ---
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO topico (id_turma, titulo, inicio, fim, conteudo,"
            " primeiro_visto, ultimo_visto) VALUES (?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma, titulo, inicio) DO UPDATE SET"
            "   fim=excluded.fim, conteudo=excluded.conteudo,"
            "   ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& t : s.topicos) {
        if (t.idTurma.empty() || t.titulo.empty()) continue;
        const std::string inicio = t.inicio.toIso();
        const std::string fim = t.fim.toIso();
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, t.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, t.titulo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, inicio.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, fim.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, t.conteudo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 6, agora);
        sqlite3_bind_int64(st, 7, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    // --- frequência ---
    //
    // Upsert como todo o resto: uma coleta que não visitou as turmas traz zero
    // frequências, e apagar as guardadas faria o app anunciar "sem dados de
    // falta" logo depois de um sync leve.
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO frequencia (id_turma, turma_nome, presencas,"
            " aulas_com_registro, aulas_pela_ch, tem_dados, ultimo_visto)"
            " VALUES (?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma) DO UPDATE SET"
            "   turma_nome=excluded.turma_nome, presencas=excluded.presencas,"
            "   aulas_com_registro=excluded.aulas_com_registro,"
            "   aulas_pela_ch=excluded.aulas_pela_ch,"
            "   tem_dados=excluded.tem_dados, ultimo_visto=excluded.ultimo_visto",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& f : s.frequencias) {
        if (f.idTurma.empty()) continue;
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, f.idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, f.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, f.presencas);
        sqlite3_bind_int(st, 4, f.aulasComRegistro);
        sqlite3_bind_int(st, 5, f.aulasPelaCH);
        sqlite3_bind_int(st, 6, f.temDados ? 1 : 0);
        sqlite3_bind_int64(st, 7, agora);
        if (sqlite3_step(st) != SQLITE_DONE) {
            impl_->erro = sqlite3_errmsg(impl_->db);
            sqlite3_finalize(st);
            return rollback();
        }
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO frequencia_dia (id_turma, data, situacao, faltas)"
            " VALUES (?,?,?,?)"
            " ON CONFLICT(id_turma, data) DO UPDATE SET"
            "   situacao=excluded.situacao, faltas=excluded.faltas",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return rollback();
    }
    for (const auto& f : s.frequencias) {
        for (const auto& d : f.dias) {
            const std::string data = d.data.toIso();
            if (data.empty()) continue;
            sqlite3_reset(st);
            sqlite3_bind_text(st, 1, f.idTurma.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, data.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, situacaoParaTexto(d.situacao), -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 4, d.faltas);
            if (sqlite3_step(st) != SQLITE_DONE) {
                impl_->erro = sqlite3_errmsg(impl_->db);
                sqlite3_finalize(st);
                return rollback();
            }
        }
    }
    sqlite3_finalize(st);

    return impl_->exec("COMMIT");
}

// ---------------------------------------------------------------------------

int Database::ciclos() {
    if (!aberto()) return 0;
    sqlite3_stmt* st = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(impl_->db, "SELECT valor FROM meta WHERE chave='ciclos'",
                           -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) n = std::atoi(txt(st, 0).c_str());
    }
    sqlite3_finalize(st);
    return n;
}

int Database::participantesGuardados() {
    if (!aberto()) return 0;
    sqlite3_stmt* st = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(impl_->db, "SELECT COUNT(*) FROM participante", -1, &st,
                           nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return n;
}

void Database::registrarCiclo(std::int64_t agora) {
    if (!aberto()) return;
    const int n = ciclos() + 1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO meta (chave, valor) VALUES ('ciclos', ?)"
            " ON CONFLICT(chave) DO UPDATE SET valor=excluded.valor",
            -1, &st, nullptr) == SQLITE_OK) {
        const std::string v = std::to_string(n);
        sqlite3_bind_text(st, 1, v.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO meta (chave, valor) VALUES ('ultimo_sync', ?)"
            " ON CONFLICT(chave) DO UPDATE SET valor=excluded.valor",
            -1, &st, nullptr) == SQLITE_OK) {
        const std::string v = std::to_string(agora);
        sqlite3_bind_text(st, 1, v.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
}


// ---------------------------------------------------------------------------
// Correções do aluno e histórico
// ---------------------------------------------------------------------------

namespace {

// Texto <-> enum do histórico. O banco guarda o texto, não o número: um enum
// serializado por valor vira lixo silencioso no dia em que alguém inserir um
// caso no meio da lista, e o histórico é justamente o que não pode mentir.
avaliacao::TipoMudanca textoParaTipo(const std::string& t) {
    using T = avaliacao::TipoMudanca;
    if (t == "sigaa-marcou")    return T::SigaaMarcou;
    if (t == "sigaa-remarcou")  return T::SigaaRemarcou;
    if (t == "aluno-corrigiu")  return T::AlunoCorrigiu;
    if (t == "aluno-confirmou") return T::AlunoConfirmou;
    if (t == "aluno-criou")     return T::AlunoCriou;
    if (t == "aluno-desfez")    return T::AlunoDesfez;
    return T::SigaaAtropelou;
}

} // namespace

std::vector<avaliacao::Ajuste> Database::carregarAjustes() {
    std::vector<avaliacao::Ajuste> out;
    if (!aberto()) return out;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "SELECT id_turma, descricao, turma_nome, quando, horario_bruto, nota,"
            " criada_pelo_aluno, confirmada, ativo, quando_sigaa, editado_em"
            " FROM ajuste_avaliacao", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            avaliacao::Ajuste a;
            a.idTurma = txt(st, 0);
            a.descricao = txt(st, 1);
            a.turmaNome = txt(st, 2);
            a.quando = deIso(txt(st, 3));
            a.horarioBruto = txt(st, 4);
            a.nota = txt(st, 5);
            a.criadaPeloAluno = sqlite3_column_int(st, 6) != 0;
            a.confirmada = sqlite3_column_int(st, 7) != 0;
            a.ativo = sqlite3_column_int(st, 8) != 0;
            a.quandoSigaaNaEpoca = deIso(txt(st, 9));
            a.editadoEm = sqlite3_column_int64(st, 10);
            out.push_back(std::move(a));
        }
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::gravarAjuste(const avaliacao::Ajuste& a) {
    if (!aberto()) return false;
    if (a.idTurma.empty() || a.descricao.empty()) return false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO ajuste_avaliacao (id_turma, descricao, turma_nome, quando,"
            " horario_bruto, nota, criada_pelo_aluno, confirmada, ativo,"
            " quando_sigaa, editado_em) VALUES (?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma, descricao) DO UPDATE SET"
            "   turma_nome=excluded.turma_nome, quando=excluded.quando,"
            "   horario_bruto=excluded.horario_bruto, nota=excluded.nota,"
            "   criada_pelo_aluno=excluded.criada_pelo_aluno,"
            "   confirmada=excluded.confirmada, ativo=excluded.ativo,"
            "   quando_sigaa=excluded.quando_sigaa, editado_em=excluded.editado_em",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }

    const std::string quando = a.quando.toIso();
    const std::string sigaa = a.quandoSigaaNaEpoca.toIso();
    sqlite3_bind_text(st, 1, a.idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, a.descricao.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, a.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, quando.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, a.horarioBruto.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, a.nota.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, a.criadaPeloAluno ? 1 : 0);
    sqlite3_bind_int(st, 8, a.confirmada ? 1 : 0);
    sqlite3_bind_int(st, 9, a.ativo ? 1 : 0);
    sqlite3_bind_text(st, 10, sigaa.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 11, a.editadoEm);

    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) impl_->erro = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(st);
    return ok;
}

bool Database::gravarAjustes(const std::vector<avaliacao::Ajuste>& as) {
    // Sem transação explícita: `gravarAjuste` é um upsert por linha e a lista
    // é de dezenas, não de milhares. Uma falha no meio deixa as linhas já
    // gravadas — e isso é o certo aqui, porque cada ajuste é independente dos
    // outros; abortar tudo por causa de um perderia correções boas.
    bool ok = true;
    for (const auto& a : as) {
        if (!gravarAjuste(a)) ok = false;
    }
    return ok;
}

bool Database::removerAjuste(const std::string& idTurma, const std::string& descricao) {
    if (!aberto()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "DELETE FROM ajuste_avaliacao WHERE id_turma=? AND descricao=?",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }
    sqlite3_bind_text(st, 1, idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, descricao.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) impl_->erro = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(st);
    return ok;
}

bool Database::registrarMudanca(const avaliacao::Mudanca& m) {
    if (!aberto()) return false;
    if (m.idTurma.empty() || m.descricao.empty()) return false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO mudanca_avaliacao (id_turma, turma_nome, descricao, tipo,"
            " de, para, nota, quando) VALUES (?,?,?,?,?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }
    const std::string tipo(avaliacao::toString(m.tipo));
    sqlite3_bind_text(st, 1, m.idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, m.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, m.descricao.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, tipo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, m.de.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, m.para.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, m.nota.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, m.quando);

    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) impl_->erro = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(st);
    return ok;
}

std::vector<avaliacao::Mudanca> Database::historico(const std::string& idTurma,
                                                    const std::string& descricao,
                                                    int limite) {
    std::vector<avaliacao::Mudanca> out;
    if (!aberto()) return out;

    // Uma prova, ou todas. O filtro entra como parâmetro ligado/desligado em
    // vez de duas consultas: duas strings de SQL parecidas envelhecem em
    // direções diferentes na primeira coluna que alguém acrescentar.
    const bool filtrado = !idTurma.empty() && !descricao.empty();
    const char* sql =
        filtrado
            ? "SELECT id_turma, turma_nome, descricao, tipo, de, para, nota, quando"
              " FROM mudanca_avaliacao WHERE id_turma=? AND descricao=?"
              " ORDER BY quando DESC, id DESC LIMIT ?"
            : "SELECT id_turma, turma_nome, descricao, tipo, de, para, nota, quando"
              " FROM mudanca_avaliacao ORDER BY quando DESC, id DESC LIMIT ?";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return out;
    }
    if (filtrado) {
        sqlite3_bind_text(st, 1, idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, descricao.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, limite);
    } else {
        sqlite3_bind_int(st, 1, limite);
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        avaliacao::Mudanca m;
        m.idTurma = txt(st, 0);
        m.turmaNome = txt(st, 1);
        m.descricao = txt(st, 2);
        m.tipo = textoParaTipo(txt(st, 3));
        m.de = txt(st, 4);
        m.para = txt(st, 5);
        m.nota = txt(st, 6);
        m.quando = sqlite3_column_int64(st, 7);
        out.push_back(std::move(m));
    }
    sqlite3_finalize(st);
    return out;
}



// ---------------------------------------------------------------------------
// Presença marcada pelo aluno
// ---------------------------------------------------------------------------

namespace {

frequencia::TipoMudanca textoParaTipoPresenca(const std::string& t) {
    using T = frequencia::TipoMudanca;
    if (t == "aluno-marcou")    return T::AlunoMarcou;
    if (t == "aluno-desfez")    return T::AlunoDesfez;
    if (t == "sigaa-registrou") return T::SigaaRegistrou;
    return T::SigaaAtropelou;
}

} // namespace

std::vector<frequencia::Marcacao> Database::carregarMarcacoes(const std::string& idTurma) {
    std::vector<frequencia::Marcacao> out;
    if (!aberto()) return out;

    // Uma turma ou todas. O filtro entra como parâmetro ligado/desligado em vez
    // de duas consultas parecidas, que envelheceriam em direções diferentes.
    const bool filtrado = !idTurma.empty();
    const char* sql =
        filtrado
            ? "SELECT id_turma, data, situacao, faltas, nota, ativo, situacao_sigaa,"
              " editado_em FROM marcacao_presenca WHERE id_turma=? ORDER BY data"
            : "SELECT id_turma, data, situacao, faltas, nota, ativo, situacao_sigaa,"
              " editado_em FROM marcacao_presenca ORDER BY data";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    if (filtrado) sqlite3_bind_text(st, 1, idTurma.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(st) == SQLITE_ROW) {
        frequencia::Marcacao m;
        m.idTurma = txt(st, 0);
        m.data = deIso(txt(st, 1));
        m.situacao = textoParaSituacao(txt(st, 2));
        m.faltas = sqlite3_column_int(st, 3);
        m.nota = txt(st, 4);
        m.ativo = sqlite3_column_int(st, 5) != 0;
        m.situacaoSigaaNaEpoca = textoParaSituacao(txt(st, 6));
        m.editadoEm = sqlite3_column_int64(st, 7);
        out.push_back(std::move(m));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::gravarMarcacao(const frequencia::Marcacao& m) {
    if (!aberto() || m.idTurma.empty()) return false;
    const std::string data = m.data.toIso();
    if (data.empty()) return false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO marcacao_presenca (id_turma, data, situacao, faltas, nota,"
            " ativo, situacao_sigaa, editado_em) VALUES (?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id_turma, data) DO UPDATE SET"
            "   situacao=excluded.situacao, faltas=excluded.faltas,"
            "   nota=excluded.nota, ativo=excluded.ativo,"
            "   situacao_sigaa=excluded.situacao_sigaa,"
            "   editado_em=excluded.editado_em",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }
    sqlite3_bind_text(st, 1, m.idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, data.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, situacaoParaTexto(m.situacao), -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 4, m.faltas);
    sqlite3_bind_text(st, 5, m.nota.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, m.ativo ? 1 : 0);
    sqlite3_bind_text(st, 7, situacaoParaTexto(m.situacaoSigaaNaEpoca), -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 8, m.editadoEm);

    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) impl_->erro = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(st);
    return ok;
}

bool Database::gravarMarcacoes(const std::vector<frequencia::Marcacao>& ms) {
    bool ok = true;
    for (const auto& m : ms) {
        if (!gravarMarcacao(m)) ok = false;
    }
    return ok;
}

bool Database::removerMarcacao(const std::string& idTurma, const std::string& dataIso) {
    if (!aberto()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "DELETE FROM marcacao_presenca WHERE id_turma=? AND data=?",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }
    sqlite3_bind_text(st, 1, idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, dataIso.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Database::registrarMudancaPresenca(const frequencia::Mudanca& m) {
    if (!aberto() || m.idTurma.empty()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
            "INSERT INTO mudanca_presenca (id_turma, turma_nome, data, tipo, de,"
            " para, nota, quando) VALUES (?,?,?,?,?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        impl_->erro = sqlite3_errmsg(impl_->db);
        return false;
    }
    const std::string data = m.data.toIso();
    const std::string tipo(frequencia::toString(m.tipo));
    sqlite3_bind_text(st, 1, m.idTurma.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, m.turmaNome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, data.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, tipo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, m.de.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, m.para.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, m.nota.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, m.quando);

    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) impl_->erro = sqlite3_errmsg(impl_->db);
    sqlite3_finalize(st);
    return ok;
}

std::vector<frequencia::Mudanca> Database::historicoPresenca(const std::string& idTurma,
                                                             int limite) {
    std::vector<frequencia::Mudanca> out;
    if (!aberto()) return out;

    const bool filtrado = !idTurma.empty();
    const char* sql =
        filtrado
            ? "SELECT id_turma, turma_nome, data, tipo, de, para, nota, quando"
              " FROM mudanca_presenca WHERE id_turma=?"
              " ORDER BY quando DESC, id DESC LIMIT ?"
            : "SELECT id_turma, turma_nome, data, tipo, de, para, nota, quando"
              " FROM mudanca_presenca ORDER BY quando DESC, id DESC LIMIT ?";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    if (filtrado) {
        sqlite3_bind_text(st, 1, idTurma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, limite);
    } else {
        sqlite3_bind_int(st, 1, limite);
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        frequencia::Mudanca m;
        m.idTurma = txt(st, 0);
        m.turmaNome = txt(st, 1);
        m.data = deIso(txt(st, 2));
        m.tipo = textoParaTipoPresenca(txt(st, 3));
        m.de = txt(st, 4);
        m.para = txt(st, 5);
        m.nota = txt(st, 6);
        m.quando = sqlite3_column_int64(st, 7);
        out.push_back(std::move(m));
    }
    sqlite3_finalize(st);
    return out;
}


} // namespace sigaa::store
