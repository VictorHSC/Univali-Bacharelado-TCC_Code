#pragma once
#include "algorithm/Algorithm.hpp"

// Baseline trivial: uma única construção gulosa, sem busca. É o piso que qualquer
// metaheurística de verdade deve superar e um teste de sanidade de que o modelo
// do problema e a lógica de reparo produzem um plano viável por conta própria.
class GreedyBaseline : public Algorithm {
public:
    std::string name() const override { return "greedy"; }
    Solution    solve(const Vessel& vessel, const Loadlist& loadlist,
                      const RunConfig& cfg) override;
};
