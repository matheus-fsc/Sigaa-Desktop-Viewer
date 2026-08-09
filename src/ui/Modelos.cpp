#include "ui/Modelos.h"

#include <QBrush>
#include <QColor>
#include <QDate>
#include <QFont>
#include <QStandardItem>
#include <QStandardItemModel>

#include <algorithm>

#include "core/calendar/Calendario.h"
#include "core/model/Models.h"

namespace sigaa::ui {
namespace {

QDate paraQDate(const DateTime& d) {
    if (!d.valid()) return {};
    return QDate(d.year, d.month, d.day);
}

QStandardItem* item(const QString& texto, const QVariant& ordem) {
    auto* it = new QStandardItem(texto);
    it->setEditable(false);
    it->setData(ordem, PapelOrdenacao);
    return it;
}

QStandardItem* item(const QString& texto) { return item(texto, texto); }

// Texto simples sem quebra: o SIGAA manda atualização com \n e \t no meio, e
// numa célula de tabela isso vira um retângulo com um símbolo estranho.
QString umaLinha(const std::string& s) {
    return QString::fromStdString(s).simplified();
}

QStandardItemModel* novoModelo(QObject* pai, const QStringList& colunas) {
    auto* m = new QStandardItemModel(0, colunas.size(), pai);
    m->setHorizontalHeaderLabels(colunas);
    return m;
}

} // namespace

namespace cor {
QColor atrasado() { return QColor(0xC0, 0x39, 0x2B); }
QColor urgente() { return QColor(0xD3, 0x5C, 0x00); }
QColor inferido() { return QColor(0xD3, 0x5C, 0x00); }
QColor apagado() { return QColor(0x7F, 0x8C, 0x8D); }
} // namespace cor

QString formatarData(const DateTime& d) {
    const QDate qd = paraQDate(d);
    if (!qd.isValid()) return {};
    QString s = qd.toString("dd/MM/yyyy");
    if (d.hasTime) {
        s += QString(" %1:%2")
                 .arg(d.hour, 2, 10, QChar('0'))
                 .arg(d.minute, 2, 10, QChar('0'));
    }
    return s;
}

int diasAte(const DateTime& d) {
    const QDate qd = paraQDate(d);
    if (!qd.isValid()) return semPrazo;
    return static_cast<int>(QDate::currentDate().daysTo(qd));
}

QString prazoRelativo(const DateTime& d) {
    const int n = diasAte(d);
    if (n == semPrazo) return QStringLiteral("sem prazo");
    if (n == 0) return QStringLiteral("vence hoje");
    if (n == 1) return QStringLiteral("amanhã");
    if (n == -1) return QStringLiteral("atrasado 1 dia");
    if (n < 0) return QStringLiteral("atrasado %1 dias").arg(-n);
    return QStringLiteral("em %1 dias").arg(n);
}

QStandardItemModel* modeloPrazos(const Snapshot& s, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Prazo"), QStringLiteral("Vence"),
                               QStringLiteral("Turma"), QStringLiteral("Atividade"),
                               QStringLiteral("Tipo")});

    for (const auto& a : s.atividades) {
        const int dias = diasAte(a.prazo);
        const bool concluida = a.status == StatusAtividade::Concluida;

        auto* prazo = item(concluida ? QStringLiteral("concluída") : prazoRelativo(a.prazo),
                           concluida ? semPrazo : dias);

        // Vermelho para atrasado, laranja para "vence em até 2 dias". Só nessa
        // coluna: pintar a linha inteira transforma a tabela num semáforo e
        // nenhuma cor significa mais nada.
        if (!concluida && dias != semPrazo) {
            if (dias < 0) prazo->setForeground(QBrush(cor::atrasado()));
            else if (dias <= 2) prazo->setForeground(QBrush(cor::urgente()));
            if (dias <= 2) {
                QFont f = prazo->font();
                f.setBold(true);
                prazo->setFont(f);
            }
        }
        if (concluida) prazo->setForeground(QBrush(cor::apagado()));

        m->appendRow({
            prazo,
            item(formatarData(a.prazo), QString::fromStdString(a.prazo.toIso())),
            item(umaLinha(a.turmaNome)),
            item(umaLinha(a.titulo)),
            item(umaLinha(a.tipo)),
        });
    }
    return m;
}

