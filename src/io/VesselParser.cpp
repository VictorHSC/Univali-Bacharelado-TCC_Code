#include "io/VesselParser.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

// ── Auxiliares ──────────────────────────────────────────────────────────────

// Conta os '#' do início da linha — a profundidade de '#' codifica o nível de
// aninhamento (# Ship, ## Bay/Hydro/Tanks, ### Stack/Buoyancy, #### Deck/Cell).
static int leadingHashes(const std::string& s) {
    int count = 0;
    for (char c : s) { if (c == '#') ++count; else break; }
    return count;
}

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool contains(const std::string& s, const char* sub) {
    return s.find(sub) != std::string::npos;
}

// ── Estado do parser (máquina de estados por profundidade de '#') ────────────

enum class State {
    NONE,
    SHIP,
    HYDRO,        // ## HydroPoints
    TANKS,        // ## Tanks
    TANK_COV,     // ### BayCoverage  (pertence ao tanque anterior)
    BAY,          // ## Bay
    BUOYANCY,     // ### BuoyancyPoints  (pertence ao bay anterior)
    STACK,        // ### Stack
    DECK,         // #### AboveDeck / BelowDeck  (lê a linha de dados do cabeçalho)
    CELLS         // #### Cell  (lê as linhas tier+reefer)
};

// ── commitSection ───────────────────────────────────────────────────────────
// Finaliza a seção AboveDeck/BelowDeck acumulada até aqui. Só cria um StackSlot
// quando há células de fato (descarta as pilhas vazias da proa, p. ex.).

struct SectionAcc {
    int   bay         = -1;
    int   stack_index = -1;
    float tcg         = 0.f;
    bool  above_deck  = true;
    float max_h       = 0.f;
    float max_w20     = 0.f;
    float max_w40     = 0.f;
    float vcg         = 0.f;
    // Células como aparecem no arquivo: tier do topo primeiro, do fundo por último.
    std::vector<std::pair<int,bool>> cells_topdown;
};

static void commitSection(Vessel& vessel, SectionAcc& acc, int& next_cell_id) {
    if (acc.cells_topdown.empty() || acc.bay < 0 || acc.stack_index < 0) {
        acc.cells_topdown.clear();
        return;
    }

    StackSlot stack_slot;
    stack_slot.bay           = acc.bay;
    stack_slot.stack_index   = acc.stack_index;
    stack_slot.tcg           = acc.tcg;
    stack_slot.max_weight_20 = acc.max_w20;
    stack_slot.max_weight_40 = acc.max_w40;
    stack_slot.max_height    = acc.max_h;
    stack_slot.hatch_cover   = acc.bay;   // uma tampa de porão por bay
    stack_slot.above_deck    = acc.above_deck;

    // O arquivo lista as células de cima para baixo; armazenamos de baixo para cima.
    for (int i = (int)acc.cells_topdown.size() - 1; i >= 0; --i) {
        auto [tier, reefer] = acc.cells_topdown[i];
        CellSlot cell_slot;
        cell_slot.global_id  = next_cell_id++;
        cell_slot.tier       = tier;
        cell_slot.reefer     = reefer;
        cell_slot.above_deck = acc.above_deck;
        cell_slot.vcg        = acc.vcg;
        stack_slot.cells.push_back(cell_slot);
        vessel.cell_lookup[{acc.bay, acc.stack_index, tier}] = cell_slot.global_id;
    }

    vessel.stacks.push_back(std::move(stack_slot));
    acc.cells_topdown.clear();
    // Mantém bay/stack_index para uma seção BelowDeck seguinte reaproveitá-los.
    acc.max_h = acc.max_w20 = acc.max_w40 = acc.vcg = 0.f;
}

// ── parse ───────────────────────────────────────────────────────────────────

