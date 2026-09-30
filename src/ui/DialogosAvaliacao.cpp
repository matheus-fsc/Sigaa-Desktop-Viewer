#include "ui/DialogosAvaliacao.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimeEdit>
#include <QVBoxLayout>

#include "ui/Copiar.h"
#include "ui/Distintivos.h"
#include "ui/Modelos.h"
#include "ui/Tema.h"

namespace sigaa::ui {
namespace {

QDate paraQDate(const DateTime& d) {
    return d.valid() ? QDate(d.year, d.month, d.day) : QDate();
}

DateTime deQt(const QDate& d, const QTime& t, bool comHora) {
    DateTime r;
    if (!d.isValid()) return r;
    r.year = d.year();
    r.month = d.month();
    r.day = d.day();
    if (comHora && t.isValid()) {
        r.hour = t.hour();
        r.minute = t.minute();
        r.hasTime = true;
    }
    return r;
}

QString frase(avaliacao::TipoMudanca t) {
    using T = avaliacao::TipoMudanca;
    switch (t) {
        case T::SigaaMarcou:    return QStringLiteral("o SIGAA anunciou");
        case T::SigaaRemarcou:  return QStringLiteral("o professor remarcou");
        case T::AlunoCorrigiu:  return QStringLiteral("você corrigiu");
        case T::AlunoConfirmou: return QStringLiteral("você confirmou");
        case T::AlunoCriou:     return QStringLiteral("você cadastrou");
        case T::AlunoDesfez:    return QStringLiteral("você desfez a correção");
        case T::AlunoDescartou: return QStringLiteral("você disse que não é prova");
        case T::SigaaAtropelou: return QStringLiteral("o SIGAA substituiu a sua data");
    }
    return {};
}

// "2026-09-27" -> "27/09/2026". O histórico guarda ISO porque é o que ordena;
// a tela mostra o formato que se lê.
QString dataLegivel(const std::string& iso) {
    if (iso.empty()) return QStringLiteral("—");
    const QString s = QString::fromStdString(iso);
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    const QDate d = QDate::fromString(s.left(10), Qt::ISODate);
    return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : s;
}

} // namespace

// ---------------------------------------------------------------------------
// DialogoAvaliacao
// ---------------------------------------------------------------------------

DialogoAvaliacao::DialogoAvaliacao(const avaliacao::Efetiva& atual, QWidget* pai)
    : QDialog(pai), criando_(false), atual_(atual) {
    montar();
}

DialogoAvaliacao::DialogoAvaliacao(const std::vector<Turma>& turmas, QWidget* pai)
    : QDialog(pai), criando_(true), turmas_(turmas) {
    montar();
}

void DialogoAvaliacao::montar() {
    setWindowTitle(criando_ ? QStringLiteral("Nova prova")
                            : QStringLiteral("Corrigir data da prova"));
    setModal(true);

    auto* raiz = new QVBoxLayout(this);
    raiz->setSpacing(tema::esp(3));
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));

    auto* titulo = new QLabel(
        criando_ ? QStringLiteral("Cadastrar uma prova que o SIGAA não tem")
                 : QString::fromStdString(atual_.av.descricao),
        this);
    titulo->setFont(tema::fonte(tema::Papel::Subtitulo));
    titulo->setWordWrap(true);
    raiz->addWidget(titulo);

    if (!criando_) {
        auto* sub = new QLabel(QString::fromStdString(atual_.av.turmaNome), this);
        sub->setFont(tema::fonte(tema::Papel::Legenda));
        QPalette p = sub->palette();
        p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
        sub->setPalette(p);
        raiz->addWidget(sub);
    }

    auto* form = new QFormLayout;
    form->setSpacing(tema::esp(3));
    form->setLabelAlignment(Qt::AlignLeft);

    if (criando_) {
        turma_ = new QComboBox(this);
        for (const auto& t : turmas_) {
            turma_->addItem(QString::fromStdString(t.nome),
                            QString::fromStdString(t.idTurma));
        }
        form->addRow(QStringLiteral("Turma"), turma_);

        descricao_ = new QLineEdit(this);
        descricao_->setPlaceholderText(QStringLiteral("Prova 2, Trabalho final…"));
        form->addRow(QStringLiteral("Prova"), descricao_);
    }

    data_ = new QDateEdit(this);
    data_->setCalendarPopup(true);
    data_->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    const QDate deOnde = paraQDate(atual_.av.quando);
    data_->setDate(deOnde.isValid() ? deOnde : QDate::currentDate());
    form->addRow(QStringLiteral("Data"), data_);

    // Hora OPCIONAL, e é uma decisão de honestidade: o SIGAA anuncia muita
    // prova só com o dia, e um campo de hora sempre preenchido inventaria
    // "00:00" — que o .ics exportaria como "prova à meia-noite".
    auto* linhaHora = new QWidget(this);
    auto* lh = new QHBoxLayout(linhaHora);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->setSpacing(tema::esp(2));
    temHora_ = new QCheckBox(QStringLiteral("tem horário"), linhaHora);
    hora_ = new QTimeEdit(linhaHora);
    hora_->setDisplayFormat(QStringLiteral("HH:mm"));
    temHora_->setChecked(atual_.av.quando.hasTime);
    hora_->setEnabled(atual_.av.quando.hasTime);
    if (atual_.av.quando.hasTime) {
        hora_->setTime(QTime(atual_.av.quando.hour, atual_.av.quando.minute));
    } else {
        hora_->setTime(QTime(8, 0));
    }
    connect(temHora_, &QCheckBox::toggled, hora_, &QWidget::setEnabled);
    lh->addWidget(temHora_);
    lh->addWidget(hora_);
    lh->addStretch();
    form->addRow(QStringLiteral("Horário"), linhaHora);

    nota_ = new QPlainTextEdit(this);
    nota_->setPlaceholderText(
        QStringLiteral("Por que esta data? \"adiada em sala, confirmei com o monitor\""));
    nota_->setMaximumHeight(72);
    nota_->setPlainText(QString::fromStdString(atual_.nota));
    form->addRow(QStringLiteral("Nota"), nota_);

    raiz->addLayout(form);

    if (!criando_ && atual_.quandoSigaa.valid()) {
        // O que o SIGAA diz, à vista enquanto se digita: é a informação que
        // torna a correção conferível em vez de um palpite.
        auto* ref = new QLabel(
            QStringLiteral("O SIGAA anuncia %1. Sua correção passa a valer no app, "
                           "no calendário e no .ics — até o professor atualizar a "
                           "plataforma, e aí a dele volta a valer.")
                .arg(dataLegivel(atual_.quandoSigaa.toIso())),
            this);
        ref->setWordWrap(true);
        ref->setFont(tema::fonte(tema::Papel::Legenda));
        ref->setProperty("classe", QStringLiteral("recado"));
        raiz->addWidget(ref);
    }

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                        this);
    botoes->button(QDialogButtonBox::Save)->setText(QStringLiteral("Salvar"));
    botoes->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    botoes->button(QDialogButtonBox::Save)->setDefault(true);
    connect(botoes, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes);

    if (criando_) {
        // Sem descrição não há chave, e sem chave a prova não tem identidade
        // no banco. Desabilitar é mais honesto que aceitar e falhar depois.
        auto* salvar = botoes->button(QDialogButtonBox::Save);
        salvar->setEnabled(false);
        connect(descricao_, &QLineEdit::textChanged, salvar,
                [salvar, this] { salvar->setEnabled(!descricao_->text().trimmed().isEmpty()); });
    }
}

