#include "problem/Repair.hpp"
#include "problem/Stability.hpp"
#include <algorithm>
#include <numeric>
#include <limits>
#include <cmath>

namespace Repair {

// ── Tipos internos ──────────────────────────────────────────────────────────

struct Candidate {
    int   stack_index;  // índice em vessel.stacks
    int   cell_id;      // global_id da célula
    bool  use_aft;      // estivar no slot de ré (true) ou de vante (false)
    float score;        // menor = melhor
};

// Direcionamento (steering) da estiva ciente de equilíbrio. As posições dos
// contêineres são empurradas para que os momentos longitudinal/transversal e o
// esforço cortante correntes sigam a janela navegável — análogo a como o ALNS
// pontua estivas pelo impacto na estabilidade. Com peso 0, a estiva vira o
// guloso só-objetivo.
struct Targets {
    bool   active       = false;
    double weight       = 6.0;    // força do steering de trim (ajustada empiricamente)
    double shear_weight = 8.0;    // força do steering de cortante relativa ao trim (ajustável)
    double disp_final   = 1.0;    // deslocamento final esperado (toneladas)
    double tavg_lcg     = 0.0;    // LCG médio alvo (m por tonelada)
    std::vector<double> bay_lcg;  // índice do bay → LCG (m)
    // Dados de cortante por bay (no deslocamento final esperado), índice → valor:
    std::vector<double> bay_net0; // peso leve − empuxo (contribuição de cortante sem carga)
    std::vector<double> shear_lo; // shear_min
    std::vector<double> shear_hi; // shear_max
};

struct Balance {
    double lm   = 0.0;   // momento longitudinal corrente
    double tm   = 0.0;   // momento transversal corrente
    double disp = 0.0;   // deslocamento corrente
    // Esforço cortante acumulado (vante→ré) em cada bay, dada a carga já estivada
    // + peso leve − empuxo. Permite direcionar estivas pelo seu impacto no cortante.
    std::vector<double> cum_shear;
};

// Constrói os alvos de steering a partir da hidrostática do navio e da loadlist.
static Targets computeTargets(const Vessel& vessel, const Loadlist& loadlist) {
    Targets targets;
    if (vessel.hydrostatic_points.empty() || vessel.bays.empty()) return targets;

    int max_bay = 0;
    for (const BayData& bay : vessel.bays) max_bay = std::max(max_bay, bay.index);
    targets.bay_lcg.assign(max_bay + 1, 0.0);
    for (const BayData& bay : vessel.bays) targets.bay_lcg[bay.index] = bay.lcg;

    double disp = 0.0;
    for (const BayData& bay : vessel.bays) disp += bay.constant_weight;
    for (const ContainerInstance& container : loadlist.containers)
        disp += loadlist.types[container.type_id].weight;
    targets.disp_final = disp > 0.0 ? disp : 1.0;

    // Ponto hidrostático ativo para o deslocamento esperado, depois a janela de LCG.
    const auto& hydro = vessel.hydrostatic_points;
    int active = 1;
    while (active < (int)hydro.size() - 1 && hydro[active].displacement < disp) ++active;
    if ((int)hydro.size() == 1) active = 0;
    double lcg_lo = hydro[active].lcg_min * hydro[active].displacement;
    double lcg_hi = hydro[active].lcg_max * hydro[active].displacement;
    targets.tavg_lcg = 0.5 * (lcg_lo + lcg_hi) / targets.disp_final;  // LCG médio alvo (m/ton)

    // Referência de cortante por bay no deslocamento final esperado: o empuxo é
    // interpolado entre os pontos hidrostáticos vizinhos (como em Stability::check),
    // e bay_net0 = peso leve − empuxo é a contribuição de cortante sem carga.
    double fraction = 0.0;
    if (active >= 1) {
        double denom = hydro[active].displacement - hydro[active - 1].displacement;
        if (denom != 0.0) fraction = (hydro[active].displacement - disp) / denom;
        fraction = std::clamp(fraction, 0.0, 1.0);
    }
    targets.bay_net0.assign(max_bay + 1, 0.0);
    targets.shear_lo.assign(max_bay + 1, 0.0);
    targets.shear_hi.assign(max_bay + 1, 0.0);
    for (const BayData& bay : vessel.bays) {
        double buoyancy = 0.0;
        if (active >= 1 && (int)bay.buoyancy.size() > active)
            buoyancy = fraction * bay.buoyancy[active - 1] + (1.0 - fraction) * bay.buoyancy[active];
        else if ((int)bay.buoyancy.size() > active)
            buoyancy = bay.buoyancy[active];
        targets.bay_net0[bay.index] = bay.constant_weight - buoyancy;
        targets.shear_lo[bay.index] = bay.shear_min;
        targets.shear_hi[bay.index] = bay.shear_max;
    }

    targets.active = true;
    return targets;
}

// Inicializa os momentos correntes a partir do peso leve e de tudo o que já está
// estivado na solução (contêineres release e estivas anteriores).
static Balance initBalance(const Solution& solution, const Vessel& vessel,
                           const Loadlist& loadlist, const Targets& targets) {
    Balance balance;
    int max_bay = static_cast<int>(targets.bay_net0.size()) - 1;
    std::vector<double> bay_cargo(max_bay + 1, 0.0);  // peso da carga por bay até agora

    for (const BayData& bay : vessel.bays) {
        balance.disp += bay.constant_weight;
        balance.lm   += bay.lcg * bay.constant_weight;
    }
    for (const StackSlot& stack : vessel.stacks)
        for (const CellSlot& cell : stack.cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id == EMPTY) continue;
                double weight = loadlist.types[loadlist.containers[container_id].type_id].weight;
                balance.disp += weight;
                balance.lm   += targets.bay_lcg[stack.bay] * weight;
                balance.tm   += stack.tcg * weight;
                bay_cargo[stack.bay] += weight;
            }
        }

    // Cortante acumulado (vante→ré) de peso leve − empuxo + carga já estivada.
    balance.cum_shear.assign(max_bay + 1, 0.0);
    double cum = 0.0;
    for (int bay = 0; bay <= max_bay; ++bay) {
        cum += targets.bay_net0[bay] + bay_cargo[bay];
        balance.cum_shear[bay] = cum;
    }
    return balance;
}

