#include "ui/Modelos.h"

#include "ui/Distintivos.h"
#include "ui/Tema.h"

#include <QBrush>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSet>
#include <QLocale>
#include <QColor>
#include <QDate>
#include <QFont>
#include <QIcon>
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

// Tamanho do avatar na lista de participantes. Proporção 4:5, que é a do
// retrato que o SIGAA serve (48x60) — esticar para um quadrado achataria a
// cara de todo mundo.
constexpr int kAvatarL = 36;
constexpr int kAvatarA = 45;

// As iniciais de "MARIA DA SILVA COELHO" -> "MC".
QString iniciaisDe(const QString& nome) {
    const QStringList partes = nome.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Conectivos fora: "MD" de "MARIA DA SILVA COELHO" não identifica ninguém.
    static const QSet<QString> conectivos{
        QStringLiteral("DA"),  QStringLiteral("DE"),  QStringLiteral("DO"),
        QStringLiteral("DAS"), QStringLiteral("DOS"), QStringLiteral("E"),
    };
    QStringList uteis;
    for (const QString& parte : partes) {
        if (!conectivos.contains(parte.toUpper())) uteis << parte;
    }
    if (uteis.isEmpty()) uteis = partes;
    if (uteis.isEmpty()) return QStringLiteral("?");
    QString r = uteis.first().left(1);
    if (uteis.size() > 1) r += uteis.last().left(1);
    return r.toUpper();
}

// Cor estável por pessoa: a mesma pessoa tem sempre o mesmo fundo, em turmas
// diferentes e entre execuções. Aleatório a cada abertura seria pior que cinza,
// porque a cor deixaria de ajudar a reconhecer alguém de relance.
QColor corDoNome(const QString& nome) {
    uint h = qHash(nome);
    // Saturação e luminosidade fixas: só o matiz varia, então nenhum avatar
    // sai claro demais para o texto branco por cima.
    return QColor::fromHsl(static_cast<int>(h % 360), 110, 105);
}

// As iniciais da pessoa num quadrado colorido.
//
// Já foi o retrato de verdade, lido de um cache em disco. A foto saiu em
// 18/09/2026: alimentá-la significava copiar a imagem de 31 colegas para a
// máquina do aluno, e o rosto de alguém é dado dessa pessoa — ela cadastrou a
// foto no SIGAA, para o SIGAA.
//
// As iniciais resolvem o que a tela precisava (reconhecer alguém de relance) e
// funcionam para todo mundo, inclusive os 13 de 32 que nunca cadastraram
// retrato — antes, esses ficavam com o mesmo quadrado que agora todos têm.
QPixmap avatarDe(const QString& nome) {
    const qreal dpr = 2.0;   // nítido em tela HiDPI sem depender da janela
    QPixmap alvo(static_cast<int>(kAvatarL * dpr), static_cast<int>(kAvatarA * dpr));
    alvo.setDevicePixelRatio(dpr);
    alvo.fill(Qt::transparent);

    QPainter g(&alvo);
    g.setRenderHint(QPainter::Antialiasing);
    g.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF r(0, 0, kAvatarL, kAvatarA);
    QPainterPath moldura;
    moldura.addRoundedRect(r, 4, 4);
    g.setClipPath(moldura);

    g.fillRect(r, corDoNome(nome));
    QFont f = g.font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    g.setFont(f);
    g.setPen(Qt::white);
    g.drawText(r, Qt::AlignCenter, iniciaisDe(nome));
    return alvo;
}

QStandardItemModel* novoModelo(QObject* pai, const QStringList& colunas) {
    auto* m = new QStandardItemModel(0, colunas.size(), pai);
    m->setHorizontalHeaderLabels(colunas);
    return m;
}

// Um cabeçalho de seção na árvore ("Atuais", "Histórico (7)").
//
// `PapelOrdenacao` recebe a POSIÇÃO da seção, não o texto: a árvore é
// ordenável, e sem uma chave própria um clique em "Prazo" jogaria o histórico
// para cima dos prazos que ainda importam.
QStandardItem* grupoSecao(const QString& texto, int posicao, bool apagado) {
    auto* g = item(texto, posicao);
    QFont f = g->font();
    f.setBold(true);
    g->setFont(f);
    if (apagado) g->setForeground(QBrush(cor::apagado()));
    return g;
}

