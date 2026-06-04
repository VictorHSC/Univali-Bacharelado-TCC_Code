#include "problem/Evaluator.hpp"
#include "problem/Stability.hpp"
#include <set>
#include <algorithm>
#include <limits>

namespace Evaluator {

// Pesos das penalidades soft sobre as quatro violações de estabilidade
// normalizadas, aplicados apenas ao FITNESS de busca (não ao objetivo reportado).
// Espelham os pesos ϑ do ALNS de L&P, em que o esforço cortante é o mais forte
// (50000/500000) e trim/flexão menores. Altos o bastante para a busca preferir
// fortemente planos navegáveis. Ajustáveis.
static constexpr double W_LCG   =  5000.0;
static constexpr double W_TCG   =  5000.0;
static constexpr double W_SHEAR = 50000.0;   // maior ênfase, conforme L&P
static constexpr double W_BEND  =  5000.0;

// Peso por violação de "peso decresce para cima" no convés (uma célula mais
// pesada sobre uma mais leve, numa pilha acima do convés). Ajustável.
static constexpr double C_LASHING = 50.0;

// Conta, nas pilhas acima do convés, as células mais pesadas que a célula ocupada
// imediatamente abaixo (regra de lashing — restrição H5: contêineres mais pesados
// embaixo). Pilhas abaixo do convés ficam em cell guides e estão isentas.
static int weightInversions(const Solution& solution, const Vessel& vessel,
                            const Loadlist& loadlist) {
    int inversions = 0;
    for (const StackSlot& stack : vessel.stacks) {
        if (!stack.above_deck) continue;
        float below_weight = -1.f;                  // peso da última célula ocupada abaixo
        for (const CellSlot& cell : stack.cells) {  // de baixo para cima
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            float weight = 0.f; bool occupied = false;
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id != EMPTY) {
                    weight += loadlist.types[loadlist.containers[container_id].type_id].weight;
                    occupied = true;
                }
            }
            if (!occupied) continue;
            if (below_weight >= 0.f && weight > below_weight + 1e-3f) ++inversions;
            below_weight = weight;
        }
    }
    return inversions;
}

