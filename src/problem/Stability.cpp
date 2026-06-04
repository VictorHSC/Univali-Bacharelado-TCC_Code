#include "problem/Stability.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Stability {

// Convenções de nomes (acrônimos navais mantidos):
//   lm  = momento longitudinal,   lcg = centro de gravidade longitudinal (trim)
//   tm  = momento transversal,    tcg = centro de gravidade transversal (banda/heel)
//   vm  = momento vertical,       vcg = centro de gravidade vertical (GM)
//   w_disp = deslocamento (peso total),  w_bay = peso total do bay

StabilityReport check(const Solution& solution, const Vessel& vessel,
                      const Loadlist& loadlist) {
    StabilityReport report;

    const std::vector<HydroPoint>& hydro_points = vessel.hydrostatic_points;
    const std::vector<BayData>&    bays         = vessel.bays;
    if (hydro_points.empty() || bays.empty())
        return report;   // sem dados de estabilidade → nada a verificar

    // ── Peso da carga por bay e momento transversal (Eqs. 21, 28) ────────────
    int max_bay = 0;
    for (const BayData& bay : bays) max_bay = std::max(max_bay, bay.index);
    std::vector<double> bay_cargo(max_bay + 1, 0.0);

    double tm = 0.0;   // momento transversal Σ tcg_s · w_t · x
    for (const StackSlot& stack : vessel.stacks) {
        for (const CellSlot& cell : stack.cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id == EMPTY) continue;
                double weight = loadlist.types[loadlist.containers[container_id].type_id].weight;
                bay_cargo[stack.bay] += weight;
                tm += stack.tcg * weight;
            }
        }
    }

    // ── Deslocamento e momento longitudinal (Eqs. 22, 25) ────────────────────
    double w_disp = 0.0;   // deslocamento total = peso leve + carga
    double lm     = 0.0;   // momento longitudinal Σ lcg_b · (const + carga)
    for (const BayData& bay : bays) {
        double w_bay = bay.constant_weight + bay_cargo[bay.index];
        w_disp += w_bay;
        lm     += bay.lcg * w_bay;
    }
    report.displacement = w_disp;
    report.lm = lm;
    report.tm = tm;

    // ── Ponto hidrostático ativo (Eqs. 23, 24) ───────────────────────────────
    // Escolhe `active` (≥1) com disp[active-1] ≤ w_disp ≤ disp[active]; limita às pontas.
    int active = 1;
    while (active < (int)hydro_points.size() - 1 && hydro_points[active].displacement < w_disp)
        ++active;
    if ((int)hydro_points.size() == 1) active = 0;
    report.hydro_index = active;
    const HydroPoint& active_point = hydro_points[active];

    // ── Janela de LCG (Eq. 26): lcg_min·disp ≤ lm ≤ lcg_max·disp — restrição hard
    report.lcg_lo = active_point.lcg_min * active_point.displacement;
    report.lcg_hi = active_point.lcg_max * active_point.displacement;
    report.lcg_ok = (lm >= report.lcg_lo && lm <= report.lcg_hi);

    // ── Banda de TCG (Eq. 29): ±tolerância·disp — restrição hard ──────────────
    report.tcg_lo = -vessel.tcg_tolerance * active_point.displacement;
    report.tcg_hi =  vessel.tcg_tolerance * active_point.displacement;
    report.tcg_ok = (tm >= report.tcg_lo && tm <= report.tcg_hi);

    // ── Empuxo no deslocamento ativo (Eqs. 30, 31) ───────────────────────────
    // Interpolação linear entre os dois pontos hidrostáticos que cercam w_disp.
    double fraction = 0.0;
    if (active >= 1) {
        double denom = hydro_points[active].displacement - hydro_points[active - 1].displacement;
        if (denom != 0.0)
            fraction = (hydro_points[active].displacement - w_disp) / denom;
        fraction = std::clamp(fraction, 0.0, 1.0);
    }

    // ── Esforço cortante acumulado (Eq. 32) e momento fletor, vante → ré ──────
    // O cortante no bay é a soma corrente de (peso − empuxo). O momento fletor é a
    // integral da curva de cortante (momento fletor físico em cada seção):
    //   M_b = Σ_{k=1}^{b} shear_{k-1}·(lcg_{k-1} − lcg_k).
    // Isso bate com a magnitude de bend_max no benchmark; o braço escrito na Eq.
    // (33) é relativo à seção local, não à perpendicular de vante.
    double cum_shear  = 0.0;   // força cortante no bay atual
    double cum_bend   = 0.0;   // momento fletor no bay atual
    bool   first      = true;
    double prev_shear = 0.0;
    double prev_lcg   = 0.0;
    double shear_viol = 0.0;   // Σ excesso / limite, acumulado pelos bays
    double bend_viol  = 0.0;
    for (const BayData& bay : bays) {
        double buoyancy = 0.0;
        if (active >= 1 && (int)bay.buoyancy.size() > active)
            buoyancy = fraction * bay.buoyancy[active - 1] + (1.0 - fraction) * bay.buoyancy[active];
        else if ((int)bay.buoyancy.size() > active)
            buoyancy = bay.buoyancy[active];

        if (!first)
            cum_bend += prev_shear * (prev_lcg - bay.lcg);

        double net = bay.constant_weight + bay_cargo[bay.index] - buoyancy;
        cum_shear += net;

        if (cum_shear < bay.shear_min || cum_shear > bay.shear_max) {   // Eq. 32
            report.shear_ok = false;
            double over  = (cum_shear > bay.shear_max) ? cum_shear - bay.shear_max
                                                       : bay.shear_min - cum_shear;
            double range = bay.shear_max - bay.shear_min;
            shear_viol += over / (range > 0.0 ? range : 1.0);
        }
        if (std::abs(cum_bend) > bay.bend_max) {                        // Eq. 33
            report.bend_ok = false;
            bend_viol += (std::abs(cum_bend) - bay.bend_max) /
                         (bay.bend_max > 0.0 ? bay.bend_max : 1.0);
        }

        prev_shear = cum_shear;
        prev_lcg   = bay.lcg;
        first      = false;
    }

    // ── Altura metacêntrica (apenas informativa) ─────────────────────────────
    // KG = momento vertical / deslocamento; GM = KM − KG (KM = metacentro hidrostático).
    double vm = 0.0;
    for (const StackSlot& stack : vessel.stacks)
        for (const CellSlot& cell : stack.cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id != EMPTY)
                    vm += cell.vcg * loadlist.types[loadlist.containers[container_id].type_id].weight;
            }
        }
    for (const BayData& bay : bays)
        vm += bay.constant_weight_vcg * bay.constant_weight;
    if (w_disp > 0.0)
        report.gm = active_point.metacenter - vm / w_disp;

    // ── Violações normalizadas (0 se satisfeita) ─────────────────────────────
    // LCG/TCG são escalados pela meia-largura da sua janela ("frações de meia-janela
    // fora"); cortante/flexão já são somas de frações por bay.
    double lcg_over = std::max({0.0, report.lcg_lo - lm, lm - report.lcg_hi});
    double tcg_over = std::max({0.0, report.tcg_lo - tm, tm - report.tcg_hi});
    double lcg_half = 0.5 * (report.lcg_hi - report.lcg_lo);
    report.lcg_violation   = lcg_over / (lcg_half     > 1.0 ? lcg_half     : 1.0);
    report.tcg_violation   = tcg_over / (report.tcg_hi > 1.0 ? report.tcg_hi : 1.0);
    report.shear_violation = shear_viol;
    report.bend_violation  = bend_viol;
    report.violation = report.lcg_violation + report.tcg_violation
                     + report.shear_violation + report.bend_violation;

    report.feasible = report.lcg_ok && report.tcg_ok && report.shear_ok && report.bend_ok;
    return report;
}

} // namespace Stability
