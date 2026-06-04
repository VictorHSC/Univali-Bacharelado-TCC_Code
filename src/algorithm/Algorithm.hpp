#pragma once
#include <string>
#include "model/Solution.hpp"
#include "model/Vessel.hpp"
#include "model/Container.hpp"
#include "algorithm/RunConfig.hpp"

// Interface comum a todo solver de estiva. Uma nova metaheurística implementa
// esta interface e se registra em Factory.cpp — nenhuma outra parte do programa
// precisa mudar, o que torna a simulação "hot-swappable" (troca de algoritmo).
class Algorithm {
public:
    virtual ~Algorithm() = default;

    // Identificador curto usado na linha de comando e na saída (ex.: "csa").
    virtual std::string name() const = 0;

    // Produz o melhor plano de estiva que conseguir para o problema dado.
    virtual Solution solve(const Vessel& vessel, const Loadlist& loadlist,
                           const RunConfig& cfg) = 0;
};
