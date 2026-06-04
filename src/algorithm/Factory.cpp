#include "algorithm/Factory.hpp"
#include "algorithm/crow_search/CrowSearch.hpp"
#include "algorithm/baseline/GreedyBaseline.hpp"

namespace AlgorithmFactory {

// Cria o solver pedido pelo nome (CLI --algo). Facilita trocar de algoritmo sem
// tocar no main — basta registrar aqui e em available().
std::unique_ptr<Algorithm> create(const std::string& name) {
    if (name == "csa")    return std::make_unique<CrowSearch>();
    if (name == "greedy") return std::make_unique<GreedyBaseline>();
    return nullptr;
}

std::vector<std::string> available() {
    return {"csa", "greedy"};
}

} // namespace AlgorithmFactory
