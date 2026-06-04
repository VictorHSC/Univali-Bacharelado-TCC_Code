# Container Ship Stowage Planning Problem

<details open>
  <summary><h2>Descrição</h2></summary>

Programa para o **Problema de Planejamento de Estiva de Navios Porta-Contêineres**
(*Container Ship Stowage Planning Problem*, CSPP).

Dada uma **lista de carga** (contêineres a embarcar em um porto) e um **navio** (estrutura,
hidrostática, condição de chegada), o programa atribui cada contêiner a uma célula+vaga de
modo que o navio permaneça **navegável** (seaworthy) e a **eficiência portuária** seja
maximizada. O objetivo é uma soma ponderada de 9 indicadores (KPIs), seguindo o modelo de
Larsen & Pacino (2021).

O programa é **genérico**: o algoritmo de busca é intercambiável (*hot-swappable*) atrás de
uma interface comum. Hoje acompanham o projeto:

- **`csa`** — Crow Search Algorithm (metaheurística populacional), o algoritmo padrão.
- **`greedy`** — uma construção gulosa de passe único, baseline trivial de referência.

> Projeto de Trabalho de Conclusão de Curso (TCC).\
> Victor Hugo da Silva Crispim.\
> UNIVALI – Universidade do Vale de Itajaí.
</details>

<details open>
  <summary><h2>Como compilar</h2></summary>

Um único comando, sem CMake ou Make, funciona a partir de qualquer caminho:

```bash
cd Code
./build.sh          # gera build/cspp
```

Internamente é só um `clang++` (C++17, `-O3 -march=native -pthread`). O projeto é pequeno,
então a recompilação completa leva poucos segundos.

> **Pré-requisito:** um compilador C++17 (`clang++` ou `g++`). Nada além disso.

> **Com Claude Code:** a skill [`run-cspp`](.claude/skills/run-cspp/SKILL.md) compila
> automaticamente quando preciso — não é necessário rodar `./build.sh` à mão.
</details>

<details open>
  <summary><h2>Como executar</h2></summary>

> **Com Claude Code:** o jeito mais simples é a skill
> [`run-cspp`](.claude/skills/run-cspp/SKILL.md). Ela compila se preciso, aceita linguagem
> natural ("navio M, baixa utilização, instância 2, 40 corvos"), mapeia para os arquivos
> corretos, executa e resume o resultado — inclusive varreduras (*sweeps*) das 27
> instâncias. O resto desta seção descreve a invocação manual equivalente.

```bash
./build/cspp [--algo NOME] <arquivo-navio> <arquivo-instância> [population] [max_iter] [threads]
```

| Opção        | Valores                                | Padrão |
|--------------|----------------------------------------|--------|
| `--algo`     | um algoritmo registrado (veja abaixo)  | `csa`  |
| navio        | arquivo `vessel_{S,M,L}.txt`           | `S`    |
| instância    | arquivo da lista de carga              | —      |
| `population` | inteiro (corvos, só CSA)               | `20`   |
| `max_iter`   | inteiro (iterações)                    | `1000` |
| `threads`    | inteiro (1 = determinístico)           | `4`    |

Os algoritmos são descobertos em tempo de execução — para ver a lista atual:

```bash
./build/cspp --help     # a linha "Available:" lista todos os algoritmos registrados
```

### Exemplos

```bash
# CSA no navio S, instância de alta utilização (réplica 1)
./build/cspp benchmark/vessels/vessel_S.txt benchmark/instances/Vessel_S/VSHigh1.txt

# Mais busca: 40 corvos, 2000 iterações, 4 threads
./build/cspp benchmark/vessels/vessel_S.txt benchmark/instances/Vessel_S/VSHigh1.txt 40 2000 4

# Baseline guloso (ignora population/iter/threads)
./build/cspp --algo greedy benchmark/vessels/vessel_S.txt benchmark/instances/Vessel_S/VSHigh1.txt
```

O programa imprime o **objetivo (Eq. 1)**, o detalhamento dos 9 KPIs e um bloco de
**navegabilidade** (seaworthiness).

### Benchmark

As instâncias seguem o nome `V<navio><utilização><réplica>` — ex.: `VSHigh1`, `VMMed2`,
`VLLow3`. São **27 no total**: 3 navios (S/M/L) × 3 utilizações (High/Med/Low) × 3 réplicas.

```
benchmark/vessels/vessel_{S,M,L}.txt
benchmark/instances/Vessel_{S,M,L}/V{S,M,L}{High,Med,Low}{1,2,3}.txt
```
</details>

<details open>
  <summary><h2>Estrutura do projeto</h2></summary>