// Penalidade de steering ao estivar peso `weight` no bay `bay` (tcg `stack_tcg`):
//   • trim  — quão longe deixa o CoG longitudinal/transversal do alvo;
//   • shear — impacto local na janela de cortante acumulado naquele bay (penaliza
//             passar de shear_max; premia preencher um déficit de shear_min),
//             espelhando o termo de estabilidade de L&P na pontuação de matching.
static double balancePenalty(const Targets& targets, const Balance& balance,
                             int bay, double stack_tcg, double weight) {
    double new_lm   = balance.lm   + weight * targets.bay_lcg[bay];
    double new_tm   = balance.tm   + weight * stack_tcg;
    double new_disp = balance.disp + weight;
    double trim = (std::abs(new_lm - targets.tavg_lcg * new_disp) + std::abs(new_tm))
                  / targets.disp_final;

    double shear_here = balance.cum_shear[bay];
    double excess   = std::max(0.0, (shear_here + weight) - targets.shear_hi[bay]);  // passou do máx
    double deficit  = std::max(0.0, targets.shear_lo[bay] - shear_here)
                    - std::max(0.0, targets.shear_lo[bay] - (shear_here + weight));  // déficit preenchido
    double range    = targets.shear_hi[bay] - targets.shear_lo[bay];
    double shear    = (range > 1.0) ? (excess - deficit) / range : 0.0;

    return trim + targets.shear_weight * shear;
}

// ── Auxiliares ──────────────────────────────────────────────────────────────

// Peso total de 20 e de 40 pés numa seção de pilha, rastreados separadamente
// porque os dois limites de peso de L&P os ponderam de formas diferentes.
struct StackWeight { float w20 = 0.f; float w40 = 0.f; };

// Restrição rígida H2 (Eqs. 7–8): os dois limites de peso (½-ponderados) valem ao
// mesmo tempo:  ½·W20 + W40 ≤ max40  e  W20 + ½·W40 ≤ max20. Só 4 das 8 fittings de
// canto suportam carga quando há 40 pés, daí a metade do "outro" comprimento.
static bool fitsWeight(const StackSlot& stack, float w20, float w40) {
    return 0.5f * w20 + w40 <= stack.max_weight_40 + 1e-3f
        && w20 + 0.5f * w40 <= stack.max_weight_20 + 1e-3f;
}

