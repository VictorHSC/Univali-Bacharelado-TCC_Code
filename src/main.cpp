#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "model/Vessel.hpp"
#include "model/Container.hpp"
#include "model/Solution.hpp"
#include "io/VesselParser.hpp"
#include "io/InstanceParser.hpp"
#include "problem/Evaluator.hpp"
#include "problem/Stability.hpp"
#include "algorithm/Algorithm.hpp"
#include "algorithm/Factory.hpp"

// ── Resumos impressos no console ────────────────────────────────────────────

static void printVesselSummary(const Vessel& vessel) {
    std::cout << "=== Vessel ===\n";
    std::cout << "  Grid          : " << vessel.number_of_bays << " bays × "
              << vessel.number_of_stacks << " stacks × " << vessel.number_of_tiers << " tiers\n";
    std::cout << "  TCG tolerance : " << vessel.tcg_tolerance << "\n";
    std::cout << "  Stack sections: " << vessel.stacks.size() << "\n";
    std::cout << "  Total cells   : " << vessel.total_cells
              << "  (" << vessel.total_cells * 2 << " TEU capacity)\n";

    int reefer_cells = 0;
    for (const StackSlot& stack : vessel.stacks)
        for (const CellSlot& cell : stack.cells)
            if (cell.reefer) ++reefer_cells;
    std::cout << "  Reefer plugs  : " << reefer_cells << "\n";
    std::cout << "  Hatch covers  : " << vessel.hatch_above_cells.size() << "\n";
    std::cout << "  Adjacent pairs: " << vessel.adjacent_bay_pairs.size() << "\n\n";
}

static void printLoadlistSummary(const Loadlist& loadlist) {
    std::cout << "=== Loadlist ===\n";
    std::cout << "  Ports      : " << loadlist.number_of_ports << "\n";
    std::cout << "  Containers : " << loadlist.containers.size()
              << "  (header=" << loadlist.number_of_containers << ")\n";

    int free = 0, release = 0, reefer = 0, ft20 = 0, ft40 = 0;
    for (const ContainerInstance& container : loadlist.containers) {
        if (container.is_release) ++release; else ++free;
        const ContainerType& type = loadlist.types[container.type_id];
        if (type.is_reefer) ++reefer;
        if (type.length == 20) ++ft20; else ++ft40;
    }
    std::cout << "  Release    : " << release << "  Free: " << free << "\n";
    std::cout << "  20-foot    : " << ft20 << "  40-foot: " << ft40 << "\n";
    std::cout << "  Reefer     : " << reefer << " units\n";
    std::cout << "  Loadlist   : " << (ft20 + ft40 * 2) << " TEU\n\n";
}

static void printKPIs(const Solution& solution) {
    const KPIs& kpis = solution.kpis;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Objective (Eq.1) : " << solution.objective()
              << "   ← comparable to ALNS\n";
    std::cout << "  Search fitness   : " << solution.fitness()
              << "   = objective + soft penalties\n";
    std::cout << "  ── KPI breakdown ──────────────────────────────\n";
    std::cout << "  [×1000] Unloaded       : " << kpis.unloaded
              << "  (" << kpis.unloaded * 1000.0 << ")\n";
    std::cout << "  [×100 ] Hatch overstow : " << kpis.hatch_overstow
              << "  (" << kpis.hatch_overstow * 100.0 << ")\n";
    std::cout << "  [×100 ] Stack overstow : " << kpis.stack_overstow
              << "  (" << kpis.stack_overstow * 100.0 << ")\n";
    std::cout << "  [×1   ] Makespan       : " << kpis.makespan << "\n";
    std::cout << "  [×0.0001] Vert. moment : " << kpis.vert_moment
              << "  (" << kpis.vert_moment * 0.0001 << ")\n";
    std::cout << "  [×20  ] Block purity   : " << kpis.block_purity
              << "  (" << kpis.block_purity * 20.0 << ")\n";
    std::cout << "  [−10  ] Empty stacks   : " << kpis.empty_stacks
              << "  (" << -kpis.empty_stacks * 10.0 << ")\n";
    std::cout << "  [×5   ] Reefer misuse  : " << kpis.reefer_misuse
              << "  (" << kpis.reefer_misuse * 5.0 << ")\n";
    std::cout << "  [−0.5 ] Below score    : " << kpis.below_score
              << "  (" << -kpis.below_score * 0.5 << ")\n";
    std::cout << "  ── Soft penalties (search only, not in objective) ──\n";
    std::cout << "  [pen ] Stability       : (" << solution.stability_penalty << ")\n";
    std::cout << "  [pen ] Lashing (wt↑)   : (" << solution.lashing_penalty << ")\n";
}

