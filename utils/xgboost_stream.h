//
// Created by jay on 2022/12/27.
//

#ifndef MY_XGB_XGBOOST_STREAM_H
#define MY_XGB_XGBOOST_STREAM_H
#include <cstdio>


namespace xgboost{
    namespace utils{
        class IStream{
        public:
            virtual size_t Read( void *ptr, size_t size) = 0;
            virtual void Write( const void *ptr, size_t size ) = 0;
            virtual ~IStream( void ){}
        };

        class FileStream: public IStream{
        private:
            FILE *fp;
        public:
            FileStream( FILE *fp ){
                this->fp = fp;
            }

            virtual size_t Read( void *ptr, size_t size){
                return fread( ptr, size, 1, fp );
            }

            virtual void Write( const void *ptr, size_t size){
                fwrite( ptr, size, 1, fp );
            }

            inline void Close( void ){
                fclose( fp );
            }
        };
    }// namespace utils
}//namespace xgboost

#endif //MY_XGB_XGBOOST_STREAM_H