```
Code/
├── build.sh                  # compilação em um comando -> build/cspp
├── benchmark/
│   ├── vessels/              # arquivos de navio (S, M, L)
│   └── instances/            # 27 instâncias de lista de carga
└── src/
    ├── main.cpp              # CLI: lê argumentos, monta RunConfig, chama o algoritmo e imprime os resultados
    ├── model/                # estruturas de dados puras (sem lógica de busca)
    │   ├── Vessel.hpp        #   navio: células, pilhas, hidrostática, dados por baía
    │   ├── Container.hpp     #   tipos de contêiner e a lista de carga (Loadlist)
    │   └── Solution.hpp      #   um plano de estiva + os KPIs (objective(), fitness())
    ├── io/                   # leitura dos arquivos de entrada
    │   ├── VesselParser      #   parser do arquivo de navio (nível = quantidade de '#')
    │   └── InstanceParser    #   parser da lista de carga (resolve posições release)
    ├── problem/              # a "física" do problema, independente do algoritmo
    │   ├── Evaluator         #   calcula os 9 KPIs + penalidades de estabilidade
    │   ├── Repair            #   construção/conserto de soluções (greedy, pareamento, steering)
    │   └── Stability         #   momentos e violações de navegabilidade (LCG/TCG/shear/bending)
    └── algorithm/            # os algoritmos intercambiáveis
        ├── Algorithm.hpp     #   interface comum (name(), solve())
        ├── Factory           #   create("csa"|"greedy"|...) + available()
        ├── RunConfig.hpp     #   parâmetros de execução (seed, iter, population, ...)
        ├── crow_search/      #   CrowSearch (CSA) + Flight (operadores de voo)
        └── baseline/         #   GreedyBaseline (referência trivial)
```

A ideia central: **`model/` + `io/` + `problem/` descrevem o problema** (iguais para
qualquer algoritmo), e **`algorithm/` contém a busca**. Um novo algoritmo só mexe em
`algorithm/` — nada mais precisa mudar.
</details>

<details>
  <summary><h2>Como adicionar um novo algoritmo</h2></summary>

A interface é mínima: implementar `Algorithm` e registrar na `Factory`. Quatro passos.

**1. Criar a classe** (ex.: `src/algorithm/aco/AntColony.hpp`):

```cpp
#pragma once
#include "algorithm/Algorithm.hpp"

class AntColony : public Algorithm {
public:
    std::string name() const override { return "aco"; }
    Solution    solve(const Vessel& vessel, const Loadlist& loadlist,
                      const RunConfig& cfg) override;
};
```

**2. Implementar `solve`** (ex.: `src/algorithm/aco/AntColony.cpp`). Use os utilitários do
problema — não reimplemente a física:

```cpp
#include "algorithm/aco/AntColony.hpp"
#include "problem/Repair.hpp"      // construir/consertar soluções viáveis
#include "problem/Evaluator.hpp"   // preencher os KPIs e penalidades

Solution AntColony::solve(const Vessel& vessel, const Loadlist& loadlist,
                          const RunConfig& cfg) {
    Solution best = Repair::buildGreedy(vessel, loadlist);
    Evaluator::update(best, vessel, loadlist);   // best.fitness() já disponível

    for (int it = 0; it < cfg.max_iter; ++it) {
        // ... sua busca: gerar candidatos, avaliar com Evaluator::update,
        //     comparar por solution.fitness(), guardar o melhor ...
    }
    return best;   // a melhor solução encontrada
}
```

**3. Registrar na `Factory`** (`src/algorithm/Factory.cpp`) — duas linhas:

```cpp
#include "algorithm/aco/AntColony.hpp"
// ...
if (name == "aco") return std::make_unique<AntColony>();   // em create()
// ...
return {"csa", "greedy", "aco"};                            // em available()
```

**4. Recompilar.** O `build.sh` faz glob de `src/**/*.cpp`, então o novo arquivo é
incluído automaticamente:

```bash
./build.sh
./build/cspp --algo aco benchmark/vessels/vessel_S.txt benchmark/instances/Vessel_S/VSHigh1.txt
```

Pronto — `main.cpp`, o parser e o `--help` já reconhecem o novo algoritmo sem nenhuma outra
alteração.

### O que você precisa saber para a busca

- **`Repair::buildGreedy(vessel, loadlist)`** — devolve uma `Solution` viável inicial.
  Há também operadores de vizinhança em `problem/Repair.hpp` e `crow_search/Flight.hpp`.
- **`Evaluator::update(solution, vessel, loadlist)`** — recalcula os 9 KPIs e as
  penalidades de estabilidade da solução. Chame após qualquer modificação.
- **`solution.objective()`** — a soma ponderada dos 9 KPIs (número comparável ao baseline).
- **`solution.fitness()`** — `objective() + penalidades` (guia de busca; use isto para
  comparar candidatos durante a otimização).
- **`RunConfig`** — `max_iter`, `seed`, `threads` valem para todos; campos como
  `population`, `FL`, `AP` são específicos do CSA e podem ser ignorados pelo seu algoritmo.
</details>

<details open>
  <summary><h2>Referência</h2></summary>

Larsen, R.; Pacino, D. (2021). *A heuristic and a benchmark for the stowage planning
problem.* Maritime Economics & Logistics, 23, 94–122.
</details>