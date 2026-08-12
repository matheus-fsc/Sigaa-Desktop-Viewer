#include "ui/Modelos.h"

#include <QBrush>
#include <QHash>
#include <QSet>
#include <QLocale>
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

namespace {

// Código de horário por turma ("24M23"), para o `caiEm` saber em que dias da
// semana aquela turma encontra. Sem isto, um tópico que cobre três semanas
// aparece no sábado e no domingo — foi o que aconteceu com "Desenvolvimento
// Móvel" (07/08 a 28/08, turma que só tem aula na sexta).
QHash<QString, std::string> horariosDasTurmas(const Snapshot& s) {
    QHash<QString, std::string> h;
    for (const auto& t : s.turmas) {
        h.insert(QString::fromStdString(t.idTurma), t.horario);
    }
    return h;
}

// A regra de "esta aula acontece neste dia" vive em core/calendar, com teste
// headless: ela cruza o bloco de dias do tópico com a grade horária da turma, e
// errar ali esvazia a tela (ou a enche de aula que não existe) em silêncio.
bool caiEm(const TopicoAula& t, QDate d, const QHash<QString, std::string>& horarios) {
    return calendario::aulaOcorreEm(
        t, DateTime{d.year(), d.month(), d.day(), 0, 0, false},
        horarios.value(QString::fromStdString(t.idTurma)));
}

// Quantos arquivos o professor pendurou nesta aula. A coluna "Tópico de Aula"
// da aba Arquivos casa com o título do tópico (RECON §1.6.1).
int materiaisDoTopico(const Snapshot& s, const TopicoAula& t) {
    int n = 0;
    const QString titulo = umaLinha(t.titulo);
    for (const auto& a : s.arquivos) {
        if (a.idTurma == t.idTurma && umaLinha(a.topico) == titulo) ++n;
    }
    // O tópico também carrega os materiais que vieram inline com ele. Quando as
    // duas fontes existem, a da aba Arquivos é a completa — usar o máximo evita
    // tanto somar em dobro quanto mostrar zero numa aula que tem material.
    return (std::max)(n, static_cast<int>(t.materiais.size()));
}

// Cabeçalho de um dia: "Hoje — quarta, 12 de agosto".
//
// QLocale().toString, e não QDate::toString: a sobrecarga de QDate com string
// de formato usa a locale C no Qt 6, e o cabeçalho sairia "Wednesday, 12 de
// August" — meio traduzido, que é pior que nenhum.
QString rotuloDoDia(QDate dia, QDate hoje) {
    QString quando = QLocale().toString(dia, QStringLiteral("dddd, d 'de' MMMM"));
    if (!quando.isEmpty()) quando[0] = quando[0].toUpper();

    const qint64 delta = hoje.daysTo(dia);
    if (delta == 0) return QStringLiteral("Hoje — ") + quando;
    if (delta == 1) return QStringLiteral("Amanhã — ") + quando;
    if (delta == -1) return QStringLiteral("Ontem — ") + quando;
    return quando;
}

// Destaque só para o dia de hoje e para os dias à frente que TÊM aula. O que
// já passou e o que está vazio ficam esmaecidos: numa semana inteira na tela,
// se todo cabeçalho tiver o mesmo peso, nenhum orienta o olho — e o dia que a
// pessoa abriu o app para ver é o de hoje.
QStandardItem* grupoDia(const QString& texto, bool destacado) {
    auto* g = item(texto);
    QFont f = g->font();
    f.setBold(true);
    g->setFont(f);
    // A cor só reforça, nunca carrega a informação sozinha: o texto do próprio
    // grupo já diz de que dia se trata.
    if (!destacado) g->setForeground(QBrush(cor::apagado()));
    return g;
}

} // namespace