// Peso de 20/40 pés por seção, derivado da solução atual.
static std::vector<StackWeight> stackWeights(const Solution& solution, const Vessel& vessel,
                                             const Loadlist& loadlist) {
    std::vector<StackWeight> weights(vessel.stacks.size());
    for (int stack_index = 0; stack_index < static_cast<int>(vessel.stacks.size()); ++stack_index) {
        for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id == EMPTY) continue;
                const ContainerType& type = loadlist.types[loadlist.containers[container_id].type_id];
                if (type.length == 40) weights[stack_index].w40 += type.weight;
                else                   weights[stack_index].w20 += type.weight;
            }
        }
    }
    return weights;
}

// Alturas externas ISO dos contêineres (m): padrão 8'6" vs high-cube 9'6".
static constexpr float HEIGHT_STANDARD  = 2.591f;
static constexpr float HEIGHT_HIGH_CUBE = 2.896f;

// Altura que um contêiner soma à seção. Dois 20 pés dividem um tier, então cada um
// conta meio tier (Eq. de L&P); um 40 pés ocupa um tier inteiro.
static float heightContribution(const ContainerType& type) {
    float h = type.is_highcube ? HEIGHT_HIGH_CUBE : HEIGHT_STANDARD;
    return type.length == 40 ? h : 0.5f * h;
}

// Altura empilhada por seção, derivada da solução atual (restrição rígida H3).
static std::vector<float> stackHeights(const Solution& solution, const Vessel& vessel,
                                       const Loadlist& loadlist) {
    std::vector<float> heights(vessel.stacks.size(), 0.f);
    for (int stack_index = 0; stack_index < static_cast<int>(vessel.stacks.size()); ++stack_index)
        for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            for (int half = 0; half < 2; ++half) {
                int container_id = (half == 0) ? occupancy.fore : occupancy.aft;
                if (container_id != EMPTY)
                    heights[stack_index] += heightContribution(loadlist.types[loadlist.containers[container_id].type_id]);
            }
        }
    return heights;
}

// Ordem de inserção: reefer → 40 pés → maior port_distance. Estivar primeiro os
// contêineres mais restritos evita que sejam espremidos para fora.
static std::vector<int> buildPool(const Solution& solution, const Loadlist& loadlist) {
    std::vector<int> pool;
    for (const ContainerInstance& container : loadlist.containers)
        if (!container.is_release && solution.container_cell[container.id] == EMPTY)
            pool.push_back(container.id);

    std::sort(pool.begin(), pool.end(), [&](int left, int right) {
        const ContainerType& type_left  = loadlist.types[loadlist.containers[left].type_id];
        const ContainerType& type_right = loadlist.types[loadlist.containers[right].type_id];
        if (type_left.is_reefer != type_right.is_reefer) return type_left.is_reefer > type_right.is_reefer;
        if (type_left.length    != type_right.length)    return type_left.length    > type_right.length;  // 40 antes de 20
        return loadlist.containers[left].port_distance > loadlist.containers[right].port_distance;
    });
    return pool;
}

// Pontuação do slot: premia porão para longo curso (KPI 9); penaliza não-reefer em
// célula reefer (KPI 8).
static float slotScore(const CellSlot& cell, const ContainerInstance& container,
                       const ContainerType& type) {
    float score = 0.f;
    if (!cell.above_deck) score -= static_cast<float>(container.port_distance) * 0.5f;
    if (cell.reefer && !type.is_reefer) score += 5.f;
    return score;
}

