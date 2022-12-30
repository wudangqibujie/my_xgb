//
// Created by jay on 2022/12/27.
//

#ifndef MY_XGB_XGBOOST_FMAP_H
#define MY_XGB_XGBOOST_FMAP_H
#include <vector>
#include <string>
#include <cstring>
#include "xgboost_utils.h"



namespace xgboost{
    namespace utils{
        class FeatMap{
        public:
            enum Type{
                kIndicator = 0,
                kQuantitive = 1,
                kInteger = 2,
                kFloat = 3
            };
        public:
            inline void LoadText( const char *fname ){
                FILE *fi = utils::FopenCheck( fname, "r" );
                this ->LoadText( fi );
                fclose(fi);
            }

            inline void LoadText( FILE *fi ){
                int fid;
                char fname[256], ftype[256];
                while ( fscanf( fi, "%d%s%s", &fid, fname, ftype ) ){
                    utils::Assert( fid == (int)names_.size(), "invalid fmap format" );
                    names_.push_back( std::string(fname) );
                    types_.push_back( GetType( ftype ) );
                }
            }

            size_t size( void ) const{
                return names_.size();
            }

            const char* name( size_t idx )const{
                utils::Assert( idx < names_.size(), "utils::FMap::name feature index exceed bound" );
                return names_[ idx ].c_str();
            }

            const Type& type( size_t idx )const{
                utils::Assert( idx < names_.size(), "utils::FMap::name feature index exceed bound" );
                return types_[ idx ];
            }

        private:
            inline static Type GetType( const char *tname ){
                if( !strcmp( "i", tname ) ) return kIndicator;
                if( !strcmp( "q", tname ) ) return kQuantitive;
                if( !strcmp( "int", tname ) ) return kInteger;
                if( !strcmp( "float", tname ) ) return kFloat;
                utils::Error("unknown feature type, use i for indicator and q for quantity");
                return kIndicator;
            }
        private:
            std::vector<std::string> names_;
            std::vector<Type> types_;
        };
    }; // utils

    namespace utils{
        class FeatConstrain{
        public:
            FeatConstrain( void ){
                default_state_ = +1;
            }
            /*!\brief set parameters */
            inline void SetParam( const char *name, const char *val ){
                int a, b;
                if( !strcmp( name, "fban") ){
                    this->ParseRange( val, a, b );
                    this->SetRange( a, b, -1 );
                }
                if( !strcmp( name, "fpass") ){
                    this->ParseRange( val, a, b );
                    this->SetRange( a, b, +1 );
                }
                if( !strcmp( name, "fdefault") ){
                    default_state_ = atoi( val );
                }
            }
            /*! \brief whether constrain is specified */
            inline bool HasConstrain( void ) const {
                return state_.size() != 0 && default_state_ == 1;
            }
            /*! \brief whether a feature index is banned or not */
            inline bool NotBanned( unsigned index ) const{
                int rt = index < state_.size() ? state_[index] : default_state_;
                if( rt == 0 ) rt = default_state_;
                return rt == 1;
            }
        private:
            inline void SetRange( int a, int b, int st ){
                if( b > (int)state_.size() ) state_.resize( b, 0 );
                for( int i = a; i < b; ++ i ){
                    state_[i] = st;
                }
            }
            inline void ParseRange( const char *val, int &a, int &b ){
                if( sscanf( val, "%d-%d", &a, &b ) == 2 ) return;
                utils::Assert( sscanf( val, "%d", &a ) == 1 );
                b = a + 1;
            }
            /*! \brief default state */
            int default_state_;
            /*! \brief whether the state here is, +1:pass, -1: ban, 0:default */
            std::vector<int> state_;
        };
    }// utils


} // xgboost



#endif //MY_XGB_XGBOOST_FMAP_H
