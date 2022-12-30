//
// Created by jay on 2022/12/29.
//

#ifndef MY_XGB_XGBOOST_OMP_H
#define MY_XGB_XGBOOST_OMP_H
#if defined(_OPENMP)
#include <omp.h>
#else
#warning "OpenMP is not available, compile to single thread code"
inline int omp_get_thread_num() { return 0; }
inline int omp_get_num_threads() { return 1; }
inline void omp_set_num_threads( int nthread ) {}
#endif
#endif //MY_XGB_XGBOOST_OMP_H