// Reúne os slots válidos para o contêiner container_id e tenta estivá-lo. Retorna
// true em caso de sucesso; atualiza solution, stack_w e o equilíbrio corrente.
static bool place(int container_id, Solution& solution, const Vessel& vessel, const Loadlist& loadlist,
                  std::vector<StackWeight>& stack_w, std::vector<float>& stack_h,
                  const std::vector<int>& open_stacks, const Targets& targets, Balance& balance,
                  bool use_greedy, std::mt19937* rng) {
    const ContainerInstance& container = loadlist.containers[container_id];
    const ContainerType&     type = loadlist.types[container.type_id];
    bool  is_40    = (type.length == 40);
    float h_contrib = heightContribution(type);

    // Pontuação de objetivo mais a penalidade de steering (trim/banda/cortante).
    auto fullScore = [&](const StackSlot& stack, const CellSlot& cell) -> float {
        float score = slotScore(cell, container, type);
        if (targets.active && targets.weight > 0.0)
            score += static_cast<float>(targets.weight *
                 balancePenalty(targets, balance, stack.bay, stack.tcg, type.weight));
        return score;
    };

    std::vector<Candidate> candidates;

    for (int stack_index : open_stacks) {                       // só pilhas com célula livre
        const StackSlot& stack = vessel.stacks[stack_index];

        // Restrição rígida H2 de peso (Eqs. 7–8).
        float w20 = stack_w[stack_index].w20 + (is_40 ? 0.f : type.weight);
        float w40 = stack_w[stack_index].w40 + (is_40 ? type.weight : 0.f);
        if (!fitsWeight(stack, w20, w40)) continue;

        // Restrição rígida H3 de altura (Eq. 9).
        if (stack_h[stack_index] + h_contrib > stack.max_height) continue;

        for (const CellSlot& cell : stack.cells) {
            // Restrição rígida H4: reefer só pode ir em célula reefer (Eq. 10).
            if (type.is_reefer && !cell.reefer) continue;

            const CellOccupancy& occupancy = solution.cells[cell.global_id];

            if (is_40) {
                // 40 pés precisa de célula totalmente vazia (ocupa fore; aft fica EMPTY).
                if (occupancy.fore == EMPTY && occupancy.aft == EMPTY)
                    candidates.push_back({stack_index, cell.global_id, false, fullScore(stack, cell)});
            } else {
                // 20 pés em fore: o fore da célula precisa estar vazio.
                if (occupancy.fore == EMPTY) {
                    candidates.push_back({stack_index, cell.global_id, false, fullScore(stack, cell)});
                }
                // 20 pés em aft: o fore já tem um 20 pés e o aft está vazio.
                else if (occupancy.aft == EMPTY) {
                    const ContainerType& fore_type =
                        loadlist.types[loadlist.containers[occupancy.fore].type_id];
                    if (fore_type.length == 20)
                        candidates.push_back({stack_index, cell.global_id, true, fullScore(stack, cell)});
                }
            }
        }
    }

    if (candidates.empty()) return false;

    Candidate chosen;
    if (use_greedy) {
        chosen = *std::min_element(candidates.begin(), candidates.end(),
                                   [](const Candidate& a, const Candidate& b) {
                                       return a.score < b.score;
                                   });
    } else {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(candidates.size()) - 1);
        chosen = candidates[dist(*rng)];
    }

    CellOccupancy& occupancy = solution.cells[chosen.cell_id];
    if (chosen.use_aft) occupancy.aft  = container_id;
    else                occupancy.fore = container_id;
    solution.container_cell[container_id] = chosen.cell_id;
    solution.container_aft[container_id]  = chosen.use_aft;
    if (is_40) stack_w[chosen.stack_index].w40 += type.weight;
    else       stack_w[chosen.stack_index].w20 += type.weight;
    stack_h[chosen.stack_index]     += h_contrib;

    if (targets.active) {
        const StackSlot& chosen_stack = vessel.stacks[chosen.stack_index];
        balance.lm   += type.weight * targets.bay_lcg[chosen_stack.bay];
        balance.tm   += type.weight * chosen_stack.tcg;
        balance.disp += type.weight;
        for (int bay = chosen_stack.bay; bay < (int)balance.cum_shear.size(); ++bay)
            balance.cum_shear[bay] += type.weight;          // o peso soma cortante vante→ré
    }
    return true;
}