avaliacao::Ajuste DialogoAvaliacao::resultado() const {
    avaliacao::Ajuste a;
    if (criando_) {
        a.idTurma = turma_ ? turma_->currentData().toString().toStdString() : "";
        a.turmaNome = turma_ ? turma_->currentText().toStdString() : "";
        a.descricao = descricao_ ? descricao_->text().trimmed().toStdString() : "";
        a.criadaPeloAluno = true;
    } else {
        a.idTurma = atual_.av.idTurma;
        a.descricao = atual_.av.descricao;
        a.turmaNome = atual_.av.turmaNome;
        // Corrigir a data de uma inferida é uma forma mais forte de confirmar:
        // quem digitou a data não precisa ser perguntado se confia nela.
        a.confirmada = true;
    }

    a.quando = deQt(data_->date(), hora_->time(), temHora_->isChecked());
    if (temHora_->isChecked()) {
        a.horarioBruto = hora_->time().toString(QStringLiteral("HH:mm")).toStdString();
    }
    a.nota = nota_->toPlainText().trimmed().toStdString();
    a.quandoSigaaNaEpoca = atual_.quandoSigaa;
    a.ativo = true;
    return a;
}

// ---------------------------------------------------------------------------
// DialogoHistorico
// ---------------------------------------------------------------------------