Vessel VesselParser::parse(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        throw std::runtime_error("VesselParser: cannot open '" + filepath + "'");

    Vessel vessel;
    int next_cell_id = 0;
    State state = State::NONE;
    SectionAcc acc;

    std::string line;
    while (std::getline(file, line)) {
        std::string s = trim(line);
        if (s.empty()) continue;

        // ── Linha de cabeçalho ────────────────────────────────────────────────
        if (s[0] == '#') {
            int hash_depth = leadingHashes(s);

            if (hash_depth == 1) {                 // # Ship:
                commitSection(vessel, acc, next_cell_id);
                state = contains(s, "Ship:") ? State::SHIP : State::NONE;

            } else if (hash_depth == 2) {          // ## Bay / HydroPoints / Tanks
                commitSection(vessel, acc, next_cell_id);
                if      (contains(s, "Bay:"))        state = State::BAY;
                else if (contains(s, "HydroPoints:"))state = State::HYDRO;
                else if (contains(s, "Tanks:"))      state = State::TANKS;
                else                                 state = State::NONE;

            } else if (hash_depth == 3) {          // ### Stack / BuoyancyPoints / BayCoverage
                commitSection(vessel, acc, next_cell_id);
                if (contains(s, "Stack:")) {
                    state = State::STACK;
                    acc.stack_index = -1;          // será lido na próxima linha de dados
                    acc.tcg         = 0.f;
                } else if (contains(s, "BuoyancyPoints:")) {
                    state = State::BUOYANCY;
                } else {
                    state = State::TANK_COV;       // BayCoverage ou outro → ignora
                }

            } else if (hash_depth == 4) {          // #### AboveDeck / BelowDeck / Cell
                if (contains(s, "AboveDeck:")) {
                    commitSection(vessel, acc, next_cell_id);
                    acc.above_deck = true;
                    state = State::DECK;
                } else if (contains(s, "BelowDeck:")) {
                    commitSection(vessel, acc, next_cell_id);
                    acc.above_deck = false;
                    state = State::DECK;
                } else if (contains(s, "Cell:")) {
                    // A linha de dados do deck já foi lida; agora coletam-se as células.
                    state = State::CELLS;
                } else {
                    state = State::NONE;
                }
            }
            continue;
        }

        // ── Linha de dados ────────────────────────────────────────────────────
        std::istringstream iss(s);

        switch (state) {
            case State::SHIP: {
                iss >> vessel.number_of_bays >> vessel.number_of_stacks
                    >> vessel.number_of_tiers >> vessel.tcg_tolerance;
                break;
            }
            case State::HYDRO: {
                // "displacement minLcg maxLcg metacenter"
                HydroPoint hydro_point;
                if (iss >> hydro_point.displacement >> hydro_point.lcg_min >> hydro_point.lcg_max >> hydro_point.metacenter)
                    vessel.hydrostatic_points.push_back(hydro_point);
                break;
            }
            case State::TANKS: {
                // "cap lcg tcg vcg_empty vcg_full" — um tanque por bloco ## Tanks: (não usado).
                Tank tank;
                if (iss >> tank.capacity >> tank.lcg >> tank.tcg >> tank.vcg_empty >> tank.vcg_full)
                    vessel.tanks.push_back(tank);
                break;
            }
            case State::TANK_COV: {
                // "bay_idx coverage" — anexa ao tanque recém-lido.
                int bay; float fraction;
                if ((iss >> bay >> fraction) && !vessel.tanks.empty())
                    vessel.tanks.back().bay_coverage.push_back({bay, fraction});
                break;
            }
            case State::BAY: {
                // "index lcg minShear maxShear maxBending constWeight constWeightVcg"
                BayData bay_data;
                if (iss >> bay_data.index >> bay_data.lcg >> bay_data.shear_min >> bay_data.shear_max
                        >> bay_data.bend_max >> bay_data.constant_weight >> bay_data.constant_weight_vcg) {
                    acc.bay = bay_data.index;
                    vessel.bays.push_back(std::move(bay_data));
                }
                break;
            }
            case State::BUOYANCY: {
                // Um valor de empuxo por linha, na ordem dos pontos hidrostáticos.
                float buoyancy;
                if ((iss >> buoyancy) && !vessel.bays.empty())
                    vessel.bays.back().buoyancy.push_back(buoyancy);
                break;
            }
            case State::STACK: {
                // "index tcg"
                if (iss >> acc.stack_index >> acc.tcg) { }
                break;
            }
            case State::DECK: {
                // "identifier maxHeight maxWeight20 maxWeight40 vcg"
                int id;
                if (iss >> id >> acc.max_h >> acc.max_w20 >> acc.max_w40 >> acc.vcg) { }
                break;
            }
            case State::CELLS: {
                // "tier reefer_flag"
                int tier, reefer_flag;
                if (iss >> tier >> reefer_flag)
                    acc.cells_topdown.push_back({tier, reefer_flag == 1});
                break;
            }
            default:
                break;  // NONE → ignora
        }
    }

    // Finaliza qualquer seção ainda aberta no fim do arquivo.
    commitSection(vessel, acc, next_cell_id);
    vessel.total_cells = next_cell_id;

    // ── Tabelas de lookup pré-computadas ─────────────────────────────────────

    // Agrupamento por tampa de porão (hatch_id = índice do bay) — KPI 2 e 6.
    if (!vessel.stacks.empty()) {
        int max_hatch = 0;
        for (auto& stack_slot : vessel.stacks)
            max_hatch = std::max(max_hatch, stack_slot.hatch_cover);

        vessel.hatch_above_cells.resize(max_hatch + 1);
        vessel.hatch_below_cells.resize(max_hatch + 1);

        for (auto& stack_slot : vessel.stacks)
            for (auto& cell_slot : stack_slot.cells)
                (stack_slot.above_deck ? vessel.hatch_above_cells : vessel.hatch_below_cells)
                    [stack_slot.hatch_cover].push_back(cell_slot.global_id);
    }

    // Pares de bays adjacentes para o makespan (KPI 4).
    std::vector<int> bays;
    for (auto& stack_slot : vessel.stacks) bays.push_back(stack_slot.bay);
    std::sort(bays.begin(), bays.end());
    bays.erase(std::unique(bays.begin(), bays.end()), bays.end());
    for (int i = 0; i + 1 < (int)bays.size(); ++i)
        if (bays[i + 1] == bays[i] + 1)
            vessel.adjacent_bay_pairs.push_back({bays[i], bays[i + 1]});

    return vessel;
}