static void printSeaworthiness(const Solution& solution, const Vessel& vessel,
                               const Loadlist& loadlist) {
    StabilityReport report = Stability::check(solution, vessel, loadlist);
    auto ok = [](bool b) { return b ? "ok " : "VIOLATED"; };
    std::cout << "  ── Seaworthiness (soft-penalised in search) ────\n";
    std::cout << "  Overall   : "
              << (report.feasible ? "SEAWORTHY" : "NOT SEAWORTHY") << "\n";
    std::cout << "  LCG trim  : " << ok(report.lcg_ok)
              << "   TCG heel : " << ok(report.tcg_ok) << "\n";
    std::cout << "  Shear     : " << ok(report.shear_ok)
              << "   Bending  : " << ok(report.bend_ok) << "\n";
    std::cout << "  GM (info) : " << report.gm << " m\n";
}

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog
              << " [--algo NAME] [vessel] [instance] [population] [max_iter] [threads]\n\n";
    std::cout << "  --algo NAME   solver to run (default: csa). Available: ";
    for (const std::string& n : AlgorithmFactory::available())
        std::cout << n << " ";
    std::cout << "\n";
    std::cout << "  vessel        vessel file    (default: benchmark/vessels/vessel_S.txt)\n";
    std::cout << "  instance      instance file  (default: benchmark/instances/Vessel_S/VSHigh1.txt)\n";
    std::cout << "  population    crows, CSA only (default: 20)\n";
    std::cout << "  max_iter      iterations     (default: 1000)\n";
    std::cout << "  threads       parallel CSA instances, CSA only (default: 4; 1 = deterministic)\n";
}

// ── Ponto de entrada ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string algo_name     = "csa";
    std::string vessel_path   = "benchmark/vessels/vessel_S.txt";
    std::string instance_path = "benchmark/instances/Vessel_S/VSHigh1.txt";
    RunConfig   cfg;

    // Lê: [--algo NOME] e depois os posicionais [vessel instance population max_iter threads].
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--algo" && i + 1 < argc) {
            algo_name = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() >= 1) vessel_path   = positional[0];
    if (positional.size() >= 2) instance_path = positional[1];
    // std::stoi lança em entrada não-numérica: trata aqui para dar um erro claro
    // em vez de abortar com exceção não capturada.
    try {
        if (positional.size() >= 3) cfg.population = std::stoi(positional[2]);
        if (positional.size() >= 4) cfg.max_iter   = std::stoi(positional[3]);
        if (positional.size() >= 5) cfg.threads    = std::stoi(positional[4]);
    } catch (const std::exception&) {
        std::cerr << "Invalid numeric argument "
                     "(population, max_iter and threads must be integers)\n";
        printUsage(argv[0]);
        return 1;
    }

    std::unique_ptr<Algorithm> algorithm = AlgorithmFactory::create(algo_name);
    if (!algorithm) {
        std::cerr << "Unknown algorithm: " << algo_name << "\n";
        printUsage(argv[0]);
        return 1;
    }

    try {
        std::cout << "Algorithm: " << algorithm->name() << "\n";
        std::cout << "Vessel   : " << vessel_path   << "\n";
        std::cout << "Instance : " << instance_path << "\n\n";

        Vessel   vessel   = VesselParser::parse(vessel_path);
        Loadlist loadlist = InstanceParser::parse(instance_path, vessel);

        printVesselSummary(vessel);
        printLoadlistSummary(loadlist);

        Solution best = algorithm->solve(vessel, loadlist, cfg);

        std::cout << "\n=== Best Solution ===\n";
        printKPIs(best);
        printSeaworthiness(best, vessel, loadlist);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
