cmake_minimum_required(VERSION 3.22)
project(my_xgb)

set(CMAKE_CXX_STANDARD 14)

add_executable(my_xgb main.cpp utils/xgb_config.h utils/xgb_random.h utils/xgboost_utils.h regression/xgboost_reg_data.h utils/xgboost_fmap.h booster/xgboost_data.h utils/xgboost_stream.h utils/xgboost_matrix_csr.h regression/xgboost_reg.h regression/xgboost_reg_eval.h booster/xgboost_gbmtree.h utils/xgboost_omp.h booster/xgboost_ini.h booster/xgboost.h)
