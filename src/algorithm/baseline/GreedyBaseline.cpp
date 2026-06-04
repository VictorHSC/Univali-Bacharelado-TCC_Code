#include "algorithm/baseline/GreedyBaseline.hpp"
#include "problem/Repair.hpp"
#include "problem/Evaluator.hpp"
#include <iostream>

// Baseline trivial: apenas o reparo guloso + avaliação, sem busca. Serve de
// referência inferior e para validar a infraestrutura por trás da mesma interface.
Solution GreedyBaseline::solve(const Vessel& vessel, const Loadlist& loadlist,
                               const RunConfig& cfg) {
    Solution solution = Repair::buildGreedy(vessel, loadlist);
    Evaluator::update(solution, vessel, loadlist);

    if (cfg.verbose)
        std::cout << "=== Greedy baseline ===\n"
                  << "  Fitness   : " << solution.fitness()
                  << "  (unloaded=" << solution.kpis.unloaded << ")\n";

    return solution;
}