std::vector<AulaDoDia> aulasDoDia(const Snapshot& s, QDate dia) {
    std::vector<AulaDoDia> aulas;
    if (!dia.isValid()) return aulas;

    // A grade horária não tem começo nem fim — é uma regra semanal. Sem limitar
    // ao período que a coleta conhece, paginar para janeiro mostraria aula de
    // Compiladores para sempre. As pontas saem dos tópicos, que são o único
    // dado com data que o SIGAA nos dá.
    const FaixaAgenda faixa = faixaAgenda(s);
    const bool dentroDoSemestre =
        faixa.valida() && dia >= faixa.primeiro && dia <= faixa.ultimo;

    const auto horarios = horariosDasTurmas(s);
    QSet<const TopicoAula*> usados;

    for (const auto& turma : s.turmas) {
        const QString idTurma = QString::fromStdString(turma.idTurma);

        // Os tópicos que caem neste dia (o `caiEm` já cruza bloco com grade).
        std::vector<const TopicoAula*> doDia;
        for (const auto& t : s.topicos) {
            if (t.idTurma == turma.idTurma && caiEm(t, dia, horarios)) doDia.push_back(&t);
        }

        // O bloco da grade que corresponde a este dia da semana, se houver.
        const auto blocos = calendario::lerHorario(turma.horario);
        const calendario::BlocoHorario* bloco = nullptr;
        for (const auto& b : blocos) {
            if (b.dias.count(dia.dayOfWeek())) bloco = &b;
        }

        if (!bloco && doDia.empty()) continue;

        if (!bloco) {
            // Tópico datado num dia em que a turma não tem aula: reposição,
            // prova, semana especial. Aparece sempre — foi o professor que
            // apontou a data, e é o tipo de aula que não pode passar batido.
            for (const auto* t : doDia) {
                usados.insert(t);
                aulas.push_back({idTurma, umaLinha(turma.nome), QString(), umaLinha(t->titulo),
                                 umaLinha(t->conteudo), materiaisDoTopico(s, *t),
                                 /*ordem=*/999, /*semTopico=*/false, /*foraDaGrade=*/true});
            }
            continue;
        }

        // Aula da grade fora do período coletado: a regra semanal vale, mas não
        // sabemos se o semestre já começou ou acabou. Só entra se um tópico
        // datado confirmar.
        if (!dentroDoSemestre && doDia.empty()) continue;

        const QString codigo = QString::fromStdString(bloco->codigo());
        if (doDia.empty()) {
            aulas.push_back({idTurma, umaLinha(turma.nome), codigo, QString(), QString(),
                             /*materiais=*/0, bloco->ordem(), /*semTopico=*/true,
                             /*foraDaGrade=*/false});
            continue;
        }
        for (const auto* t : doDia) {
            usados.insert(t);
            aulas.push_back({idTurma, umaLinha(turma.nome), codigo, umaLinha(t->titulo),
                             umaLinha(t->conteudo), materiaisDoTopico(s, *t), bloco->ordem(),
                             /*semTopico=*/false, /*foraDaGrade=*/false});
        }
    }

    // Tópico de uma turma que não está mais na lista (trancou, ou a coleta do
    // portal trouxe menos turmas que a anterior). Some seria pior.
    for (const auto& t : s.topicos) {
        if (usados.contains(&t) || !caiEm(t, dia, horarios)) continue;
        aulas.push_back({QString::fromStdString(t.idTurma),
                         QString::fromStdString(t.idTurma), QString(), umaLinha(t.titulo),
                         umaLinha(t.conteudo), materiaisDoTopico(s, t), 999,
                         /*semTopico=*/false, /*foraDaGrade=*/true});
    }

    std::stable_sort(aulas.begin(), aulas.end(),
                     [](const AulaDoDia& a, const AulaDoDia& b) { return a.ordem < b.ordem; });
    return aulas;
}

ResumoDia resumoDia(const Snapshot& s, QDate hoje) {
    ResumoDia r;
    r.semDados = s.topicos.empty();
    r.aulasHoje = static_cast<int>(aulasDoDia(s, hoje).size());
    r.aulasAmanha = static_cast<int>(aulasDoDia(s, hoje.addDays(1)).size());
    return r;
}

int aulasEntre(const Snapshot& s, QDate inicio, QDate fim) {
    int n = 0;
    for (QDate d = inicio; d.isValid() && d <= fim; d = d.addDays(1)) {
        n += static_cast<int>(aulasDoDia(s, d).size());
    }
    return n;
}

