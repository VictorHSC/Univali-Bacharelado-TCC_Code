#include "io/InstanceParser.hpp"
#include "model/Solution.hpp"   // para o sentinela EMPTY
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

// ── Auxiliares ──────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool contains(const std::string& s, const char* sub) {
    return s.find(sub) != std::string::npos;
}

static ContainerKind kindFromString(const std::string& k) {
    if (k == "RC") return ContainerKind::RC;
    if (k == "HC") return ContainerKind::HC;
    if (k == "HR") return ContainerKind::HR;
    return ContainerKind::DC;
}

// ── parse ───────────────────────────────────────────────────────────────────

Loadlist InstanceParser::parse(const std::string& filepath, const Vessel& vessel) {
    std::ifstream file(filepath);
    if (!file.is_open())
        throw std::runtime_error("InstanceParser: cannot open '" + filepath + "'");

    Loadlist loadlist;
    int container_id = 0;

    enum class State { NONE, PARAMS, TYPES, CONTAINERS };
    State state = State::NONE;

    std::string line;
    while (std::getline(file, line)) {
        std::string s = trim(line);
        if (s.empty()) continue;

        // ── Linha de cabeçalho ────────────────────────────────────────────────
        if (s[0] == '#') {
            if      (contains(s, "Parameters:"))    state = State::PARAMS;
            else if (contains(s, "Transport type:"))state = State::TYPES;
            else if (contains(s, "Container:"))     state = State::CONTAINERS;
            continue;
        }

        // ── Linha de dados ────────────────────────────────────────────────────
        std::istringstream iss(s);

        if (state == State::PARAMS) {
            iss >> loadlist.number_of_ports >> loadlist.number_of_containers;
            state = State::NONE;  // só uma linha de dados

        } else if (state == State::TYPES) {
            // "id length weight kind" — o 3º campo é o peso em toneladas (3,6,9,14,21,27).
            int id, length, weight;
            std::string kind_str;
            if (!(iss >> id >> length >> weight >> kind_str)) continue;

            ContainerType type;
            type.id          = id;
            type.length      = length;
            type.weight      = static_cast<float>(weight);
            type.kind        = kindFromString(kind_str);
            type.is_reefer   = (type.kind == ContainerKind::RC || type.kind == ContainerKind::HR);
            type.is_highcube = (type.kind == ContainerKind::HC || type.kind == ContainerKind::HR);

            if (id >= static_cast<int>(loadlist.types.size()))
                loadlist.types.resize(id + 1);
            loadlist.types[id] = type;

        } else if (state == State::CONTAINERS) {
            // Lê todos os campos inteiros da linha.
            // 3 campos: startPort endPort typeId                      → contêiner livre
            // 7 campos: startPort endPort typeId bay stack tier slot  → contêiner release
            std::vector<int> fields;
            int value;
            while (iss >> value) fields.push_back(value);

            if (fields.size() != 3 && fields.size() != 7) continue;

            ContainerInstance container;
            container.id            = container_id++;
            container.origin_port   = fields[0];
            container.dest_port     = fields[1];
            container.type_id       = fields[2];
            container.port_distance = fields[1] - fields[0];  // d_t do KPI 9 (C_below)
            container.is_release    = (fields.size() == 7);
            container.release_cell_id = EMPTY;
            container.release_is_aft  = false;

            if (container.is_release) {
                int bay = fields[3], stack = fields[4], tier = fields[5], slot = fields[6];
                auto key = std::make_tuple(bay, stack, tier);
                auto it  = vessel.cell_lookup.find(key);
                if (it != vessel.cell_lookup.end()) {
                    container.release_cell_id = it->second;
                } else {
                    std::cerr << "InstanceParser: aviso — contêiner release " << container.id
                              << " em (bay=" << bay << " stack=" << stack
                              << " tier=" << tier << ") não encontrado no navio.\n";
                }
                container.release_is_aft = (slot == 2);   // slot 1 = vante, 2 = ré
            }

            loadlist.containers.push_back(container);
        }
    }

    return loadlist;
}
