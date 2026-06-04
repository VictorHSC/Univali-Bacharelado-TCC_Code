#pragma once
#include <vector>
#include <cstdint>

// Tipo de contêiner: DC = seco, RC = refrigerado, HC = high-cube (mais alto),
// HR = high-cube refrigerado.
enum class ContainerKind : uint8_t { DC, RC, HC, HR };

// Definição estática de uma classe de contêiner, lida da seção "# Transport
// type" do arquivo de instância.
struct ContainerType {
    int           id;
    int           length;      // 20 ou 40 (pés)
    float         weight;      // toneladas — usado no momento vertical (KPI 5) e na estabilidade
    ContainerKind kind;
    bool          is_reefer;   // RC ou HR — restrição rígida H4: precisa de tomada (reefer)
    bool          is_highcube; // HC ou HR — afeta a restrição rígida H3 de altura
};

// Uma entrada da seção "# Container" do arquivo de instância (um contêiner concreto).
struct ContainerInstance {
    int  id;             // índice sequencial na loadlist (usado como chave em todos os vetores)
    int  type_id;        // índice em Loadlist::types
    int  origin_port;    // porto de embarque (sempre 0 neste benchmark)
    int  dest_port;      // porto de descarga d_t
    int  port_distance;  // dest_port − origin_port — d_t do KPI 9 (C_below · β_c · d_t)

    bool is_release;      // já embarcado em porto anterior; posição fixa (restrição H6), imóvel
    int  release_cell_id; // global_id da célula da posição fixa (−1 se não for release)
    bool release_is_aft;  // true = slot 2 (ré/aft), false = slot 1 (vante/fore)
};

// A loadlist completa de um porto: as classes de contêiner e os contêineres a estivar.
struct Loadlist {
    int number_of_ports;       // número de portos no horizonte
    int number_of_containers;  // contagem bruta do cabeçalho do arquivo
    std::vector<ContainerType>     types;
    std::vector<ContainerInstance> containers;
};