// Estiva um par de 20 pés livres (container_id1 em fore, container_id2 em aft) numa
// mesma célula vazia. Pareá-los na estiva garante que nunca surge um 20 pés sozinho
// (restrição H1 / Eq. 5). Retorna false se nenhuma célula comporta o par (ambos
// então ficam não embarcados).
static bool placePair(int container_id1, int container_id2, Solution& solution, const Vessel& vessel,
                      const Loadlist& loadlist, std::vector<StackWeight>& stack_w,
                      std::vector<float>& stack_h, const std::vector<int>& open_stacks,
                      const Targets& targets, Balance& balance,
                      bool use_greedy, std::mt19937* rng) {
    const ContainerInstance& container1 = loadlist.containers[container_id1];
    const ContainerInstance& container2 = loadlist.containers[container_id2];
    const ContainerType&     type1  = loadlist.types[container1.type_id];
    const ContainerType&     type2  = loadlist.types[container2.type_id];
    float weight = type1.weight + type2.weight;
    float height = heightContribution(type1) + heightContribution(type2);
    bool  reefer = type1.is_reefer || type2.is_reefer;

    struct PairCand { int stack_index; int cell_id; float score; };
    std::vector<PairCand> candidates;

    for (int stack_index : open_stacks) {                        // só pilhas com célula livre
        const StackSlot& stack = vessel.stacks[stack_index];
        if (!fitsWeight(stack, stack_w[stack_index].w20 + weight, stack_w[stack_index].w40)) continue;  // ambos 20 pés
        if (stack_h[stack_index] + height > stack.max_height)    continue;

        for (const CellSlot& cell : stack.cells) {
            if (reefer && !cell.reefer) continue;                  // par reefer precisa de tomada
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            if (occupancy.fore != EMPTY || occupancy.aft != EMPTY) continue; // par precisa de célula vazia

            float score = slotScore(cell, container1, type1) + slotScore(cell, container2, type2);
            if (targets.active && targets.weight > 0.0)
                score += static_cast<float>(targets.weight *
                     balancePenalty(targets, balance, stack.bay, stack.tcg, weight));
            candidates.push_back({stack_index, cell.global_id, score});
        }
    }

    if (candidates.empty()) return false;

    PairCand chosen;
    if (use_greedy)
        chosen = *std::min_element(candidates.begin(), candidates.end(),
                                   [](const PairCand& a, const PairCand& b) {
                                       return a.score < b.score;
                                   });
    else {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(candidates.size()) - 1);
        chosen = candidates[dist(*rng)];
    }

    CellOccupancy& occupancy = solution.cells[chosen.cell_id];
    occupancy.fore = container_id1;  occupancy.aft = container_id2;
    solution.container_cell[container_id1] = chosen.cell_id;  solution.container_aft[container_id1] = false;
    solution.container_cell[container_id2] = chosen.cell_id;  solution.container_aft[container_id2] = true;
    stack_w[chosen.stack_index].w20 += weight;
    stack_h[chosen.stack_index]     += height;

    if (targets.active) {
        const StackSlot& chosen_stack = vessel.stacks[chosen.stack_index];
        balance.lm   += weight * targets.bay_lcg[chosen_stack.bay];
        balance.tm   += weight * chosen_stack.tcg;
        balance.disp += weight;
        for (int bay = chosen_stack.bay; bay < (int)balance.cum_shear.size(); ++bay)
            balance.cum_shear[bay] += weight;
    }
    return true;
}

// Rede de segurança da restrição H1 / Eq. 5: um 20 pés livre sem par (sozinho no
// fore, aft vazio) é inviável, então é descarregado. O pareamento proativo torna
// isto raro (só sobra um 20 pés ímpar).
static void unloadLoneTwenties(Solution& solution, const Vessel& vessel,
                               const Loadlist& loadlist) {
    for (const StackSlot& stack : vessel.stacks)
        for (const CellSlot& cell : stack.cells) {
            CellOccupancy& occupancy = solution.cells[cell.global_id];
            if (occupancy.fore == EMPTY || occupancy.aft != EMPTY) continue;
            const ContainerInstance& fore = loadlist.containers[occupancy.fore];
            if (fore.is_release) continue;                          // release não pode mover
            if (loadlist.types[fore.type_id].length != 20) continue;   // 40 pés em fore é ok
            solution.container_cell[occupancy.fore] = EMPTY;
            solution.container_aft[occupancy.fore]  = false;
            occupancy.fore = EMPTY;
        }
}

// NOTA: um "unload gate" para LCG/TCG foi testado e removido. Seguindo o próprio
// ALNS de L&P, a estabilidade é tratada como penalidades SOFT (ver Evaluator) mais
// o steering de trim acima, não por descarga: remover carga só move o centro de
// gravidade em direção à massa fixa, então em instâncias de alta utilização (onde
// peso leve + release já está fora da janela de LCG) nunca atinge viabilidade —
// só descarrega de forma destrutiva. A busca minimiza as penalidades.

