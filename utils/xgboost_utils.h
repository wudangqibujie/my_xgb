//
// Created by jay on 2022/12/25.
//

#ifndef MY_XGB_XGBOOST_UTILS_H
#define MY_XGB_XGBOOST_UTILS_H

#define _CRT_SECURE_NO_WARNINGS
#ifdef _MSC_VER
#define fopen64 fopen
#else

// use 64 bit offset, either to include this header in the beginning, or
#ifdef _FILE_OFFSET_BITS
#if _FILE_OFFSET_BITS == 32
#warning "FILE OFFSET BITS defined to be 32 bit"
#endif
#endif

#ifdef __APPLE__
#define off64_t off_t
#define fopen64 fopen
#endif

#define _FILE_OFFSET_BITS 64
extern "C"{
#include <sys/types.h>
};
#include <cstdio>
#endif

#include <cstdio>
#include <cstdlib>

namespace xgboost{
    namespace utils{
        inline void Error( const char *msg ){
            fprintf( stderr, "Error: %s\n", msg);
            exit(-1);
        }

        inline void Assert( bool exp){
            if( !exp ) Error("AssertError");
        }

        inline void Assert( bool exp, const char *msg){
            if ( !exp ) Error(msg);
        }

        inline void Warning( const char *msg ){
            fprintf(stderr, "warning: %s\n", msg);
        }

        inline FILE *FopenCheck( const char *fname, const char *flag ){
            FILE *fp = fopen64( fname, flag );
            if (fp == NULL){
                fprintf(stderr, "can not open file \"%s\"\n",fname );
                exit(-1);
            }
            return fp;
        }
    } //utils
} //xgboost


#endif //MY_XGB_XGBOOST_UTILS_H
