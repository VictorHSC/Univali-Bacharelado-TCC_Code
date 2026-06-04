#pragma once
#include "model/Solution.hpp"
#include "model/Vessel.hpp"
#include "model/Container.hpp"

namespace Evaluator {
    // Calcula os 9 KPIs de Larsen & Pacino (2021).
    KPIs compute(const Solution& solution, const Vessel& vessel, const Loadlist& loadlist);
    // Calcula os KPIs + penalidades soft e limpa a flag dirty.
    void update (Solution& solution,       const Vessel& vessel, const Loadlist& loadlist);
}