QStandardItemModel* modeloProvas(const Snapshot& s, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Data"), QStringLiteral("Turma"),
                               QStringLiteral("Avaliação"), QStringLiteral("Horário"),
                               QStringLiteral("Origem")});

    // Mescla as duas fontes com a mesma regra do .ics — a UI e o calendário não
    // podem discordar sobre quantas provas existem.
    for (const auto& av : calendario::mesclarAvaliacoes(s.avaliacoes)) {
        const bool inferida = av.fonte == FonteAvaliacao::TopicoAula;

        auto* origem = item(inferida ? QStringLiteral("inferido — confirme")
                                     : QStringLiteral("painel do professor"),
                            inferida ? 1 : 0);
        if (inferida) {
            origem->setForeground(QBrush(cor::inferido()));
            origem->setToolTip(QStringLiteral(
                "Data deduzida do título de um tópico de aula, não do painel de "
                "avaliações. Confirme com o professor antes de confiar."));
        }

        m->appendRow({
            item(formatarData(av.quando), QString::fromStdString(av.quando.toIso())),
            item(umaLinha(av.turmaNome)),
            item(umaLinha(av.descricao)),
            item(umaLinha(av.horarioBruto)),
            origem,
        });
    }
    return m;
}

QMap<QDate, DiaComProva> provasPorDia(const Snapshot& s) {
    QMap<QDate, DiaComProva> m;
    for (const auto& av : calendario::mesclarAvaliacoes(s.avaliacoes)) {
        const QDate d = paraQDate(av.quando);
        if (!d.isValid()) continue;   // prova sem data não marca dia nenhum
        auto& dia = m[d];
        if (av.fonte == FonteAvaliacao::TopicoAula) ++dia.inferidas;
        else ++dia.confirmadas;
    }
    return m;
}

ResumoProvas resumoProvas(const Snapshot& s) {
    ResumoProvas r;
    const QDate hoje = QDate::currentDate();

    // Ordena por data para que "a próxima" seja a primeira futura, e não a
    // primeira que o crawler tiver encontrado.
    auto avs = calendario::mesclarAvaliacoes(s.avaliacoes);
    std::sort(avs.begin(), avs.end(),
              [](const Avaliacao& a, const Avaliacao& b) { return a.quando < b.quando; });

    for (const auto& av : avs) {
        ++r.total;
        if (av.fonte == FonteAvaliacao::TopicoAula) ++r.inferidas;

        const QDate d = paraQDate(av.quando);
        if (!d.isValid()) continue;

        const int dias = static_cast<int>(hoje.daysTo(d));
        if (dias >= 0 && dias <= 30) ++r.proximos30;

        // A prova de hoje ainda conta como próxima: ela é exatamente a que o
        // aluno precisa ver ao abrir o app.
        if (dias >= 0 && !r.data.isValid()) {
            r.data = d;
            r.emDias = dias;
            r.descricao = umaLinha(av.descricao);
            r.turma = umaLinha(av.turmaNome);
        }
    }
    return r;
}

QStandardItemModel* modeloTurmas(const Snapshot& s, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Turma"), QStringLiteral("Horário"),
                               QStringLiteral("Local"), QStringLiteral("Período")});
    for (const auto& t : s.turmas) {
        auto* nome = item(umaLinha(t.nome));
        // Sem frontEndId não há como entrar na turma virtual (RECON §1.2). A
        // linha continua na lista — a turma existe — mas o cinza avisa antes
        // do duplo clique que ali não vai abrir nada.
        if (t.frontEndId.empty()) {
            nome->setForeground(QBrush(cor::apagado()));
            nome->setToolTip(QStringLiteral(
                "Esta turma não expôs o identificador da Turma Virtual nesta "
                "coleta. Use \"Atualizar tudo\" e tente de novo."));
        }
        m->appendRow({
            nome,
            item(umaLinha(t.horario)),
            item(umaLinha(t.local)),
            item(umaLinha(t.periodo)),
        });
    }
    return m;
}

QStandardItemModel* modeloArquivos(const std::vector<ArquivoTurma>& arquivos, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Arquivo"), QStringLiteral("Tópico de aula")});
    for (const auto& a : arquivos) {
        auto* titulo = item(umaLinha(a.titulo));
        titulo->setData(QString::fromStdString(a.idArquivo), PapelIdArquivo);
        if (!a.descricao.empty()) titulo->setToolTip(umaLinha(a.descricao));
        m->appendRow({titulo, item(umaLinha(a.topico))});
    }
    return m;
}

QStandardItemModel* modeloAtualizacoes(const Snapshot& s, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Data"), QStringLiteral("Turma"),
                               QStringLiteral("Atualização")});

    for (const auto& a : s.atualizacoes) {
        m->appendRow({
            item(formatarData(a.data), QString::fromStdString(a.data.toIso())),
            item(umaLinha(a.turmaNome)),
            item(umaLinha(a.texto)),
        });
    }
    return m;
}

} // namespace sigaa::ui