FaixaAgenda faixaAgenda(const Snapshot& s) {
    FaixaAgenda f;
    for (const auto& t : s.topicos) {
        // O fim vale como limite superior; quando o professor registrou um dia
        // só, `fim` vem inválido e o início responde pelos dois lados.
        const QDate a = paraQDate(t.inicio);
        const QDate b = paraQDate(t.fim);
        for (const QDate d : {a, b}) {
            if (!d.isValid()) continue;
            if (!f.primeiro.isValid() || d < f.primeiro) f.primeiro = d;
            if (!f.ultimo.isValid() || d > f.ultimo) f.ultimo = d;
        }
    }
    return f;
}

QStandardItemModel* modeloAgenda(const Snapshot& s, QDate inicio, QDate fim, QDate hoje,
                                 QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Turma"), QStringLiteral("Aula"),
                               QStringLiteral("Horário"), QStringLiteral("Material")});
    if (!inicio.isValid() || !fim.isValid()) return m;

    for (QDate dia = inicio; dia <= fim; dia = dia.addDays(1)) {
        QList<QList<QStandardItem*>> filhos;

        // A TURMA vem primeiro agora, e não o título do tópico: a aula existe
        // pela grade horária, e o tópico é o conteúdo dela quando o professor
        // registrou. Com o título na frente, uma aula sem tópico virava uma
        // linha que começava com um traço.
        for (const AulaDoDia& a : aulasDoDia(s, dia)) {
            auto* turma = item(a.turma);
            turma->setData(a.idTurma, PapelIdTurma);

            QStandardItem* titulo = nullptr;
            if (a.semTopico) {
                // Não é erro nem falta de dado nosso: a aula está na grade, o
                // professor é que não publicou nada para o dia. Dizer isso é
                // diferente de omitir a aula, que faria o aluno concluir que
                // não tem aula.
                titulo = item(QStringLiteral("sem tópico registrado"));
                titulo->setForeground(QBrush(cor::apagado()));
                titulo->setToolTip(QStringLiteral(
                    "A turma tem aula neste dia pela grade horária, mas o "
                    "professor não publicou tópico para ela na Turma Virtual."));
            } else {
                titulo = item(a.titulo);
                if (!a.conteudo.isEmpty()) titulo->setToolTip(a.conteudo);
            }

            auto* quando = item(a.horario, a.ordem);
            if (a.foraDaGrade) {
                quando->setText(QStringLiteral("extra"));
                quando->setForeground(QBrush(cor::inferido()));
                quando->setToolTip(QStringLiteral(
                    "O professor datou esta aula num dia em que a turma não tem "
                    "horário — reposição, prova ou semana especial."));
            }

            filhos.append({turma, titulo, quando,
                           item(a.materiais == 0
                                    ? QString()
                                    : QStringLiteral("%1 arquivo(s)").arg(a.materiais),
                                a.materiais)});
        }

        const int n = filhos.size();
        auto* grupo = grupoDia(rotuloDoDia(dia, hoje),
                               /*destacado=*/dia == hoje || (n > 0 && dia > hoje));
        // Quantas aulas o dia tem, para quem desenha decidir o que expandir sem
        // ter que percorrer o modelo de novo.
        grupo->setData(n, PapelOrdenacao);

        for (const auto& linha : filhos) grupo->appendRow(linha);
        if (n == 0) {
            // Linha explícita em vez de grupo vazio: um grupo sem filhos parece
            // um erro de carregamento, e a pessoa fica esperando algo aparecer.
            auto* vazio = item(QStringLiteral("Nenhuma aula registrada."));
            vazio->setForeground(QBrush(cor::apagado()));
            grupo->appendRow({vazio, item(QString()), item(QString()), item(QString())});
        }
        m->appendRow({grupo, item(QString()), item(QString()), item(QString())});
    }
    return m;
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

