#pragma once

// Parâmetros de execução compartilhados por todo solver. Campos específicos de
// algoritmo (population, FL, AP) são simplesmente ignorados pelos que não os usam,
// mantendo a interface comum pequena e ainda configurável.
struct RunConfig {
    int  seed     = 42;
    int  max_iter = 1000;
    bool verbose  = true;
    int  threads  = 4;        // instâncias CSA independentes em paralelo (L&P usou 4)

    // Parâmetros do Crow Search Algorithm.
    int    population = 20;   // número de corvos
    double FL         = 0.2;  // flight length — fração de pilhas por movimento
    double AP         = 0.1;  // awareness probability — chance de voo aleatório
};