std::vector<LinhaHistorico> paraHistorico(const std::vector<avaliacao::Mudanca>& ms) {
    std::vector<LinhaHistorico> out;
    out.reserve(ms.size());
    for (const auto& m : ms) {
        LinhaHistorico l;
        l.quando = QDateTime::fromSecsSinceEpoch(m.quando)
                       .toString(QStringLiteral("dd/MM/yyyy HH:mm"));
        l.onde = QStringLiteral("%1 · %2")
                     .arg(QString::fromStdString(m.turmaNome),
                          QString::fromStdString(m.descricao));
        l.oQue = frase(m.tipo);
        l.mudanca = m.de.empty() ? dataLegivel(m.para)
                                 : dataLegivel(m.de) + QStringLiteral("  →  ") +
                                       dataLegivel(m.para);
        l.nota = QString::fromStdString(m.nota);
        l.alarme = m.tipo == avaliacao::TipoMudanca::SigaaAtropelou;
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<LinhaHistorico> paraHistorico(const std::vector<frequencia::Mudanca>& ms) {
    std::vector<LinhaHistorico> out;
    out.reserve(ms.size());
    for (const auto& m : ms) {
        LinhaHistorico l;
        l.quando = QDateTime::fromSecsSinceEpoch(m.quando)
                       .toString(QStringLiteral("dd/MM/yyyy HH:mm"));
        l.onde = dataLegivel(m.data.toIso());
        switch (m.tipo) {
            case frequencia::TipoMudanca::AlunoMarcou:
                l.oQue = QStringLiteral("você registrou"); break;
            case frequencia::TipoMudanca::AlunoDesfez:
                l.oQue = QStringLiteral("você removeu o registro"); break;
            case frequencia::TipoMudanca::SigaaRegistrou:
                l.oQue = QStringLiteral("o professor lançou"); break;
            case frequencia::TipoMudanca::SigaaAtropelou:
                l.oQue = QStringLiteral("o professor lançou diferente"); break;
        }
        l.mudanca = QStringLiteral("%1  →  %2")
                        .arg(QString::fromStdString(m.de),
                             QString::fromStdString(m.para));
        l.nota = QString::fromStdString(m.nota);
        l.alarme = m.tipo == frequencia::TipoMudanca::SigaaAtropelou;
        out.push_back(std::move(l));
    }
    return out;
}

DialogoHistorico::DialogoHistorico(const std::vector<LinhaHistorico>& linhas,
                                   const QString& titulo, const QString& vazio,
                                   QWidget* pai)
    : QDialog(pai) {
    setWindowTitle(titulo);
    setModal(true);
    resize(720, 420);

    auto* raiz = new QVBoxLayout(this);
    raiz->setSpacing(tema::esp(3));
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));

    auto* cabecalho = new QLabel(titulo, this);
    cabecalho->setFont(tema::fonte(tema::Papel::Subtitulo));
    cabecalho->setWordWrap(true);
    raiz->addWidget(cabecalho);

    if (linhas.empty()) {
        // Vazio EXPLICADO, não uma tabela em branco: "nada aconteceu" e "o app
        // não registrou nada" são estados diferentes para quem está olhando.
        auto* l = new QLabel(vazio, this);
        l->setWordWrap(true);
        raiz->addWidget(l);
        raiz->addStretch();
    } else {
        auto* m = new QStandardItemModel(0, 4, this);
        m->setHorizontalHeaderLabels({QStringLiteral("Quando"), QStringLiteral("Onde"),
                                      QStringLiteral("O que"),
                                      QStringLiteral("Mudança")});
        for (const auto& linha : linhas) {
            auto* quando = new QStandardItem(linha.quando);
            auto* onde = new QStandardItem(linha.onde);
            auto* oQue = new QStandardItem(linha.oQue);
            auto* mudanca = new QStandardItem(linha.mudanca);
            for (auto* i : {quando, onde, oQue, mudanca}) i->setEditable(false);
            if (linha.alarme) {
                oQue->setData(static_cast<int>(Tom::Atrasado), PapelDistintivo);
            }
            if (!linha.nota.isEmpty()) mudanca->setToolTip(linha.nota);
            m->appendRow({quando, onde, oQue, mudanca});
        }

        auto* tv = new QTableView(this);
        tv->setModel(m);
        tv->setSelectionBehavior(QAbstractItemView::SelectRows);
        tv->setAlternatingRowColors(true);
        tema::ajustarLista(tv);
        habilitarCopia(tv);
        tv->setItemDelegate(new DelegadoDistintivo(tv));
        tv->resizeColumnsToContents();
        tema::esticarColuna(tv, 3);
        raiz->addWidget(tv, 1);
    }

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Close, this);
    botoes->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fechar"));
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(botoes, &QDialogButtonBox::accepted, this, &QDialog::accept);
    raiz->addWidget(botoes);
}