// Preenche a linha do grupo até `colunas`, para o cabeçalho ocupar a largura
// toda em vez de deixar células fantasma que aceitam seleção.
QList<QStandardItem*> linhaDeGrupo(QStandardItem* g, int colunas) {
    QList<QStandardItem*> linha{g};
    for (int i = 1; i < colunas; ++i) linha << item(QString());
    return linha;
}

} // namespace

namespace cor {
// Repassadas ao tema, que tem uma versão para cada modo. Ficaram aqui como
// nomes porque é por estes nomes que a tabela de provas e o calendário falam
// da mesma coisa; o tom de vermelho é decisão do tema, não da apresentação.
QColor atrasado() { return tema::cor::atrasado(); }
QColor urgente() { return tema::cor::urgente(); }
QColor inferido() { return tema::cor::inferido(); }
QColor apagado() { return tema::cor::apagado(); }
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
    constexpr int kColunasPrazos = 5;
    auto* m = novoModelo(pai, {QStringLiteral("Prazo"), QStringLiteral("Vence"),
                               QStringLiteral("Turma"), QStringLiteral("Atividade"),
                               QStringLiteral("Tipo")});

    QList<QList<QStandardItem*>> atuais;
    QList<QList<QStandardItem*>> historico;

    for (const auto& a : s.atividades) {
        const int dias = diasAte(a.prazo);
        const bool concluida = a.status == StatusAtividade::Concluida;

        auto* prazo = item(concluida ? QStringLiteral("concluída") : prazoRelativo(a.prazo),
                           concluida ? semPrazo : dias);

        // Distintivo só no que EXIGE ação, e só nessa coluna.
        //
        // "em 22 dias" fica como texto simples de propósito: etiqueta em toda
        // linha não é hierarquia, é ruído com cantos arredondados — se tudo é
        // destaque, nada é. Quem está com o prazo em dia não precisa ser
        // avisado disso; quem está atrasado precisa, e é esse que a etiqueta
        // encontra numa varredura vertical, sem ler uma palavra.
        Tom tom = Tom::Nenhum;
        if (concluida) tom = Tom::Apagado;
        else if (dias == semPrazo) tom = Tom::Nenhum;   // sem prazo não é urgente
        else if (dias < 0) tom = Tom::Atrasado;
        else if (dias <= 2) tom = Tom::Urgente;
        if (tom != Tom::Nenhum) prazo->setData(static_cast<int>(tom), PapelDistintivo);
        if (concluida || dias == semPrazo) prazo->setForeground(QBrush(cor::apagado()));

        QList<QStandardItem*> linha{
            prazo,
            item(formatarData(a.prazo), QString::fromStdString(a.prazo.toIso())),
            item(umaLinha(a.turmaNome)),
            item(umaLinha(a.titulo)),
            item(umaLinha(a.tipo)),
        };

        // O que já passou do ponto vai para o histórico. Uma entrega atrasada
        // há dois dias ainda é assunto; a de três semanas atrás não volta, e
        // deixá-la no meio da lista empurra para baixo o que vence amanhã.
        //
        // Concluída também desce: ela não pede nada de ninguém.
        const bool velha = concluida ||
                           (dias != semPrazo && dias <= -kDiasParaHistorico);
        (velha ? historico : atuais).push_back(std::move(linha));
    }

    // O histórico existe MESMO VAZIO? Não: um grupo "Histórico (0)" só ocupa
    // linha e ensina a ignorar cabeçalho. Some quando não há o que guardar.
    if (!atuais.isEmpty() || historico.isEmpty()) {
        auto* g = grupoSecao(QStringLiteral("Prazos atuais"), 0, /*apagado=*/false);
        for (const auto& l : atuais) g->appendRow(l);
        m->appendRow(linhaDeGrupo(g, kColunasPrazos));
    }
    if (!historico.isEmpty()) {
        auto* g = grupoSecao(
            QStringLiteral("Histórico (%1)").arg(historico.size()), 1, /*apagado=*/true);
        g->setToolTip(QStringLiteral(
            "Entregas concluídas e as atrasadas há %1 dias ou mais. Ficam aqui "
            "para não empurrarem para baixo o que ainda dá para fazer.")
                          .arg(kDiasParaHistorico));
        for (const auto& l : historico) g->appendRow(l);
        m->appendRow(linhaDeGrupo(g, kColunasPrazos));
    }
    return m;
}

