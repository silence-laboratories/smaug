#include <cstring>
#include <iostream>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "mpc/mpc.h"

// Private set intersection CARDINALITY: reveal only |X ∩ Y| (a count), not which
// elements matched. The reveal-view leaks `cardinality` to both parties — the
// annotated manifest (benchmarks/metadata/psi_cardinality.ll.json) matches the
// ap3-manifest golden `psi_cardinality` vector.
void psi_cardinality(int *X, int *Y, int N, int *count) {
  MPC::setNumGates();
  int c = 0;
  for (int i = 0; i < N; ++i) {
    bool found = false;
    for (int j = 0; j < N; ++j) {
      if (X[i] == Y[j])
        found = true;
    }
    if (found)
      c++;
  }
  count[0] = c;
  std::cout << "(" << N << ") psi_cardinality: " << MPC::getNumGates() << "\n";
}

int main(int argc, char **argv) {
  time_t t;
  srand((unsigned)time(&t));
  MPC::setup(atoi(argv[1]), atoi(argv[2]));

  int32_t N = atoi(argv[3]);
  int32_t *X = (int32_t *)malloc(sizeof(int32_t) * N);
  int32_t *Y = (int32_t *)malloc(sizeof(int32_t) * N);
  int32_t *count = (int32_t *)malloc(sizeof(int32_t));
  memset(X, 0, sizeof(int32_t) * N);
  memset(Y, 0, sizeof(int32_t) * N);
  *count = 0;
  for (int32_t i = 0; i < N; ++i) {
    X[i] = rand() % 1000;
    Y[i] = rand() % 1000;
  }

  psi_cardinality(X, Y, N, count);

  MPC::finish();
}
