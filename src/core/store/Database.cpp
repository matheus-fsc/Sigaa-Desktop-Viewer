#include "core/store/Database.h"

#include <sqlite3.h>

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

CREATE TABLE IF NOT EXISTS meta (
  chave TEXT PRIMARY KEY,
  valor TEXT
);

CREATE INDEX IF NOT EXISTS idx_atividade_prazo ON atividade(prazo);
CREATE INDEX IF NOT EXISTS idx_topico_inicio ON topico(inicio);
CREATE INDEX IF NOT EXISTS idx_arquivo_turma ON arquivo(id_turma);
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

} // namespace sigaa::store