namespace {

// "18/08" para uma aula de um dia só, "18/08 – 20/08" quando o professor
// registrou um intervalo. O ano fica de fora: a janela é de uma turma de um
// período, e repetir "2026" em quinze linhas só rouba largura da coluna que
// interessa.
QString intervaloTopico(const TopicoAula& t) {
    const QDate a = paraQDate(t.inicio);
    const QDate b = paraQDate(t.fim);
    if (!a.isValid()) return {};
    if (!b.isValid() || b == a) return a.toString(QStringLiteral("dd/MM"));
    return a.toString(QStringLiteral("dd/MM")) + QStringLiteral(" – ") +
           b.toString(QStringLiteral("dd/MM"));
}

QStandardItem* linhaMaterial(const QString& titulo, const QString& id, bool baixavel,
                             const QString& detalhe) {
    auto* it = item(titulo);
    if (!id.isEmpty()) it->setData(id, PapelIdArquivo);
    it->setData(baixavel, PapelBaixavel);
    if (!detalhe.isEmpty()) it->setToolTip(detalhe);
    if (!baixavel) {
        // Não é um erro nem um aviso: é uma tarefa, um fórum, um vídeo. Existe
        // na aula e o aluno merece vê-lo listado; só não dá para baixar daqui.
        it->setForeground(QBrush(cor::apagado()));
    }
    return it;
}

} // namespace

QStandardItemModel* modeloAulas(const std::vector<TopicoAula>& topicos,
                                const std::vector<ArquivoTurma>& arquivos,
                                const QHash<QString, QString>& offline, QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Aula / material"), QStringLiteral("Quando"),
                               QStringLiteral("Offline")});

    // Índice dos arquivos por id: é o que decide quem é baixável.
    QSet<QString> idsDeArquivo;
    for (const auto& a : arquivos) idsDeArquivo.insert(QString::fromStdString(a.idArquivo));

    auto marcaOffline = [&offline](const QString& id) {
        return offline.contains(id) ? QStringLiteral("✓") : QString();
    };

    QSet<QString> usados;

    for (const auto& t : topicos) {
        auto* aula = item(umaLinha(t.titulo));
        QFont f = aula->font();
        f.setBold(true);
        aula->setFont(f);
        if (!t.conteudo.empty()) aula->setToolTip(umaLinha(t.conteudo));

        // Par (item da coluna 0, texto da coluna "Quando").
        std::vector<std::pair<QStandardItem*, QString>> filhos;

        for (const auto& mat : t.materiais) {
            const QString id = QString::fromStdString(mat.id);
            const bool baixavel = !id.isEmpty() && idsDeArquivo.contains(id);
            if (!id.isEmpty()) usados.insert(id);
            filhos.emplace_back(linhaMaterial(umaLinha(mat.titulo), id, baixavel,
                                              umaLinha(mat.descricao)),
                                QString::fromStdString(mat.tipo));
        }

        // A aba Arquivos agrupa por "Tópico de Aula", e a coluna casa com o
        // título do tópico (RECON §1.6.1). É o que traz o PDF para dentro da
        // aula em que ele foi publicado.
        for (const auto& a : arquivos) {
            if (umaLinha(a.topico) != umaLinha(t.titulo)) continue;
            const QString id = QString::fromStdString(a.idArquivo);
            if (usados.contains(id)) continue;
            usados.insert(id);
            filhos.emplace_back(linhaMaterial(umaLinha(a.titulo), id, /*baixavel=*/true,
                                              umaLinha(a.descricao)),
                                QStringLiteral("arquivo"));
        }

        for (const auto& [filho, tipo] : filhos) {
            const QString id = filho->data(PapelIdArquivo).toString();
            aula->appendRow({filho, item(tipo), item(marcaOffline(id))});
        }

        m->appendRow({aula, item(intervaloTopico(t)), item(QString())});
    }

    // O que não casou com nenhuma aula. Some seria pior que desarrumado.
    QList<QStandardItem*> soltos;
    for (const auto& a : arquivos) {
        const QString id = QString::fromStdString(a.idArquivo);
        if (usados.contains(id)) continue;
        soltos << linhaMaterial(umaLinha(a.titulo), id, /*baixavel=*/true,
                                umaLinha(a.topico));
    }
    if (!soltos.isEmpty()) {
        auto* grupo = item(QStringLiteral("Outros arquivos da turma"));
        QFont f = grupo->font();
        f.setBold(true);
        grupo->setFont(f);
        for (auto* s : soltos) {
            grupo->appendRow({s, item(QStringLiteral("arquivo")),
                              item(marcaOffline(s->data(PapelIdArquivo).toString()))});
        }
        m->appendRow({grupo, item(QString()), item(QString())});
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
