#pragma once
#include <vector>
#include <limits>

static constexpr int EMPTY = -1;  // sentinela: posição vazia ou contêiner não embarcado

// O que ocupa uma célula (uma posição de tier dentro de uma seção de pilha).
// Uma célula contém:
//   • um contêiner de 40 pés  →  fore = id, aft = EMPTY
//   • dois contêineres de 20  →  fore = id, aft = id
//   • nada                    →  fore = EMPTY, aft = EMPTY
// Um 20 pés sozinho (fore preenchido, aft vazio) é proibido (restrição H1/Eq. 5);
// o reparo garante que isso não permaneça na solução final.
struct CellOccupancy {
    int fore = EMPTY;  // slot 1 (vante) — contém o 40 pés, ou o primeiro 20 pés
    int aft  = EMPTY;  // slot 2 (ré)    — contém o segundo 20 pés do par
};

// Os 9 KPIs de Larsen & Pacino (2021), mantidos separados para que o objetivo
// e os relatórios possam inspecionar cada componente. Cada um corresponde a um
// termo da função-objetivo, Eq. (1) do artigo.
struct KPIs {
    int   unloaded       = 0;   // KPI 1 — contêineres não embarcados (Σ y_t),        custo 1000
    int   hatch_overstow = 0;   // KPI 2 — hatch overstow por célula (Σ ho_ci, Eq.36), custo 100
    int   stack_overstow = 0;   // KPI 3 — stack overstow por slot (Σ o_cl, Eq.37),    custo 100
    int   makespan       = 0;   // KPI 4 — makespan do guindaste (mk, Eq.17),          custo 1
    float vert_moment    = 0.f; // KPI 5 — momento vertical / VCG (vm, Eq.27),         custo 0.0001
    int   block_purity   = 0;   // KPI 6 — pureza de bloco (Σ bp^q_ip, Eq.18),         custo 20
    int   empty_stacks   = 0;   // KPI 7 — pilhas vazias (e_s, Eq.19),                 prêmio 10
    int   reefer_misuse  = 0;   // KPI 8 — não-reefer em slot reefer (Σ nr_cl),        custo 5
    float below_score    = 0.f; // KPI 9 — favorecer porão / longo curso (Σ β_c·d_t),  prêmio 0.5
};

struct Solution {
    // Atribuição principal: indexada pelo global_id da célula.
    std::vector<CellOccupancy> cells;

    // Índice reverso: container_id → global_id da célula que ocupa, EMPTY se não embarcado.
    std::vector<int>  container_cell;
    // Índice reverso: container_id → true se estiver no slot de ré (aft).
    std::vector<bool> container_aft;

    KPIs kpis;

    // Custo soft de inavegabilidade das quatro restrições de estabilidade (LCG,
    // TCG, esforço cortante, momento fletor), definido pelo Evaluator. Espelha o
    // termo ϑ do ALNS de L&P: guia de busca com peso alto, não rejeição rígida.
    // NÃO faz parte do objetivo reportado.
    double stability_penalty = 0.0;

    // Custo soft das violações de "peso decresce para cima" no convés (lashing),
    // definido pelo Evaluator. Regra de ordenação dentro da pilha; guia de busca.
    double lashing_penalty = 0.0;

    bool dirty = true;  // os kpis precisam ser recalculados antes de objective()/fitness()

    // Objetivo reportado — exatamente a Eq. (1) de Larsen & Pacino (2021): os 9
    // KPIs. É o número apples-to-apples comparado contra o baseline ALNS. Menor é
    // melhor (termos negativos são prêmios). A navegabilidade é uma flag separada
    // (ver Stability::check), não embutida aqui.
    double objective() const {
        return 1000.0    * kpis.unloaded         // KPI 1
             +  100.0    * kpis.hatch_overstow   // KPI 2
             +  100.0    * kpis.stack_overstow   // KPI 3
             +    1.0    * kpis.makespan         // KPI 4
             +    0.0001 * kpis.vert_moment      // KPI 5
             +   20.0    * kpis.block_purity     // KPI 6
             -   10.0    * kpis.empty_stacks     // KPI 7 (prêmio)
             +    5.0    * kpis.reefer_misuse    // KPI 8
             -    0.5    * kpis.below_score;     // KPI 9 (prêmio)
    }

    // Fitness de busca — o objetivo mais as penalidades soft que guiam a
    // metaheurística para planos navegáveis e válidos quanto ao lashing. Uso interno.
    double fitness() const {
        return objective() + stability_penalty + lashing_penalty;
    }
};
