#pragma once
#include <vector>
#include <map>
#include <tuple>
#include <utility>

// Uma posição de slot de contêiner dentro de uma seção de pilha (acima ou abaixo
// do convés). As células são armazenadas de baixo para cima em cada StackSlot.
struct CellSlot {
    int   global_id;   // índice plano usado para endereçar Solution::cells
    int   tier;        // número físico do tier (nível) nesta pilha
    bool  reefer;      // possui tomada elétrica — KPI 8 + restrição rígida de reefer
    bool  above_deck;  // acima da tampa de porão — KPI 9 (C_below)
    float vcg;         // centro de gravidade vertical desta seção — KPI 5 (vm)
};

// Uma seção estrutural de uma coluna física de pilha (acima OU abaixo do convés).
// Uma mesma coluna (bay, stack_index) pode gerar duas entradas StackSlot.
struct StackSlot {
    int   bay;            // índice do bay (base 0), também usado como ID da tampa de porão
    int   stack_index;    // índice da pilha dentro do bay (base 0)
    float tcg;            // centro de gravidade transversal desta pilha (m) — verificação TCG
    float max_weight_20;  // restrição rígida: peso máximo da pilha com 20 pés (toneladas)
    float max_weight_40;  // restrição rígida: peso máximo da pilha com 40 pés (toneladas)
    float max_height;     // restrição rígida: altura máxima empilhada (m)
    int   hatch_cover;    // ID da tampa de porão = índice do bay — KPI 2 e 6
    bool  above_deck;     // de que lado da tampa de porão a seção fica
    std::vector<CellSlot> cells;  // ordenadas de baixo para cima
};

// Um ponto da tabela hidrostática do navio. O ponto ativo é aquele cujo intervalo
// de deslocamento contém o peso total atual; ele define a faixa de LCG permitida e
// a altura do metacentro usada na verificação de VCG / GM.
struct HydroPoint {
    float displacement;  // toneladas
    float lcg_min;       // m — centro de gravidade longitudinal mínimo neste deslocamento
    float lcg_max;       // m — centro de gravidade longitudinal máximo
    float metacenter;    // m — altura do metacentro acima da quilha (KM)
};

// Um tanque de lastro/combustível: um peso fixo distribuído por um ou mais bays.
struct Tank {
    float capacity;    // toneladas (considerado cheio na condição de chegada)
    float lcg;         // m
    float tcg;         // m
    float vcg_empty;   // m
    float vcg_full;    // m
    // (índice do bay, fração do peso deste tanque suportada por esse bay).
    std::vector<std::pair<int,float>> bay_coverage;
};

// Dados estruturais por bay que alimentam as verificações de estabilidade longitudinal.
struct BayData {
    int   index;                // índice do bay (base 0)
    float lcg;                  // m — centro de gravidade longitudinal do bay
    float shear_min;            // t — limite inferior do esforço cortante acumulado neste bay
    float shear_max;            // t — limite superior do esforço cortante acumulado
    float bend_max;             // t·m — momento fletor máximo (em módulo) neste bay
    float constant_weight;      // t — peso leve (lightship) atribuído a este bay
    float constant_weight_vcg;  // m — VCG desse peso leve
    // Empuxo neste bay, um valor por ponto hidrostático (mesma ordem de hydrostatic_points).
    std::vector<float> buoyancy;
};

struct Vessel {
    int   number_of_bays;
    int   number_of_stacks;
    int   number_of_tiers;
    
    float tcg_tolerance;

    std::vector<StackSlot> stacks;   // todas as seções de pilha válidas
    int total_cells = 0;

    // Dados de estabilidade (lidos do arquivo do navio; usados nas verificações de viabilidade).
    std::vector<HydroPoint> hydrostatic_points;  // ordenados por deslocamento crescente
    std::vector<Tank>       tanks;               // pesos fixos de lastro/combustível
    std::vector<BayData>    bays;                // peso leve, empuxo e limites por bay

    // Pré-computados na leitura do arquivo para acelerar a avaliação dos KPIs:

    // hatch_above_cells[hatch_id] → global_ids das células acima daquela tampa de porão (KPI 2, 6)
    // hatch_below_cells[hatch_id] → global_ids das células abaixo daquela tampa de porão
    std::vector<std::vector<int>> hatch_above_cells;
    std::vector<std::vector<int>> hatch_below_cells;

    // Pares de bays adjacentes (KPI 4: makespan do guindaste)
    std::vector<std::pair<int,int>> adjacent_bay_pairs;

    // (bay, stack_index dentro do bay, tier) → global_id da célula.
    // Usado pelo InstanceParser para resolver as posições dos contêineres fixados (release).
    std::map<std::tuple<int,int,int>, int> cell_lookup;
};