KPIs compute(const Solution& solution, const Vessel& vessel, const Loadlist& loadlist) {
    KPIs kpis;

    // ── KPI 1: contêineres livres não embarcados (Σ y_t) ─────────────────────
    for (const ContainerInstance& container : loadlist.containers)
        if (!container.is_release && solution.container_cell[container.id] == EMPTY)
            ++kpis.unloaded;

    // ── Passo 1: KPIs por pilha (de baixo para cima) ─────────────────────────
    // KPI 3 (stack overstow), KPI 5 (momento vertical), KPI 7 (pilhas vazias),
    // KPI 8 (uso indevido de reefer), KPI 9 (prêmio por porão).
    for (const StackSlot& stack : vessel.stacks) {
        int  min_dest_below = std::numeric_limits<int>::max();
        bool has_any        = false;   // qualquer contêiner, incluindo release (Eq. 19)

        for (const CellSlot& cell : stack.cells) {     // de baixo para cima
            const CellOccupancy& occupancy = solution.cells[cell.global_id];

            // Congela o mínimo de antes desta célula para que os dois slots (fore/
            // aft) comparem contra os contêineres ABAIXO, não entre si.
            int min_below_here = min_dest_below;

            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id == EMPTY) continue;

                const ContainerInstance& container = loadlist.containers[container_id];
                const ContainerType&     type      = loadlist.types[container.type_id];

                // KPI 3: overstow se o destino deste contêiner for mais tardio que
                //        o destino mais cedo do que já está estivado abaixo.
                if (container.dest_port > min_below_here)
                    ++kpis.stack_overstow;

                // KPI 5: momento vertical — termo da carga (peso leve somado adiante).
                kpis.vert_moment += type.weight * cell.vcg;

                // KPI 8: célula reefer desperdiçada com contêiner não-reefer.
                if (cell.reefer && !type.is_reefer)
                    ++kpis.reefer_misuse;

                // KPI 9: prêmio por estivar qualquer contêiner abaixo do convés.
                if (!cell.above_deck)
                    kpis.below_score += static_cast<float>(container.port_distance);

                min_dest_below = std::min(min_dest_below, container.dest_port);
                has_any = true;
            }
        }

        // KPI 7 (Eq. 19): a pilha só é "vazia" se não tiver nenhum contêiner
        // (pilhas só com release contam como usadas). Pilhas vazias são premiadas.
        if (!has_any)
            ++kpis.empty_stacks;
    }

    // KPI 5 (Eq. 27): vm também inclui o momento vertical constante do peso leve
    // Σ_b vcg_b · w_b^const. É uma constante por navio (igual para todo plano),
    // incluída para o objetivo bater exatamente com o de L&P.
    for (const BayData& bay : vessel.bays)
        kpis.vert_moment += bay.constant_weight_vcg * bay.constant_weight;

    // ── Passo 2: KPIs por tampa de porão (hatch) ─────────────────────────────
    // KPI 2 (hatch overstow), KPI 6 (pureza de bloco).
    int number_of_hatches = static_cast<int>(vessel.hatch_above_cells.size());
    for (int hatch = 0; hatch < number_of_hatches; ++hatch) {
        // Destino mais cedo entre os contêineres abaixo + portos do bloco de baixo.
        int min_dest_below = std::numeric_limits<int>::max();
        std::set<int> ports_below;
        for (int cell_id : vessel.hatch_below_cells[hatch]) {
            const CellOccupancy& occupancy = solution.cells[cell_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id != EMPTY) {
                    int dest = loadlist.containers[container_id].dest_port;
                    min_dest_below = std::min(min_dest_below, dest);
                    ports_below.insert(dest);
                }
            }
        }

        // Varre as células acima: overstow por célula (Eq. 15) e detecção de release.
        bool release_above = false;
        std::set<int> ports_above;
        for (int cell_id : vessel.hatch_above_cells[hatch]) {
            const CellOccupancy& occupancy = solution.cells[cell_id];
            bool overstows = false;
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id == EMPTY) continue;
                const ContainerInstance& container = loadlist.containers[container_id];
                if (container.is_release)                 release_above = true;
                if (container.dest_port > min_dest_below) overstows = true;   // Eq. 15
                ports_above.insert(container.dest_port);
            }
            // KPI 2: ho_ci é binário por célula (Eq. 36) — conta cada célula no máx. 1x.
            if (overstows) ++kpis.hatch_overstow;
        }

        // Eq. 16: release acima de uma tampa força levantá-la para estivar algo
        // abaixo. Conta só as células livres ABAIXO (que NÓS atribuímos): células
        // release-abaixo foram estivadas em porto anterior e NÃO contam (confirmado
        // contra a Tabela 2 de L&P — o HO deles < a constante de release-abaixo).
        if (release_above)
            for (int cell_id : vessel.hatch_below_cells[hatch]) {
                const CellOccupancy& occupancy = solution.cells[cell_id];
                bool has_free =
                    (occupancy.fore != EMPTY && !loadlist.containers[occupancy.fore].is_release) ||
                    (occupancy.aft  != EMPTY && !loadlist.containers[occupancy.aft ].is_release);
                if (has_free) ++kpis.hatch_overstow;
            }

        // KPI 6 (Eq. 18): portos de descarga distintos em cada bloco, acima e
        // abaixo da tampa. Um bloco puro (1 porto) conta 1; vazio conta 0.
        kpis.block_purity += static_cast<int>(ports_above.size() + ports_below.size());
    }

    // ── Passo 3: makespan entre pares de bays adjacentes ─────────────────────
    // KPI 4 (Eq. 17): maior contagem de contêineres em qualquer janela de par de
    // bays adjacentes. O artigo soma todo x (release incluído), então contamos todos.
    for (auto& [bay1, bay2] : vessel.adjacent_bay_pairs) {
        int count = 0;
        for (const StackSlot& stack : vessel.stacks) {
            if (stack.bay != bay1 && stack.bay != bay2) continue;
            for (const CellSlot& cell : stack.cells) {
                const CellOccupancy& occupancy = solution.cells[cell.global_id];
                for (int half = 0; half < 2; ++half) {
                    int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                    if (container_id != EMPTY)
                        ++count;
                }
            }
        }
        kpis.makespan = std::max(kpis.makespan, count);
    }

    return kpis;
}

void update(Solution& solution, const Vessel& vessel, const Loadlist& loadlist) {
    solution.kpis = compute(solution, vessel, loadlist);

    // Penalidades soft de estabilidade (guia de busca, fora do objetivo).
    StabilityReport report = Stability::check(solution, vessel, loadlist);
    solution.stability_penalty = W_LCG   * report.lcg_violation
                               + W_TCG   * report.tcg_violation
                               + W_SHEAR * report.shear_violation
                               + W_BEND  * report.bend_violation;
    solution.lashing_penalty   = C_LASHING * weightInversions(solution, vessel, loadlist);
    solution.dirty = false;
}

} // namespace Evaluator
