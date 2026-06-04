#pragma once
#include "model/Solution.hpp"
#include "model/Vessel.hpp"
#include "model/Container.hpp"

// Avaliação de navegabilidade (estabilidade) de um plano de estiva contra os
// dados hidrostáticos do navio, seguindo Larsen & Pacino (2021), Eqs. (21)–(33).
//
// Quatro verificações definem a flag "seaworthy" (navegável):
//   • LCG  (Eq. 26) — centro de gravidade longitudinal: controla o trim
//   • TCG  (Eq. 29) — centro de gravidade transversal: controla a banda (heel)
//   • shear(Eq. 32) — esforço cortante acumulado por bay
//   • bend (Eq. 33) — momento fletor acumulado por bay
//
// Seguindo o próprio ALNS de L&P, NENHUMA é rejeição rígida: cada uma é uma
// penalidade SOFT (o termo ϑ deles) com peso alto, e a busca caminha para a
// viabilidade tolerando violação temporária. O check reporta cada violação como
// um excesso normalizado; o Evaluator os pondera no fitness de busca. A flag de
// navegabilidade é reportada ao lado do objetivo de 9 KPIs.
//
// VCG / altura metacêntrica não está entre as quatro: o artigo minimiza o momento
// vertical (vm) de forma soft no objetivo (nosso KPI 5). GM é reportado aqui só
// para informação e nunca causa inviabilidade.
struct StabilityReport {
    bool feasible = true;       // true sse as quatro verificações passam

    double displacement = 0.0;  // w^disp — peso leve + carga (toneladas)
    int    hydro_index  = 0;    // ponto hidrostático ativo (Eq. 24)

    bool lcg_ok   = true;
    bool tcg_ok   = true;
    bool shear_ok = true;
    bool bend_ok  = true;

    // Diagnóstico: cada momento e a janela [lo, hi] em que deve cair.
    double lm = 0.0, lcg_lo = 0.0, lcg_hi = 0.0;  // momento longitudinal + limites
    double tm = 0.0, tcg_lo = 0.0, tcg_hi = 0.0;  // momento transversal + limites
    double gm = 0.0;                              // altura metacêntrica (só informação)

    // Excessos de restrição normalizados (0 sse satisfeita), cada um a quantidade
    // que o momento/força ultrapassa sua janela, escalada a uma fração comparável.
    // São penalizados de forma soft no fitness de busca (Evaluator), nunca rejeitados.
    double lcg_violation   = 0.0;   // fração da meia-janela de LCG excedida
    double tcg_violation   = 0.0;   // fração da meia-banda de TCG excedida
    double shear_violation = 0.0;   // Σ_bays excesso / faixa de cortante
    double bend_violation  = 0.0;   // Σ_bays excesso / limite de flexão
    double violation       = 0.0;   // soma das quatro (diagnóstico geral)
};

namespace Stability {
    StabilityReport check(const Solution& solution, const Vessel& vessel,
                          const Loadlist& loadlist);
}
