#pragma once
#include "algorithm/Algorithm.hpp"

// Crow Search Algorithm (Askarzadeh 2016) aplicado ao problema de estiva.
// Uma população de corvos, cada um com um plano de estiva completo; a cada
// iteração um corvo faz um voo aleatório (explora) ou segue a memória de um par
// (intensifica). Usa RunConfig.population / FL / AP / threads.
class CrowSearch : public Algorithm {
public:
    std::string name() const override { return "csa"; }
    Solution    solve(const Vessel& vessel, const Loadlist& loadlist,
                      const RunConfig& cfg) override;
};
