#pragma once
#include "model/Solution.hpp"
#include "model/Vessel.hpp"
#include "model/Container.hpp"
#include <random>

namespace Flight {
    // Destrói a fração FL das pilhas ocupadas e reinsere os despejados ao acaso.
    Solution randomFlight(const Solution& solution, const Vessel& vessel, const Loadlist& loadlist,
                          double FL, std::mt19937& rng);

    // Copia a fração FL das atribuições de `target` para `current`; preenche o resto guloso.
    Solution followCrow  (const Solution& current, const Solution& target,
                          const Vessel& vessel, const Loadlist& loadlist,
                          double FL, std::mt19937& rng);
}
