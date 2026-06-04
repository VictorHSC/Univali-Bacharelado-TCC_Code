#include "algorithm/crow_search/CrowSearch.hpp"
#include "algorithm/crow_search/Flight.hpp"
#include "problem/Evaluator.hpp"
#include "problem/Repair.hpp"
#include <random>
#include <vector>
#include <thread>
#include <mutex>
#include <limits>
#include <algorithm>
#include <utility>
#include <iostream>
#include <iomanip>

namespace {

struct Crow {
    Solution position;
    Solution memory;   // melhor pessoal
};

// Melhor solução compartilhada entre as instâncias paralelas da CSA (L&P rodam 4
// threads independentes e compartilham a melhor). Acessada só nas sincronizações
// de migração, sob o mutex.
struct Shared {
    std::mutex mutex;
    Solution   best;
    double     best_fit = std::numeric_limits<double>::max();
    bool       has      = false;
};

constexpr int MIGRATE_EVERY = 25;   // iterações entre cada compartilhamento da melhor

// Uma execução independente da CSA. Se `shared` estiver definido, a cada
// MIGRATE_EVERY iterações adota a melhor global (no seu pior corvo) e contribui
// com a sua própria melhor.
Solution runInstance(const Vessel& vessel, const Loadlist& loadlist, const RunConfig& cfg,
                     int seed, Shared* shared, bool verbose) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> rand01(0.0, 1.0);
    std::uniform_int_distribution<int>     rand_peer(0, cfg.population - 1);

    // ── Inicializa a população ────────────────────────────────────────────────
    // Corvo 0 recebe o plano guloso; os demais são diversificados por um voo
    // aleatório forte a partir dele.
    std::vector<Crow> crows(cfg.population);
    crows[0].position = Repair::buildGreedy(vessel, loadlist);
    Evaluator::update(crows[0].position, vessel, loadlist);
    crows[0].memory = crows[0].position;
    for (int i = 1; i < cfg.population; ++i) {
        crows[i].position = Flight::randomFlight(crows[0].position, vessel, loadlist, 0.6, rng);
        Evaluator::update(crows[i].position, vessel, loadlist);
        crows[i].memory = crows[i].position;
    }

    Solution best = crows[0].memory;
    for (const Crow& crow : crows)
        if (crow.memory.fitness() < best.fitness()) best = crow.memory;

    if (verbose)
        std::cout << std::fixed << std::setprecision(2)
                  << "  Init best  : " << best.fitness()
                  << "  (unloaded=" << best.kpis.unloaded << ")\n\n";

    // ── Laço principal ────────────────────────────────────────────────────────
    for (int iter = 1; iter <= cfg.max_iter; ++iter) {
        for (int i = 0; i < cfg.population; ++i) {
            Solution candidate;
            if (rand01(rng) < cfg.AP) {
                // Probabilidade de consciência (AP): voo aleatório (exploração).
                candidate = Flight::randomFlight(crows[i].position, vessel, loadlist,
                                                  cfg.FL, rng);
            } else {
                // Caso contrário, segue a memória de um par (intensificação).
                int j = i;
                while (j == i) j = rand_peer(rng);
                candidate = Flight::followCrow(crows[i].position, crows[j].memory,
                                               vessel, loadlist, cfg.FL, rng);
            }
            Evaluator::update(candidate, vessel, loadlist);

            // CSA canônica: o corvo SEMPRE voa para a nova posição (esse vagar é a
            // exploração); só a memória guarda a melhor estiva encontrada.
            crows[i].position = std::move(candidate);
            if (crows[i].position.fitness() < crows[i].memory.fitness())
                crows[i].memory = crows[i].position;
            if (crows[i].memory.fitness() < best.fitness())
                best = crows[i].memory;
        }

        // Migração: troca a melhor solução com as outras threads.
        if (shared && iter % MIGRATE_EVERY == 0) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->has && shared->best_fit < best.fitness()) {
                int worst = 0;
                for (int i = 1; i < cfg.population; ++i)
                    if (crows[i].memory.fitness() > crows[worst].memory.fitness())
                        worst = i;
                crows[worst].position = shared->best;
                crows[worst].memory   = shared->best;
                best = shared->best;
            }
            if (!shared->has || best.fitness() < shared->best_fit) {
                shared->best     = best;
                shared->best_fit = best.fitness();
                shared->has      = true;
            }
        }

        if (verbose && iter % 100 == 0)
            std::cout << "  Iter " << std::setw(5) << iter
                      << " | fitness=" << std::setw(12) << best.fitness()
                      << " | unloaded=" << best.kpis.unloaded
                      << " | hatch_ov=" << best.kpis.hatch_overstow
                      << " | stack_ov=" << best.kpis.stack_overstow << "\n";
    }
    return best;
}

} // namespace

Solution CrowSearch::solve(const Vessel& vessel, const Loadlist& loadlist,
                           const RunConfig& cfg) {
    int n_threads = std::max(1, cfg.threads);

    if (cfg.verbose)
        std::cout << "=== CSA ===\n"
                  << "  Threads    : " << n_threads
                  << (n_threads > 1 ? "  (independent, best shared)\n" : "\n")
                  << "  Population : " << cfg.population << (n_threads > 1 ? " each\n" : "\n")
                  << "  FL / AP    : " << cfg.FL << " / " << cfg.AP << "\n"
                  << "  Max iter   : " << cfg.max_iter << "\n";

    // Uma thread: determinístico, sem migração.
    if (n_threads == 1)
        return runInstance(vessel, loadlist, cfg, cfg.seed, nullptr, cfg.verbose);

    // Multithread: instâncias independentes com sementes distintas, compartilhando a melhor.
    Shared shared;
    std::vector<std::thread> pool;
    std::vector<Solution>    results(n_threads);
    for (int thread_index = 0; thread_index < n_threads; ++thread_index)
        pool.emplace_back([&, thread_index] {
            results[thread_index] = runInstance(vessel, loadlist, cfg,
                                                cfg.seed + thread_index * 7919,
                                                &shared, cfg.verbose && thread_index == 0);
        });
    for (std::thread& worker : pool) worker.join();

    Solution best = results[0];
    for (const Solution& result : results)
        if (result.fitness() < best.fitness()) best = result;

    if (cfg.verbose)
        std::cout << "\n  Final best (over " << n_threads << " threads): "
                  << best.fitness() << "\n";
    return best;
}