// ---------------------------------------------------------------------------
// Datas a confirmar
// ---------------------------------------------------------------------------

namespace {

// "qua 07/10", sem o ponto do locale.
QString diaCurtoDialogo(QDate d) {
    QString dia = QLocale(QLocale::Portuguese, QLocale::Brazil).toString(d, QStringLiteral("ddd"));
    dia.remove(QLatin1Char('.'));
    return dia + d.toString(QStringLiteral(" dd/MM"));
}

QString distancia(QDate d) {
    const qint64 n = QDate::currentDate().daysTo(d);
    if (n == 0) return QStringLiteral("hoje");
    if (n == 1) return QStringLiteral("amanhã");
    if (n == -1) return QStringLiteral("ontem");
    return n > 0 ? QStringLiteral("em %1 dias").arg(n) : QStringLiteral("há %1 dias").arg(-n);
}

QPushButton* botao(const QString& texto, const char* papel, const QString& dica, QWidget* pai) {
    auto* b = new QPushButton(texto, pai);
    b->setProperty("papel", papel);
    b->setToolTip(dica);
    b->setAutoDefault(false);   // Enter no diálogo não pode confirmar uma prova ao acaso
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Uma linha: data e distância | descrição e turma | ações. Um QFrame com
// filete embaixo, como as linhas das listas do app.
QFrame* linha(QWidget* pai) {
    auto* f = new QFrame(pai);
    f->setObjectName(QStringLiteral("linhaConfirmar"));
    return f;
}

QLabel* rotulo(const QString& texto, tema::Papel papel, bool negrito, bool apagado,
               QWidget* pai) {
    auto* l = new QLabel(texto, pai);
    QFont f = tema::fonte(papel);
    if (negrito) f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setWordWrap(true);
    if (apagado) l->setProperty("classe", QStringLiteral("nota"));
    return l;
}

} // namespace

DialogoConfirmarDatas::DialogoConfirmarDatas(Acoes acoes, QWidget* pai)
    : QDialog(pai), acoes_(std::move(acoes)) {
    setWindowTitle(QStringLiteral("Datas a confirmar"));
    setModal(true);
    resize(760, 520);

    auto* raiz = new QVBoxLayout(this);
    raiz->setSpacing(tema::esp(3));
    raiz->setContentsMargins(tema::esp(5), tema::esp(5), tema::esp(5), tema::esp(4));

    auto* titulo = new QLabel(QStringLiteral("Datas a confirmar"), this);
    QFont ft = tema::fonte(tema::Papel::Subtitulo);
    ft.setWeight(QFont::Bold);
    titulo->setFont(ft);
    raiz->addWidget(titulo);

    auto* explica = rotulo(
        QStringLiteral("O app deduziu estas datas do título de um tópico de aula — o "
                       "professor não as cadastrou como prova. Confirme as certas, "
                       "corrija a data das erradas e remova o que não é prova. Nada "
                       "disso muda o SIGAA: vale no app, no calendário e no .ics."),
        tema::Papel::Corpo, false, true, this);
    raiz->addWidget(explica);

    // A lista rola; título, explicação e rodapé ficam parados.
    auto* rolagem = new QScrollArea(this);
    rolagem->setWidgetResizable(true);
    rolagem->setFrameShape(QFrame::NoFrame);
    auto* suporte = new QWidget(rolagem);
    suporte->setObjectName(QStringLiteral("listaConfirmar"));
    area_ = new QVBoxLayout(suporte);
    area_->setContentsMargins(0, 0, 0, 0);
    area_->setSpacing(0);
    rolagem->setWidget(suporte);
    raiz->addWidget(rolagem, 1);

    auto* botoes = new QDialogButtonBox(QDialogButtonBox::Close, this);
    botoes->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fechar"));
    // Secundário, não primário: fechar não é a ação desta tela — as ações estão
    // nas linhas. Sozinho na caixa, o Qt o faria botão padrão (azul, e Enter
    // fecharia o diálogo no meio de uma revisão).
    auto* fechar = botoes->button(QDialogButtonBox::Close);
    fechar->setAutoDefault(false);
    fechar->setDefault(false);
    fechar->setIcon(QIcon());
    connect(botoes, &QDialogButtonBox::rejected, this, &QDialog::reject);
    raiz->addWidget(botoes);
}

void DialogoConfirmarDatas::mostrar(const std::vector<avaliacao::Efetiva>& pendentes,
                                    const std::vector<avaliacao::Ajuste>& descartadas) {
    // `deleteLater`, e não `delete`: esta função roda DENTRO do clique de um
    // botão que mora no conteúdo velho. Apagá-lo na hora destruiria o botão no
    // meio do próprio sinal.
    if (conteudo_) {
        area_->removeWidget(conteudo_);
        conteudo_->hide();
        conteudo_->deleteLater();
    }
    conteudo_ = new QWidget(area_->parentWidget());
    auto* lista = new QVBoxLayout(conteudo_);
    lista->setContentsMargins(0, 0, 0, 0);
    lista->setSpacing(0);
    area_->addWidget(conteudo_);

    if (pendentes.empty()) {
        auto* vazio = rotulo(
            QStringLiteral("✓ Nenhuma data esperando confirmação. Todas vieram do "
                           "professor ou já passaram por você."),
            tema::Papel::Corpo, false, false, conteudo_);
        vazio->setContentsMargins(0, tema::esp(3), 0, tema::esp(3));
        lista->addWidget(vazio);
    }

    for (const auto& p : pendentes) {
        auto* f = linha(conteudo_);
        auto* h = new QHBoxLayout(f);
        h->setContentsMargins(0, tema::esp(3), 0, tema::esp(3));
        h->setSpacing(tema::esp(4));

        const QDate d = paraQDate(p.av.quando);
        auto* quando = new QVBoxLayout;
        quando->setSpacing(0);
        quando->addWidget(rotulo(d.isValid() ? diaCurtoDialogo(d) : QStringLiteral("sem data"),
                                 tema::Papel::Corpo, true, false, f));
        quando->addWidget(rotulo(d.isValid() ? distancia(d) : QString(), tema::Papel::Legenda,
                                 false, true, f));
        quando->addStretch();
        auto* colunaData = new QWidget(f);
        colunaData->setLayout(quando);
        colunaData->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("0000 00/00")) +
                                  tema::esp(2));
        h->addWidget(colunaData, 0, Qt::AlignTop);