namespace {

// Texto e tom do distintivo de origem, por estado.
//
// A coluna "Origem" responde a uma pergunta só: QUANTO dá para confiar nesta
// data? Por isso ela mistura procedência (painel, tópico) com intervenção do
// aluno (corrigida, confirmada) — do ponto de vista de quem vai estudar, são
// a mesma pergunta.
struct Procedencia {
    QString texto;
    Tom tom;
    QString dica;
};

Procedencia procedenciaDe(avaliacao::Estado e) {
    using E = avaliacao::Estado;
    switch (e) {
    case E::DoSigaa:
        return {QStringLiteral("painel do professor"), Tom::Nenhum, {}};
    case E::Inferida:
        return {QStringLiteral("inferido — confirme"), Tom::Urgente,
                QStringLiteral(
                    "Data deduzida do título de um tópico de aula, não do painel "
                    "de avaliações. Use “Confirmar” ou “Corrigir data”.")};
    case E::Confirmada:
        return {QStringLiteral("✓ você confirmou"), Tom::Sucesso,
                QStringLiteral("Você deu esta data como boa. O app parou de pedir "
                               "confirmação para ela.")};
    case E::Editada:
        return {QStringLiteral("✎ você corrigiu"), Tom::Sucesso,
                QStringLiteral("Esta é a sua data, não a do SIGAA. A data que o "
                               "SIGAA ainda anuncia aparece na coluna ao lado.")};
    case E::Criada:
        return {QStringLiteral("✎ você criou"), Tom::Sucesso,
                QStringLiteral("Prova que você cadastrou. O SIGAA não sabe que "
                               "ela existe.")};
    case E::Conflitada:
        return {QStringLiteral("⚠ o SIGAA mudou"), Tom::Atrasado,
                QStringLiteral("O professor atualizou a plataforma e a data dele "
                               "substituiu a sua. Veja o histórico.")};
    }
    return {};
}

} // namespace

QStandardItemModel* modeloProvas(const std::vector<avaliacao::Efetiva>& provas,
                                 QObject* pai) {
    constexpr int kColunasProvas = 6;
    auto* m = novoModelo(pai, {QStringLiteral("Data"), QStringLiteral("Turma"),
                               QStringLiteral("Avaliação"), QStringLiteral("Horário"),
                               QStringLiteral("Origem"), QStringLiteral("SIGAA diz")});

    QList<QList<QStandardItem*>> proximas;
    QList<QList<QStandardItem*>> antigas;

    for (const auto& p : provas) {
        const Avaliacao& av = p.av;
        const Procedencia proc = procedenciaDe(p.estado);

        auto* origem = item(proc.texto, static_cast<int>(p.estado));
        if (proc.tom != Tom::Nenhum) {
            origem->setData(static_cast<int>(proc.tom), PapelDistintivo);
        } else {
            origem->setForeground(QBrush(cor::apagado()));
        }
        if (!proc.dica.isEmpty()) origem->setToolTip(proc.dica);

        // A coluna "SIGAA diz" só tem conteúdo quando há discordância. Repetir
        // a mesma data nas duas colunas em toda linha ensinaria a ignorá-la,
        // e a linha onde ela difere é justamente a que importa.
        const bool discorda = p.quandoSigaa.valid() &&
                              p.quandoSigaa.toIso() != av.quando.toIso();
        auto* sigaa = item(discorda ? formatarData(p.quandoSigaa) : QString());
        if (discorda) {
            sigaa->setForeground(QBrush(cor::apagado()));
            sigaa->setToolTip(QStringLiteral(
                "O SIGAA continua anunciando esta data. A que vale, acima, é a "
                "sua correção."));
        }

        auto* data = item(formatarData(av.quando),
                          QString::fromStdString(av.quando.toIso()));
        // A chave viaja na linha: é como o botão de corrigir sabe sobre qual
        // prova o aluno clicou, sem depender da ordem da tabela, que muda a
        // cada clique no cabeçalho.
        data->setData(QString::fromStdString(av.idTurma), PapelIdTurmaProva);
        data->setData(QString::fromStdString(av.descricao), PapelDescricaoProva);
        data->setData(static_cast<int>(p.estado), PapelEstadoProva);

        auto* descricao = item(umaLinha(av.descricao));
        if (!p.nota.empty()) {
            // A nota do aluno ("adiada em sala, confirmei com o monitor") é o
            // que explica, em outubro, por que a data é essa.
            descricao->setToolTip(umaLinha(p.nota));
        }

        QList<QStandardItem*> linha{
            data,
            item(umaLinha(av.turmaNome)),
            descricao,
            item(umaLinha(av.horarioBruto)),
            origem,
            sigaa,
        };

        // O CORTE DA PROVA É MAIS DURO QUE O DO PRAZO, e de propósito: uma
        // entrega atrasada ainda pode ser feita, uma prova que já aconteceu
        // não. Passou o dia, vai para o histórico.
        //
        // O dia da prova NÃO conta como passado: é justamente o dia em que ela
        // precisa estar à vista, e uma prova às 15h45 não pode sumir da tela
        // às 00h01.
        const QDate quando = paraQDate(av.quando);
        const bool passou = quando.isValid() && quando < QDate::currentDate();
        (passou ? antigas : proximas).push_back(std::move(linha));
    }

    if (!proximas.isEmpty() || antigas.isEmpty()) {
        auto* g = grupoSecao(QStringLiteral("Próximas provas"), 0, /*apagado=*/false);
        for (const auto& l : proximas) g->appendRow(l);
        m->appendRow(linhaDeGrupo(g, kColunasProvas));
    }
    if (!antigas.isEmpty()) {
        auto* g = grupoSecao(QStringLiteral("Provas antigas (%1)").arg(antigas.size()),
                             1, /*apagado=*/true);
        g->setToolTip(QStringLiteral(
            "Provas cuja data já passou. Ficam guardadas — a data e a origem "
            "continuam sendo o registro do que aconteceu no semestre."));
        for (const auto& l : antigas) g->appendRow(l);
        m->appendRow(linhaDeGrupo(g, kColunasProvas));
    }
    return m;
}

