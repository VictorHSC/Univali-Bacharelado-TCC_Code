#include "algorithm/crow_search/Flight.hpp"
#include "problem/Repair.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Flight {

// Remove todos os contêineres LIVRES da seção de pilha stack_index. Retorna a
// lista de ids despejados para que possam ser reinseridos.
static std::vector<int> evict(Solution& solution, const Vessel& vessel,
                              const Loadlist& loadlist, int stack_index) {
    std::vector<int> evicted;
    for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
        CellOccupancy& occupancy = solution.cells[cell.global_id];
        for (int half = 0; half < 2; ++half) {
            int& ref = (half == 0) ? occupancy.fore : occupancy.aft;
            if (ref == EMPTY) continue;
            if (loadlist.containers[ref].is_release) continue;   // fixo (release) — nunca move
            evicted.push_back(ref);
            solution.container_cell[ref] = EMPTY;
            solution.container_aft[ref]  = false;
            ref = EMPTY;
        }
    }
    return evicted;
}

// Voo aleatório (exploração): destrói FL% das pilhas com carga livre e refaz o
// preenchimento ao acaso. É o operador de diversificação da CSA.
Solution randomFlight(const Solution& solution, const Vessel& vessel, const Loadlist& loadlist,
                      double FL, std::mt19937& rng) {
    Solution candidate = solution;

    // Junta as pilhas que têm ao menos um contêiner livre.
    std::vector<int> occupied;
    for (int stack_index = 0; stack_index < static_cast<int>(vessel.stacks.size()); ++stack_index) {
        for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
            const CellOccupancy& occupancy = candidate.cells[cell.global_id];
            auto has_free = [&](int container_id) {
                return container_id != EMPTY && !loadlist.containers[container_id].is_release;
            };
            if (has_free(occupancy.fore) || has_free(occupancy.aft)) {
                occupied.push_back(stack_index);
                break;
            }
        }
    }

    if (occupied.empty()) return candidate;

    std::shuffle(occupied.begin(), occupied.end(), rng);
    int k = std::max(1, static_cast<int>(std::ceil(FL * occupied.size())));
    k = std::min(k, static_cast<int>(occupied.size()));

    for (int i = 0; i < k; ++i)
        evict(candidate, vessel, loadlist, occupied[i]);

    Repair::random(candidate, vessel, loadlist, rng);
    candidate.dirty = true;
    return candidate;
}

// Seguir outro corvo (exploração de regiões boas): importa FL% das pilhas da
// solução `target` (a memória de um par) para `current` e preenche o resto.
Solution followCrow(const Solution& current, const Solution& target,
                    const Vessel& vessel, const Loadlist& loadlist,
                    double FL, std::mt19937& rng) {
    Solution candidate = current;

    // Escolhe k seções de pilha aleatórias para importar de `target`.
    std::vector<int> indices(vessel.stacks.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);
    int k = std::max(1, static_cast<int>(std::ceil(FL * vessel.stacks.size())));
    k = std::min(k, static_cast<int>(vessel.stacks.size()));

    for (int i = 0; i < k; ++i) {
        int stack_index = indices[i];
        evict(candidate, vessel, loadlist, stack_index);

        // Tenta copiar as atribuições de contêineres livres de `target` nas mesmas células.
        for (const CellSlot& cell : vessel.stacks[stack_index].cells) {
            const CellOccupancy& target_occupancy = target.cells[cell.global_id];
            CellOccupancy&       cand_occupancy   = candidate.cells[cell.global_id];

            for (int half = 0; half < 2; ++half) {
                int target_container_id = (half == 0) ? target_occupancy.fore : target_occupancy.aft;
                if (target_container_id == EMPTY) continue;
                if (loadlist.containers[target_container_id].is_release) continue;

                // Pula se este contêiner já estiver estivado em outro lugar do candidato.
                if (candidate.container_cell[target_container_id] != EMPTY) continue;

                int& cand_ref = (half == 0) ? cand_occupancy.fore : cand_occupancy.aft;
                if (cand_ref != EMPTY) continue;   // slot já ocupado

                // Para o aft: exige um 20 pés no fore (restrição H1 / Eq. 5).
                if (half == 1) {
                    if (cand_occupancy.fore == EMPTY) continue;
                    if (loadlist.types[loadlist.containers[cand_occupancy.fore].type_id].length != 20) continue;
                }

                cand_ref = target_container_id;
                candidate.container_cell[target_container_id] = cell.global_id;
                candidate.container_aft[target_container_id]  = (half == 1);
            }
        }
    }

    // Preenche de forma gulosa quaisquer contêineres que sobraram após a importação.
    Repair::greedy(candidate, vessel, loadlist);
    candidate.dirty = true;
    return candidate;
}

} // namespace Flight
