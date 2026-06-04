#pragma once
#include <string>
#include "model/Container.hpp"
#include "model/Vessel.hpp"

namespace InstanceParser {
    // Lê um arquivo de instância de contêineres (ex.: VSHigh1.txt) e retorna a Loadlist.
    // O Vessel é necessário para resolver as posições dos contêineres release via cell_lookup.
    // Lança std::runtime_error se o arquivo não puder ser aberto ou estiver malformado.
    Loadlist parse(const std::string& filepath, const Vessel& vessel);
}