// Insere cada contêiner de `pool`: 40 pés individualmente, 20 pés em pares para
// nenhum ficar sozinho (Eq. 5). O pool mantém a ordem de prioridade; um 20 pés
// final ímpar (ou par/40 pés sem célula válida) permanece não embarcado.
static void fillPool(const std::vector<int>& pool, Solution& solution,
                     const Vessel& vessel, const Loadlist& loadlist,
                     std::vector<StackWeight>& stack_w, std::vector<float>& stack_h,
                     const Targets& targets, Balance& balance,
                     bool use_greedy, std::mt19937* rng) {
    // Só pilhas com ao menos uma célula totalmente vazia comportam um 40 pés ou um
    // par de 20 pés. Restringir a varredura de candidatas a essas (em vez de todas
    // as pilhas) pula as pilhas já cheias — a maioria depois de um voo — sem mudar
    // o resultado (uma pilha cheia não geraria candidata mesmo).
    std::vector<int> open_stacks;
    open_stacks.reserve(vessel.stacks.size());
    for (int stack_index = 0; stack_index < static_cast<int>(vessel.stacks.size()); ++stack_index)
        for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
            const CellOccupancy& occupancy = solution.cells[cell.global_id];
            if (occupancy.fore == EMPTY && occupancy.aft == EMPTY) { open_stacks.push_back(stack_index); break; }
        }

    std::vector<int> twenties;
    for (int container_id : pool) {
        if (loadlist.types[loadlist.containers[container_id].type_id].length == 20)
            twenties.push_back(container_id);
        else
            place(container_id, solution, vessel, loadlist, stack_w, stack_h, open_stacks, targets, balance, use_greedy, rng);
    }
    for (size_t k = 0; k + 1 < twenties.size(); k += 2)
        placePair(twenties[k], twenties[k + 1], solution, vessel, loadlist,
                  stack_w, stack_h, open_stacks, targets, balance, use_greedy, rng);
}

// ── Interface pública ───────────────────────────────────────────────────────

void greedy(Solution& solution, const Vessel& vessel, const Loadlist& loadlist) {
    auto pool    = buildPool(solution, loadlist);
    auto stack_w = stackWeights(solution, vessel, loadlist);
    auto stack_h = stackHeights(solution, vessel, loadlist);
    Targets targets = computeTargets(vessel, loadlist);
    Balance balance = initBalance(solution, vessel, loadlist, targets);
    fillPool(pool, solution, vessel, loadlist, stack_w, stack_h, targets, balance, true, nullptr);
    unloadLoneTwenties(solution, vessel, loadlist);
    solution.dirty = true;
}

void random(Solution& solution, const Vessel& vessel, const Loadlist& loadlist, std::mt19937& rng) {
    auto pool    = buildPool(solution, loadlist);  // ordem de prioridade mantida; célula sorteada ao acaso
    auto stack_w = stackWeights(solution, vessel, loadlist);
    auto stack_h = stackHeights(solution, vessel, loadlist);
    Targets targets = computeTargets(vessel, loadlist);
    Balance balance = initBalance(solution, vessel, loadlist, targets);
    fillPool(pool, solution, vessel, loadlist, stack_w, stack_h, targets, balance, false, &rng);
    unloadLoneTwenties(solution, vessel, loadlist);
    solution.dirty = true;
}

Solution buildGreedy(const Vessel& vessel, const Loadlist& loadlist) {
    Solution solution;
    solution.cells.resize(vessel.total_cells);
    solution.container_cell.assign(loadlist.containers.size(), EMPTY);
    solution.container_aft.assign(loadlist.containers.size(), false);

    // Restrição H6: fixa os contêineres release nas suas posições antes de estivar.
    for (const ContainerInstance& container : loadlist.containers) {
        if (!container.is_release || container.release_cell_id == EMPTY) continue;
        CellOccupancy& occupancy = solution.cells[container.release_cell_id];
        if (container.release_is_aft) occupancy.aft  = container.id;
        else                          occupancy.fore = container.id;
        solution.container_cell[container.id] = container.release_cell_id;
        solution.container_aft[container.id]  = container.release_is_aft;
    }

    greedy(solution, vessel, loadlist);
    solution.dirty = true;
    return solution;
}

} // namespace Repair
