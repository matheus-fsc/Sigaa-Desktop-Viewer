#include "core/planejamento/Planejamento.h"

#include "core/calendar/Calendario.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

namespace sigaa::planejamento {
namespace {

// Granularidade do plano. Menos que meia hora não é sessão de estudo.
constexpr int kBloco = 30;
// Quantos dias antes da prova o plano começa a olhar.
constexpr int kJanela = 21;

// Dias desde 1970-01-01. Inteiro, e não sys_days, porque o plano faz muita
// conta de "quantos dias até" e um int lê melhor.
int paraDia(const DateTime& d) {
    using namespace std::chrono;
    const sys_days sd{year{d.year} / month{static_cast<unsigned>(d.month)} /
                      day{static_cast<unsigned>(d.day)}};
    return static_cast<int>(sd.time_since_epoch().count());
}

DateTime deDia(int n) {
    using namespace std::chrono;
    const year_month_day ymd{sys_days{days{n}}};
    DateTime d;
    d.year = static_cast<int>(ymd.year());
    d.month = static_cast<int>(static_cast<unsigned>(ymd.month()));
    d.day = static_cast<int>(static_cast<unsigned>(ymd.day()));
    return d;
}

// 0 = segunda. 1970-01-01 foi quinta (3).
int diaDaSemana(int n) { return ((n % 7) + 7 + 3) % 7; }

int segunda(int n) { return n - diaDaSemana(n); }

std::string ddmm(int n) {
    const DateTime d = deDia(n);
    char b[8];
    std::snprintf(b, sizeof b, "%02d/%02d", d.day, d.month);
    return b;
}

const char* nomeDia(int n) {
    static const char* nomes[] = {"segunda", "terça", "quarta", "quinta",
                                  "sexta",   "sábado", "domingo"};
    return nomes[diaDaSemana(n)];
}

// O peso de estudar `k` dias antes da prova. Perto pesa mais, mas não tudo:
// a véspera sozinha não segura uma matéria, e o resto da curva é o que faz o
// plano espaçar a revisão.
double peso(int k) {
    if (k <= 1) return 3.0;
    if (k <= 3) return 2.6;
    if (k <= 7) return 2.0;
    if (k <= 14) return 1.2;
    return 0.6;
}

// Teto da mesma prova num dia só: mais que isso rende pouco, e é melhor
// voltar à matéria no dia seguinte.
int tetoPorDia(Dificuldade d) { return d == Dificuldade::Dificil ? 180 : 120; }

std::string nomeProva(const ProvaAlvo& p) {
    return p.descricao + " de " + p.turmaNome;
}

} // namespace

Dificuldade Preferencias::dificuldadeDe(const std::string& idTurma) const {
    const auto it = dificuldade.find(idTurma);
    return it == dificuldade.end() ? Dificuldade::Media : it->second;
}

std::string Sessao::chaveProva() const { return idTurma + "|" + prova; }

std::string Sessao::chave() const {
    char b[16] = "";
    if (dia.valid()) std::snprintf(b, sizeof b, "%04d-%02d-%02d", dia.year, dia.month, dia.day);
    return chaveProva() + "|" + b;
}

double SemanaPlano::ocupacao() const {
    if (minutosLivres <= 0) return minutosPlanejados > 0 ? 1.0 : 0.0;
    return static_cast<double>(minutosPlanejados) / minutosLivres;
}

bool SemanaPlano::critica() const { return ocupacao() >= 0.8 || provas >= 3; }

int minutosNecessarios(const ProvaAlvo& p, Dificuldade d) {
    // Sem tópicos coletados, uma matéria "média" de 6. Acima de 30 o número
    // diz mais sobre como o professor registra aulas do que sobre a matéria.
    const int topicos = p.topicos < 0 ? 6 : std::min(p.topicos, 30);
    const double fator = d == Dificuldade::Facil     ? 0.7
                         : d == Dificuldade::Dificil ? 1.5
                                                     : 1.0;
    const double bruto = (120.0 + 25.0 * topicos) * fator;
    const int arred = static_cast<int>(std::lround(bruto / kBloco)) * kBloco;
    return std::clamp(arred, 60, 1200);
}

std::string duracao(int minutos) {
    const int h = minutos / 60, m = minutos % 60;
    char b[24];
    if (h == 0) std::snprintf(b, sizeof b, "%d min", m);
    else if (m == 0) std::snprintf(b, sizeof b, "%dh", h);
    else std::snprintf(b, sizeof b, "%dh%02d", h, m);
    return b;
}

std::array<int, 7> minutosDeAulaPorDia(const std::vector<Turma>& turmas) {
    std::array<int, 7> m{};
    for (const auto& t : turmas) {
        for (const auto& b : calendario::lerHorario(t.horario)) {
            for (const int iso : b.dias) {
                if (iso < 1 || iso > 7) continue;
                m[static_cast<size_t>(iso - 1)] +=
                    static_cast<int>(b.horarios.size()) * kMinutosPorHoraAula;
            }
        }
    }
    return m;
}

int minutosParaEstudo(const Preferencias& p, const std::array<int, 7>& aulas, int dia) {
    const auto i = static_cast<size_t>(dia);
    return std::max(0, p.minutosPorDia[i] - aulas[i]);
}

Plano planejar(const std::vector<ProvaAlvo>& provasIn, const std::vector<EntregaAlvo>& entregas,
               const Preferencias& prefs, const std::vector<Sessao>& guardadas,
               const DateTime& hojeDt, const std::array<int, 7>& aulas) {
    Plano plano;
    const int hoje = paraDia(hojeDt);

    // Só as provas que ainda vêm, em ordem: a mais próxima escolhe primeiro,
    // porque é a que tem menos dias para onde fugir.
    std::vector<ProvaAlvo> provas;
    for (const auto& p : provasIn) {
        if (p.data.valid() && paraDia(p.data) >= hoje) provas.push_back(p);
    }
    std::stable_sort(provas.begin(), provas.end(), [](const ProvaAlvo& a, const ProvaAlvo& b) {
        return paraDia(a.data) < paraDia(b.data);
    });

    std::map<int, int> provasNoDia, entregasNoDia;
    for (const auto& p : provas) ++provasNoDia[paraDia(p.data)];
    for (const auto& e : entregas) {
        if (e.prazo.valid() && paraDia(e.prazo) >= hoje) ++entregasNoDia[paraDia(e.prazo)];
    }

    // Tempo livre do dia, já com os descontos de prova e de entrega.
    auto capacidade = [&](int d) {
        double c = minutosParaEstudo(prefs, aulas, diaDaSemana(d));
        if (provasNoDia.count(d)) c *= 0.5;
        if (entregasNoDia.count(d)) c *= 0.75;
        return static_cast<int>(c);
    };

    // --- o que já está guardado --------------------------------------------
    std::map<int, int> usado;                    // minutos tomados por dia
    std::map<std::string, int> feito;            // por chaveProva
    std::set<std::string> diasFeitos;            // chaves de sessões feitas
    int atrasadas = 0, minutosAtrasados = 0;
    for (const auto& s : guardadas) {
        if (!s.dia.valid()) continue;
        const int d = paraDia(s.dia);
        if (s.feita) {
            plano.sessoes.push_back(s);
            feito[s.chaveProva()] += s.minutos;
            diasFeitos.insert(s.chave());
            if (d >= hoje) usado[d] += s.minutos;
        } else if (d < hoje && s.dataProva.valid() && paraDia(s.dataProva) >= hoje) {
            // Ficou para trás e a prova ainda vem: o tempo volta para a conta
            // (a prova continua precisando dele) e o aluno fica sabendo.
            ++atrasadas;
            minutosAtrasados += s.minutos;
        }
    }

    // --- distribuição, prova a prova ----------------------------------------
    std::map<std::string, int> primeiraSessao;   // por chaveProva: primeiro dia
    for (const auto& p : provas) {
        const int alvo = paraDia(p.data);
        const Dificuldade dif = prefs.dificuldadeDe(p.idTurma);
        const std::string chaveP = p.idTurma + "|" + p.descricao;
        int falta = minutosNecessarios(p, dif) - feito[chaveP];
        if (falta <= 0) continue;

        const int inicio = std::max(hoje, alvo - kJanela);
        std::map<int, int> blocos;               // desta prova, por dia
        int blocosFaltando = (falta + kBloco - 1) / kBloco;
        while (blocosFaltando > 0) {
            int melhor = -1;
            double melhorPeso = -1;
            for (int d = inicio; d < alvo; ++d) {
                if (capacidade(d) - usado[d] < kBloco) continue;
                if ((blocos[d] + 1) * kBloco > tetoPorDia(dif)) continue;
                // Um dia em que a prova já tem sessão FEITA fica como está: a
                // chave é uma por prova por dia, e o check não pode sumir.
                Sessao chk;
                chk.idTurma = p.idTurma;
                chk.prova = p.descricao;
                chk.dia = deDia(d);
                if (diasFeitos.count(chk.chave())) continue;

                // O segundo bloco do dia vale o mesmo que o primeiro: sessão
                // de estudo começa em 1h. Do terceiro em diante o peso cai, e
                // o plano prefere voltar à matéria em outro dia.
                const int ja = blocos[d];
                const double pd = peso(alvo - d) / (ja < 2 ? 1.0 : 1.0 + 0.8 * (ja - 1));
                // Estritamente maior: no empate fica o dia mais cedo, que é o
                // lado bom de errar — sobra folga para imprevisto.
                if (pd > melhorPeso + 1e-9) {
                    melhorPeso = pd;
                    melhor = d;
                }
            }
            if (melhor < 0) break;
            ++blocos[melhor];
            usado[melhor] += kBloco;
            --blocosFaltando;
        }
        if (blocosFaltando > 0) plano.deficit[chaveP] = blocosFaltando * kBloco;

        for (const auto& [d, n] : blocos) {
            if (n == 0) continue;
            Sessao s;
            s.idTurma = p.idTurma;
            s.turmaNome = p.turmaNome;
            s.prova = p.descricao;
            s.dataProva = p.data;
            s.dataProva.hasTime = false;
            s.dia = deDia(d);
            s.minutos = n * kBloco;
            plano.sessoes.push_back(std::move(s));
            if (!primeiraSessao.count(chaveP)) primeiraSessao[chaveP] = d;
        }
    }

    std::stable_sort(plano.sessoes.begin(), plano.sessoes.end(),
                     [](const Sessao& a, const Sessao& b) {
                         const int da = paraDia(a.dia), db = paraDia(b.dia);
                         if (da != db) return da < db;
                         return paraDia(a.dataProva) < paraDia(b.dataProva);
                     });

    // --- mapa de pressão ----------------------------------------------------
    int ultima = hoje;
    for (const auto& p : provas) ultima = std::max(ultima, paraDia(p.data));
    for (int w = segunda(hoje); w <= segunda(ultima); w += 7) {
        SemanaPlano sp;
        sp.inicio = deDia(w);
        for (int d = std::max(w, hoje); d < w + 7; ++d) sp.minutosLivres += capacidade(d);
        for (const auto& s : plano.sessoes) {
            const int d = paraDia(s.dia);
            if (d >= w && d < w + 7) sp.minutosPlanejados += s.minutos;
        }
        for (const auto& [d, n] : provasNoDia) {
            if (d >= w && d < w + 7) sp.provas += n;
        }
        for (const auto& [d, n] : entregasNoDia) {
            if (d >= w && d < w + 7) sp.entregas += n;
        }
        plano.semanas.push_back(sp);
    }

    // --- dicas --------------------------------------------------------------
    auto dica = [&](TipoDica t, int prio, std::string texto) {
        plano.dicas.push_back({t, prio, std::move(texto)});
    };

    for (const auto& p : provas) {
        const auto it = plano.deficit.find(p.idTurma + "|" + p.descricao);
        if (it == plano.deficit.end()) continue;
        const int faltam = paraDia(p.data) - hoje;
        if (faltam <= 2) {
            // Em cima da hora, "aumente as horas" não é conselho: não há dias
            // para onde aumentar. O que resta é escolher bem o que estudar.
            dica(TipoDica::Deficit, 100,
                 nomeProva(p) + (faltam == 0   ? " é hoje"
                                 : faltam == 1 ? " é amanhã"
                                               : " é depois de amanhã") +
                     ", e o tempo livre até lá não cobre a matéria toda. Foque no que mais "
                     "cai e em exercícios resolvidos, e durma antes.");
        } else {
            dica(TipoDica::Deficit, 100,
                 nomeProva(p) + " (" + ddmm(paraDia(p.data)) + ") precisa de mais " +
                     duracao(it->second) +
                     " do que cabe no seu tempo livre antes dela. Aumente as horas de "
                     "algum dia ou comece a revisar já.");
        }
    }

    for (const auto& [d, n] : provasNoDia) {
        if (n < 2) continue;
        std::string nomes;
        const ProvaAlvo* maisDificil = nullptr;
        bool empate = false;
        for (const auto& p : provas) {
            if (paraDia(p.data) != d) continue;
            nomes += (nomes.empty() ? "" : " e ") + p.turmaNome;
            const auto dp = prefs.dificuldadeDe(p.idTurma);
            if (!maisDificil || dp > prefs.dificuldadeDe(maisDificil->idTurma)) {
                maisDificil = &p;
                empate = false;
            } else if (dp == prefs.dificuldadeDe(maisDificil->idTurma)) {
                empate = true;
            }
        }
        // Com a dificuldade empatada, apontar "a mais difícil" seria escolher
        // ao acaso. Melhor pedir a informação que falta.
        const std::string vespera =
            empate ? "na véspera, divida o tempo — e se uma delas for mais difícil para "
                     "você, marque em \"Horas e dificuldade\" que o plano dá mais tempo a ela."
                   : "na véspera, dê a maior parte do tempo a " + maisDificil->turmaNome + ".";
        dica(TipoDica::MesmoDia, 90,
             std::to_string(n) + " provas na " + nomeDia(d) + " " + ddmm(d) + " (" + nomes +
                 "). O plano revisa todas nos dias anteriores; " + vespera);
    }

    // Aglomerado: 3+ provas numa janela de 7 dias. Cada grupo uma vez só.
    for (size_t i = 0; i < provas.size();) {
        size_t j = i;
        while (j + 1 < provas.size() &&
               paraDia(provas[j + 1].data) - paraDia(provas[i].data) <= 6) {
            ++j;
        }
        const size_t n = j - i + 1;
        if (n >= 3) {
            int comeco = paraDia(provas[i].data);
            for (size_t k = i; k <= j; ++k) {
                const auto it = primeiraSessao.find(provas[k].idTurma + "|" + provas[k].descricao);
                if (it != primeiraSessao.end()) comeco = std::min(comeco, it->second);
            }
            std::string texto = std::to_string(n) + " provas entre " +
                                ddmm(paraDia(provas[i].data)) + " e " +
                                ddmm(paraDia(provas[j].data)) + ".";
            if (comeco < paraDia(provas[i].data)) {
                texto += " O plano começa a revisar para elas em " + ddmm(comeco) +
                         ": é dali em diante que não dá para deixar sessão para depois.";
            }
            dica(TipoDica::Aglomerado, 80, texto);
            i = j + 1;
        } else {
            ++i;
        }
    }

    if (atrasadas > 0) {
        dica(TipoDica::Atrasadas, 75,
             std::to_string(atrasadas) +
                 (atrasadas == 1 ? " sessão ficou" : " sessões ficaram") + " para trás (" +
                 duracao(minutosAtrasados) + "). O plano redistribuiu esse tempo nos "
                 "próximos dias.");
    }

    for (const auto& e : entregas) {
        if (!e.prazo.valid()) continue;
        const int d = paraDia(e.prazo);
        if (d < hoje) continue;
        for (const auto& p : provas) {
            const int k = paraDia(p.data) - d;
            if (k != 0 && k != 1) continue;
            dica(TipoDica::EntregaNaVespera, 70,
                 "A entrega \"" + e.titulo + "\" (" + e.turmaNome + ") vence em " + ddmm(d) +
                     (k == 0 ? ", no dia da " : ", véspera da ") + nomeProva(p) +
                     ". Termine-a antes da reta final, para a véspera ficar com a revisão.");
            break;
        }
    }

    for (const auto& p : provas) {
        if (!p.inferida || paraDia(p.data) - hoje > 35) continue;
        dica(TipoDica::Deduzida, 60,
             "A data de " + nomeProva(p) + " (" + ddmm(paraDia(p.data)) +
                 ") foi deduzida, não confirmada. Confirme em \"Datas a confirmar\" para o "
                 "plano não mirar o dia errado.");
    }

    for (size_t i = 0; i + 1 < plano.semanas.size(); ++i) {
        const auto& a = plano.semanas[i];
        const auto& b = plano.semanas[i + 1];
        if (a.minutosLivres <= 0 || a.ocupacao() >= 0.4 || !b.critica()) continue;
        const int w = paraDia(a.inicio);
        dica(TipoDica::SemanaLeve, 50,
             "A semana de " + ddmm(w) + " a " + ddmm(w + 6) + " está folgada (" +
                 duracao(a.minutosPlanejados) + " planejadas de " + duracao(a.minutosLivres) +
                 " livres) e a seguinte aperta. Use a folga para adiantar entregas e fazer a "
                 "primeira leitura das matérias da semana seguinte.");
    }

    if (std::any_of(plano.semanas.begin(), plano.semanas.end(),
                    [](const SemanaPlano& s) { return s.critica(); })) {
        dica(TipoDica::Estrategia, 40,
             "Nas semanas críticas: blocos de 50 minutos com pausa, alternando matérias; "
             "revise fazendo exercícios e resumindo de memória, que fixa mais do que reler; "
             "e durma — a véspera de prova não é noite para virar.");
    }

    std::stable_sort(plano.dicas.begin(), plano.dicas.end(),
                     [](const Dica& a, const Dica& b) { return a.prioridade > b.prioridade; });
    return plano;
}

} // namespace sigaa::planejamento
