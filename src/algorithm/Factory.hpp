#pragma once
#include <memory>
#include <string>
#include <vector>
#include "algorithm/Algorithm.hpp"

namespace AlgorithmFactory {
    // Cria um solver pelo nome (ex.: "csa", "greedy"). Retorna nullptr se desconhecido.
    std::unique_ptr<Algorithm> create(const std::string& name);

    // Nomes de todos os algoritmos registrados, para o texto de ajuda e validação.
    std::vector<std::string> available();
}
