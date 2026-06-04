#pragma once
#include "model/Solution.hpp"
#include "model/Vessel.hpp"
#include "model/Container.hpp"
#include <random>

namespace Repair {
    // Estiva todos os contêineres livres ainda fora, escolhendo o slot de melhor score.
    void     greedy(Solution& solution, const Vessel& vessel, const Loadlist& loadlist);

    // Estiva todos os contêineres livres ainda fora, em um slot válido sorteado.
    void     random(Solution& solution, const Vessel& vessel, const Loadlist& loadlist,
                    std::mt19937& rng);

    // Constrói uma solução completa do zero: fixa os contêineres release e preenche guloso.
    Solution buildGreedy(const Vessel& vessel, const Loadlist& loadlist);
}