QMap<QDate, DiaComProva> provasPorDia(const std::vector<avaliacao::Efetiva>& provas) {
    QMap<QDate, DiaComProva> m;
    for (const auto& p : provas) {
        const QDate d = paraQDate(p.av.quando);
        if (!d.isValid()) continue;   // prova sem data não marca dia nenhum
        auto& dia = m[d];
        // Só a inferida NÃO confirmada conta como duvidosa: depois que o aluno
        // confirma ou corrige, o ponto no calendário não deve mais pedir
        // confirmação — foi ele que a deu.
        if (p.estado == avaliacao::Estado::Inferida) ++dia.inferidas;
        else ++dia.confirmadas;
    }
    return m;
}

ResumoProvas resumoProvas(const std::vector<avaliacao::Efetiva>& provas) {
    ResumoProvas r;
    const QDate hoje = QDate::currentDate();

    // `efetivas` já devolve em ordem cronológica, então "a próxima" é a
    // primeira futura da lista — não há o que reordenar aqui.
    for (const auto& p : provas) {
        const Avaliacao& av = p.av;
        ++r.total;
        // Só a inferida ainda não confirmada entra na conta de "confirmar":
        // o cartão existe para dizer quantas datas pedem uma conversa com o
        // professor, e a que o aluno já confirmou não pede mais.
        if (p.estado == avaliacao::Estado::Inferida) ++r.inferidas;

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

namespace {

// A célula "faltas n/k" da agenda.
//
// n são as aulas perdidas, k o teto antes de reprovar (25% da carga horária do
// componente — a regra que a própria página do SIGAA enuncia). A UNIDADE É A
// HORA-AULA, não o dia: um encontro de dois horários seguidos lança 2 faltas,
// e é por isso que uma disciplina de 64 horas tolera 16 faltas e não 16 dias.
//
// O tom sobe junto com o risco, e só aí: em "2/16" no começo do semestre um
// distintivo colorido seria alarme sem fato. Ver a nota sobre hierarquia em
// ui/Distintivos.h.
QStandardItem* celulaFaltas(const Frequencia* f) {
    if (!f || !f->temDados) {
        // "O professor não abriu o diário" NÃO é "zero faltas". As duas levam
        // a decisões opostas, e a segunda é a que faz alguém faltar mais uma.
        auto* it = item(QStringLiteral("—"));
        it->setForeground(QBrush(cor::apagado()));
        it->setToolTip(QStringLiteral(
            "O professor ainda não lançou frequência nesta turma. Isto não "
            "quer dizer que você não tem faltas."));
        return it;
    }

    const int n = f->faltas();
    const int k = f->limiteFaltas();
    auto* it = item(QStringLiteral("%1/%2").arg(n).arg(k), n);

    Tom tom = Tom::Nenhum;
    if (f->reprovado()) tom = Tom::Atrasado;
    else if (k > 0 && n * 4 >= k * 3) tom = Tom::Urgente;   // 3/4 do teto
    if (tom != Tom::Nenhum) it->setData(static_cast<int>(tom), PapelDistintivo);

    // Quantos dias o professor deixou em branco. É o que explica um número que
    // parece bom demais — e é o aviso de que ele ainda pode piorar.
    int semRegistro = 0;
    for (const auto& d : f->dias) {
        if (d.situacao == SituacaoDia::NaoRegistrada) ++semRegistro;
    }

    QString dica = QStringLiteral(
        "%1 falta(s) de %2 aulas. O limite é %3 (25% da carga horária de %4 "
        "aulas); acima disso, reprovação por frequência.")
        .arg(n).arg(f->aulasComRegistro).arg(k).arg(f->aulasPelaCH);
    if (f->aulasComRegistro < f->aulasPelaCH) {
        dica += QStringLiteral("\nO professor lançou %1 das %2 aulas do semestre.")
                    .arg(f->aulasComRegistro).arg(f->aulasPelaCH);
    }
    if (semRegistro > 0) {
        dica += QStringLiteral("\n%1 dia(s) da lista estão como “Não Registrada”.")
                    .arg(semRegistro);
    }
    it->setToolTip(dica);
    return it;
}

}  // namespace

QStandardItemModel* modeloAgenda(const Snapshot& s, QDate inicio, QDate fim, QDate hoje,
                                 QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Turma"), QStringLiteral("Aula"),
                               QStringLiteral("Horário"), QStringLiteral("Material"),
                               QStringLiteral("Faltas")});
    if (!inicio.isValid() || !fim.isValid()) return m;

    // Índice por turma: a agenda repete a mesma turma em vários dias da semana,
    // e varrer o vetor a cada linha seria O(dias x turmas x frequências).
    QHash<QString, const Frequencia*> freq;
    for (const auto& f : s.frequencias) {
        freq.insert(QString::fromStdString(f.idTurma), &f);
    }

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
                                a.materiais),
                           celulaFaltas(freq.value(a.idTurma, nullptr))});
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
            grupo->appendRow({vazio, item(QString()), item(QString()), item(QString()),
                              item(QString())});
        }
        m->appendRow({grupo, item(QString()), item(QString()), item(QString()),
                      item(QString())});
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

    // "✓ offline" em vez de um "✓" solto: a marca sozinha obriga a subir até o
    // cabeçalho para lembrar o que ela quer dizer, e o distintivo tem espaço
    // para a palavra que dispensa essa viagem.
    auto marcaOffline = [&offline](const QString& id) {
        auto* it = item(offline.contains(id) ? QStringLiteral("✓ offline") : QString());
        if (offline.contains(id)) {
            it->setData(static_cast<int>(Tom::Sucesso), PapelDistintivo);
        }
        return it;
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
            aula->appendRow({filho, item(tipo), marcaOffline(id)});
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
                              marcaOffline(s->data(PapelIdArquivo).toString())});
        }
        m->appendRow({grupo, item(QString()), item(QString())});
    }

    return m;
}

