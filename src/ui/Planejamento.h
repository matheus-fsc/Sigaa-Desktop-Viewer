#pragma once
// A janela de Planejamento de estudo, aberta pelo "Carga do período".
//
// Três perguntas, de cima para baixo:
//   - ONDE APERTA: o mapa de pressão, uma coluna por semana com o tempo de
//     estudo planejado contra o tempo livre; as semanas críticas em laranja.
//   - O QUE FAZER: as dicas, geradas pelo núcleo a partir do plano.
//   - QUANDO ESTUDAR O QUÊ: o plano, dia a dia, com um check por sessão.
//
// A conta é toda do núcleo (core/planejamento). Esta janela só lê o banco,
// pede o plano, grava as sessões e mostra.

#include <QDialog>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include "core/planejamento/Planejamento.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace sigaa::ui {

// O mapa de pressão: uma coluna por semana.
class GraficoPressao : public QWidget {
public:
    explicit GraficoPressao(QWidget* pai = nullptr);
    void definir(std::vector<planejamento::SemanaPlano> semanas,
                 std::vector<planejamento::Sessao> sessoes);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<planejamento::SemanaPlano> semanas_;
    std::vector<int> feitosPorSemana_;   // minutos já feitos, por coluna
};

// Horas livres por dia da semana e dificuldade por matéria.
class DialogoPreferenciasEstudo : public QDialog {
public:
    DialogoPreferenciasEstudo(const planejamento::Preferencias& atual,
                              const std::vector<std::pair<std::string, std::string>>& turmas,
                              QWidget* pai = nullptr);
    planejamento::Preferencias preferencias() const { return prefs_; }

private:
    planejamento::Preferencias prefs_;
};

class DialogoPlanejamento : public QDialog {
public:
    struct Entradas {
        std::vector<planejamento::ProvaAlvo> provas;
        std::vector<planejamento::EntregaAlvo> entregas;
        // (idTurma, nome) de todas as turmas, para a dificuldade.
        std::vector<std::pair<std::string, std::string>> turmas;
    };

    explicit DialogoPlanejamento(Entradas e, QWidget* pai = nullptr);

private:
    // Lê o banco, recalcula, grava e redesenha tudo.
    void replanejar();
    void mostrarDicas();
    void mostrarPlano();
    void atualizarResumo();
    void abrirPreferencias();
    void marcar(size_t i, bool feita);

    Entradas entradas_;
    planejamento::Preferencias prefs_;
    planejamento::Plano plano_;

    QLabel* resumo_{nullptr};
    GraficoPressao* grafico_{nullptr};
    QScrollArea* rolagemGrafico_{nullptr};
    QVBoxLayout* areaDicas_{nullptr};
    QWidget* conteudoDicas_{nullptr};
    QVBoxLayout* areaPlano_{nullptr};
    QWidget* conteudoPlano_{nullptr};
};

} // namespace sigaa::ui