        // A descrição INTEIRA, sem corte: é lendo o título do tópico que se
        // decide se é prova ("Revisão para a P1" não é).
        auto* texto = new QVBoxLayout;
        texto->setSpacing(2);
        texto->addWidget(rotulo(QString::fromStdString(p.av.descricao), tema::Papel::Corpo,
                                true, false, f));
        texto->addWidget(rotulo(QString::fromStdString(p.av.turmaNome), tema::Papel::Legenda,
                                false, true, f));
        h->addLayout(texto, 1);

        auto* confirmar = botao(QStringLiteral("Confirmar"), "secundario",
                                QStringLiteral("A data está certa. Some o aviso."), f);
        auto* editar = botao(QStringLiteral("Editar data"), "discreto",
                             QStringLiteral("É prova, mas em outro dia ou horário."), f);
        auto* remover = botao(QStringLiteral("Não é prova"), "perigo",
                              QStringLiteral("Tira do app, do calendário e do .ics. Dá para "
                                             "restaurar logo abaixo."),
                              f);
        for (QPushButton* b : {confirmar, editar, remover}) h->addWidget(b, 0, Qt::AlignTop);

        connect(confirmar, &QPushButton::clicked, this, [this, p] { acoes_.confirmar(p); });
        connect(editar, &QPushButton::clicked, this, [this, p] { acoes_.editar(p); });
        connect(remover, &QPushButton::clicked, this, [this, p] { acoes_.descartar(p); });
        lista->addWidget(f);
    }

    // Removidas: a volta de um clique errado. Sem esta seção, "Não é prova"
    // seria definitivo — a prova sumiu, e com ela o único lugar onde clicar.
    if (!descartadas.empty()) {
        auto* secao = new QLabel(
            QStringLiteral("Removidas · %1").arg(descartadas.size()), conteudo_);
        QFont fs = tema::fonte(tema::Papel::Legenda);
        fs.setWeight(QFont::Bold);
        fs.setCapitalization(QFont::AllUppercase);
        fs.setLetterSpacing(QFont::PercentageSpacing, 107);
        secao->setProperty("classe", QStringLiteral("secao"));
        secao->style()->unpolish(secao);
        secao->style()->polish(secao);
        secao->setFont(fs);
        secao->setContentsMargins(0, tema::esp(5), 0, tema::esp(1));
        lista->addWidget(secao);

        for (const auto& aj : descartadas) {
            auto* f = linha(conteudo_);
            auto* h = new QHBoxLayout(f);
            h->setContentsMargins(0, tema::esp(2), 0, tema::esp(2));
            h->setSpacing(tema::esp(4));
            const QDate d = paraQDate(aj.quando);
            const QString onde = QString::fromStdString(aj.turmaNome);
            auto* texto = rotulo(
                QStringLiteral("%1 — %2%3")
                    .arg(d.isValid() ? diaCurtoDialogo(d) : QStringLiteral("sem data"),
                         QString::fromStdString(aj.descricao),
                         onde.isEmpty() ? QString() : QStringLiteral(" · ") + onde),
                tema::Papel::Corpo, false, true, f);
            h->addWidget(texto, 1);
            auto* restaurar = botao(QStringLiteral("Restaurar"), "discreto",
                                    QStringLiteral("Volta para a lista como data a confirmar."),
                                    f);
            h->addWidget(restaurar, 0, Qt::AlignVCenter);
            connect(restaurar, &QPushButton::clicked, this, [this, aj] { acoes_.restaurar(aj); });
            lista->addWidget(f);
        }
    }
    lista->addStretch(1);
}

} // namespace sigaa::ui