QStandardItemModel* modeloPresenca(const std::vector<frequencia::DiaEfetivo>& dias,
                                   QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Data"), QStringLiteral("Situação"),
                               QStringLiteral("Origem")});

    for (const auto& d : dias) {
        auto* data = item(formatarData(d.data), QString::fromStdString(d.data.toIso()));
        data->setData(QString::fromStdString(d.data.toIso()), PapelDataDia);

        QString texto;
        Tom tom = Tom::Nenhum;
        switch (d.situacao) {
            case SituacaoDia::Presente:
                texto = QStringLiteral("Presente");
                break;
            case SituacaoDia::Falta:
                texto = QStringLiteral("%1 falta(s)").arg(d.faltas);
                tom = Tom::Atrasado;
                break;
            case SituacaoDia::NaoRegistrada:
                texto = QStringLiteral("Não registrada");
                // Urgente, e não apagado: um dia em branco NÃO é um dia
                // tranquilo. É o dia em que o aluno ainda pode registrar que
                // esteve lá, e o único em que a aba serve para alguma coisa.
                tom = Tom::Urgente;
                break;
        }
        auto* situacao = item(texto, static_cast<int>(d.situacao));
        if (tom != Tom::Nenhum) situacao->setData(static_cast<int>(tom), PapelDistintivo);

        QString origem;
        QString dica;
        switch (d.estado) {
            case frequencia::EstadoDia::DoSigaa:
                origem = QStringLiteral("diário do professor");
                break;
            case frequencia::EstadoDia::NaoRegistrada:
                origem = QStringLiteral("o professor não lançou");
                dica = QStringLiteral(
                    "Não quer dizer que você faltou — quer dizer que o diário "
                    "não foi aberto neste dia. Use “Marcar presença” para "
                    "guardar o seu registro.");
                break;
            case frequencia::EstadoDia::MarcadaPeloAluno:
                origem = QStringLiteral("✎ você registrou");
                dica = QStringLiteral(
                    "Seu registro pessoal. Ele NÃO altera a contagem de faltas "
                    "do SIGAA — vale como prova para uma conversa com o "
                    "professor ou a secretaria.");
                break;
            case frequencia::EstadoDia::Conflitada:
                origem = QStringLiteral("⚠ difere do seu registro");
                dica = QStringLiteral(
                    "O professor lançou este dia diferente do que você tinha "
                    "registrado. A dele é a que vale no sistema; a sua está no "
                    "histórico, com a data em que você a fez.");
                break;
        }
        auto* col = item(origem, static_cast<int>(d.estado));
        if (d.estado == frequencia::EstadoDia::MarcadaPeloAluno) {
            col->setData(static_cast<int>(Tom::Sucesso), PapelDistintivo);
        } else if (d.estado == frequencia::EstadoDia::Conflitada) {
            col->setData(static_cast<int>(Tom::Atrasado), PapelDistintivo);
        } else if (d.estado == frequencia::EstadoDia::DoSigaa) {
            col->setForeground(QBrush(cor::apagado()));
        }
        if (!dica.isEmpty()) col->setToolTip(dica);
        if (!d.nota.empty()) data->setToolTip(umaLinha(d.nota));

        m->appendRow({data, situacao, col});
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

QStandardItemModel* modeloParticipantes(const std::vector<Participante>& participantes,
                                        QObject* pai) {
    auto* m = novoModelo(pai, {QStringLiteral("Papel"), QStringLiteral("Nome"),
                               QStringLiteral("Curso / Departamento"),
                               QStringLiteral("Matrícula"), QStringLiteral("E-mail")});

    // O avatar é SEMPRE as iniciais. O app já mostrou o retrato de cada
    // colega, lido de um cache em disco — e alimentar esse cache significava
    // copiar a foto de 31 pessoas para a máquina do aluno. Removido em
    // 18/09/2026: o que a tela precisa é deixar alguém reconhecível de
    // relance, e um quadrado colorido com as iniciais faz isso sem carregar
    // dado pessoal de ninguém. De quebra, funciona para quem nunca cadastrou
    // retrato — 13 dos 32 na captura de rede.

    for (const auto& p : participantes) {
        const bool docente = p.papel == PapelParticipante::Docente;

        // A chave de ordenação leva o papel na frente do nome, nas DUAS
        // primeiras colunas. Sem isso, clicar em "Nome" espalharia o professor
        // no meio da lista alfabética dos colegas — e ele é quem a pessoa
        // procura primeiro nesta tela.
        const QString papel = docente ? QStringLiteral("Professor")
                                      : QStringLiteral("Aluno");
        const QString nome = umaLinha(p.nome);
        const QString ordem = (docente ? QStringLiteral("0") : QStringLiteral("1")) + nome;

        auto* colPapel = item(papel, ordem);
        auto* colNome = item(nome, ordem);

        // O avatar entra na célula do NOME, e não numa coluna própria: numa
        // tabela ordenável, uma coluna de imagem sem texto não tem por que ser
        // clicada e ainda ganharia um cabeçalho vazio. Colado no nome, ele faz
        // o que a pessoa quer aqui — reconhecer o colega de relance.
        colNome->setIcon(QIcon(avatarDe(nome)));

        if (docente && !p.formacao.empty()) {
            // A formação não ganha coluna: é o único campo que só o docente
            // tem, e uma coluna vazia em 31 de 32 linhas custa mais largura do
            // que informa. No tooltip ela continua a um passo do mouse.
            colNome->setToolTip(umaLinha(p.formacao));
        }

        m->appendRow({
            colPapel,
            colNome,
            item(umaLinha(docente ? p.departamento : p.curso)),
            item(umaLinha(p.matricula)),
            item(umaLinha(p.email)),
        });
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
