#pragma once
#include <string>
#include "model/Vessel.hpp"

namespace VesselParser {
    // Lê um arquivo de benchmark vessel_*.txt e retorna um Vessel totalmente preenchido.
    // Lança std::runtime_error se o arquivo não puder ser aberto ou estiver malformado.
    Vessel parse(const std::string& filepath);
}
