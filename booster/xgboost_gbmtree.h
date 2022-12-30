//
// Created by jay on 2022/12/29.
//

#ifndef MY_XGB_XGBOOST_GBMTREE_H
#define MY_XGB_XGBOOST_GBMTREE_H

#include <cstring>
#include <vector>
#include "../utils/xgboost_stream.h"
#include "../utils/xgboost_utils.h"
#include "../utils/xgb_config.h"
#include "../utils/xgboost_omp.h"
#include "../utils/xgboost_fmap.h"
#include "xgboost.h"
#include "xgboost_data.h"

namespace xgboost{
    namespace booster{
        /*!
         * \brief a base model class,
         *        that assembles the ensembles of booster together and provide single routines to do prediction buffer and update
         *        this class can be used as base code to create booster variants
         *         *
         *  relation to xgboost.h:
         *    (1) xgboost.h provides a interface to a single booster(e.g. a single regression tree )
         *        while GBMBaseModel builds upon IBooster to build a class that
         *        ensembls the boosters together;
         *    (2) GBMBaseModel provides prediction buffering scheme to speedup training;
         *    (3) Summary: GBMBaseModel is a standard wrapper for boosting ensembles;
         *
         *  Usage of this class, the number index gives calling dependencies:
         *    (1) model.SetParam to set the parameters
         *    (2) model.LoadModel to load old models or model.InitModel to create a new model
         *    (3) model.InitTrainer before calling model.Predict and model.DoBoost
         *    (4) model.Predict to get predictions given a instance
         *    (4) model.DoBoost to update the ensembles, add new booster to the model
         *    (4) model.SaveModel to save learned results
         *
         *  Bufferring: each instance comes with a buffer_index in Predict.
         *              when mparam.num_pbuffer != 0, a unique buffer index can be
         *              assigned to each instance to buffer previous results of boosters,
         *              this helps to speedup training, so consider assign buffer_index
         *              for each training instances, if buffer_index = -1, the code
         *              recalculate things from scratch and will still works correctly
         */
        class GBMBase{
        public:
            /*! \brief number of thread used */
            GBMBase( void ){}
            /*! \brief destructor */
            virtual ~GBMBase( void ){
                this->FreeSpace();
            }
            /*!
             * \brief set parameters from outside
             * \param name name of the parameter
             * \param val  value of the parameter
             */
            inline void SetParam( const char *name, const char *val ){
                if( !strncmp( name, "bst:", 4 ) ){
                    cfg.PushBack( name + 4, val );
                }
                if( !strcmp( name, "silent") ){
                    cfg.PushBack( name, val );
                }
                tparam.SetParam( name, val );
                if( boosters.size() == 0 ) mparam.SetParam( name, val );
            }
            /*!
             * \brief load model from stream
             * \param fi input stream
             */
            inline void LoadModel( utils::IStream &fi ){
                if( boosters.size() != 0 ) this->FreeSpace();
                utils::Assert( fi.Read( &mparam, sizeof(ModelParam) ) != 0 );
                boosters.resize( mparam.num_boosters );
                for( size_t i = 0; i < boosters.size(); i ++ ){
                    boosters[ i ] = booster::CreateBooster<booster::FMatrixS> ( mparam.booster_type );
                    boosters[ i ]->LoadModel( fi );
                }
                {// load info
                    booster_info.resize( mparam.num_boosters );
                    if( mparam.num_boosters != 0 ){
                        utils::Assert( fi.Read( &booster_info[0], sizeof(int)*mparam.num_boosters ) != 0 );
                    }
                }
                if( mparam.num_pbuffer != 0 ){
                    pred_buffer.resize ( mparam.num_pbuffer );
                    pred_counter.resize( mparam.num_pbuffer );
                    utils::Assert( fi.Read( &pred_buffer[0] , pred_buffer.size()*sizeof(float) ) != 0 );
                    utils::Assert( fi.Read( &pred_counter[0], pred_counter.size()*sizeof(unsigned) ) != 0 );
                }
            }
            /*!
             * \brief save model to stream
             * \param fo output stream
             */
            inline void SaveModel( utils::IStream &fo ) const {
                utils::Assert( mparam.num_boosters == (int)boosters.size() );
                fo.Write( &mparam, sizeof(ModelParam) );
                for( size_t i = 0; i < boosters.size(); i ++ ){
                    boosters[ i ]->SaveModel( fo );
                }
                if( booster_info.size() != 0 ){
                    fo.Write( &booster_info[0], sizeof(int) * booster_info.size() );
                }
                if( mparam.num_pbuffer != 0 ){
                    fo.Write( &pred_buffer[0] , pred_buffer.size()*sizeof(float) );
                    fo.Write( &pred_counter[0], pred_counter.size()*sizeof(unsigned) );
                }
            }
            /*!
             * \brief initialize the current data storage for model, if the model is used first time, call this function
             */
            inline void InitModel( void ){
                pred_buffer.clear(); pred_counter.clear();
                pred_buffer.resize ( mparam.num_pbuffer, 0.0 );
                pred_counter.resize( mparam.num_pbuffer, 0 );
                utils::Assert( mparam.num_boosters == 0 );
                utils::Assert( boosters.size() == 0 );
            }
            /*!
             * \brief initialize solver before training, called before training
             * this function is reserved for solver to allocate necessary space and do other preparation
             */
            inline void InitTrainer( void ){
                if( tparam.nthread != 0 ){
                    omp_set_num_threads( tparam.nthread );
                }
                // make sure all the boosters get the latest parameters
                for( size_t i = 0; i < this->boosters.size(); i ++ ){
                    this->ConfigBooster( this->boosters[i] );
                }
            }
            /*!
             * \brief DumpModel
             * \param fo text file
             * \param fmap feature map that may help give interpretations of feature
             * \param with_stats whether print statistics
             */
            inline void DumpModel( FILE *fo, const utils::FeatMap& fmap, bool with_stats ){
                for( size_t i = 0; i < boosters.size(); i ++ ){
                    fprintf( fo, "booster[%d]\n", (int)i );
                    boosters[i]->DumpModel( fo, fmap, with_stats );
                }
            }
            /*!
             * \brief Dump path of all trees
             * \param fo text file
             * \param data input data
             */
            inline void DumpPath( FILE *fo, const FMatrixS &data ){
                for( size_t i = 0; i < data.NumRow(); ++ i ){
                    for( size_t j = 0; j < boosters.size(); ++ j ){
                        if( j != 0 ) fprintf( fo, "\t" );
                        std::vector<int> path;
                        boosters[j]->PredPath( path, data, i );
                        fprintf( fo, "%d", path[0] );
                        for( size_t k = 1; k < path.size(); ++ k ){
                            fprintf( fo, ",%d", path[k] );
                        }
                    }
                    fprintf( fo, "\n" );
                }
            }
        public:
            /*!
             * \brief do gradient boost training for one step, using the information given
             *        Note: content of grad and hess can change after DoBoost
             * \param grad first order gradient of each instance
             * \param hess second order gradient of each instance
             * \param feats features of each instance
             * \param root_index pre-partitioned root index of each instance,
             *          root_index.size() can be 0 which indicates that no pre-partition involved
             */
            inline void DoBoost( std::vector<float> &grad,
                                 std::vector<float> &hess,
                                 const booster::FMatrixS &feats,
                                 const std::vector<unsigned> &root_index ) {
                booster::IBooster *bst = this->GetUpdateBooster();
                bst->DoBoost( grad, hess, feats, root_index );
            }
            /*!
             * \brief predict values for given sparse feature vector
             *   NOTE: in tree implementation, this is only OpenMP threadsafe, but not threadsafe
             * \param feats feature matrix
             * \param row_index  row index in the feature matrix
             * \param buffer_index the buffer index of the current feature line, default -1 means no buffer assigned
             * \param root_index root id of current instance, default = 0
             * \return prediction
             */
            inline float Predict( const FMatrixS &feats, bst_uint row_index, int buffer_index = -1, unsigned root_index = 0 ){
                size_t istart = 0;
                float  psum = 0.0f;

                // load buffered results if any
                if( mparam.do_reboost == 0 && buffer_index >= 0 ){
                    utils::Assert( buffer_index < mparam.num_pbuffer, "buffer index exceed num_pbuffer" );
                    istart = this->pred_counter[ buffer_index ];
                    psum   = this->pred_buffer [ buffer_index ];
                }

                for( size_t i = istart; i < this->boosters.size(); i ++ ){
                    psum += this->boosters[ i ]->Predict( feats, row_index, root_index );
                }
                // updated the buffered results
                if( mparam.do_reboost == 0 && buffer_index >= 0 ){
                    this->pred_counter[ buffer_index ] = static_cast<unsigned>( boosters.size() );
                    this->pred_buffer [ buffer_index ] = psum;
                }
                return psum;
            }
        public:
            //--------trial code for interactive update an existing booster------
            //-------- usually not needed, ignore this region ---------
            /*!
             * \brief same as Predict, but removes the prediction of booster to be updated
             *        this function must be called once and only once for every data with pbuffer
             */
            inline float InteractPredict( const FMatrixS &feats, bst_uint row_index, int buffer_index = -1, unsigned root_index = 0 ){
                float psum = this->Predict( feats, row_index, buffer_index, root_index );
                if( tparam.reupdate_booster != -1 ){
                    const int bid = tparam.reupdate_booster;
                    utils::Assert( bid >= 0 && bid < (int)boosters.size(), "interact:booster_index exceed existing bound" );
                    psum -= boosters[ bid ]->Predict( feats, row_index, root_index );
                    if( mparam.do_reboost == 0 && buffer_index >= 0 ){
                        this->pred_buffer[ buffer_index ] = psum;
                    }
                }
                return psum;
            }
            /*! \brief delete the specified booster */
            inline void DelteBooster( void ){
                const int bid = tparam.reupdate_booster;
                utils::Assert( bid >= 0 && bid < mparam.num_boosters , "must specify booster index for deletion");
                delete boosters[ bid ];
                for( int i = bid + 1; i < mparam.num_boosters; ++ i ){
                    boosters[i-1] = boosters[ i ];
                    booster_info[i-1] = booster_info[ i ];
                }
                boosters.resize( mparam.num_boosters -= 1 );
                booster_info.resize( boosters.size() );
            }
            /*! \brief update the prediction buffer, after booster have been updated */
            inline void InteractRePredict( const FMatrixS &feats, bst_uint row_index, int buffer_index = -1, unsigned root_index = 0 ){
                if( tparam.reupdate_booster != -1 ){
                    const int bid = tparam.reupdate_booster;
                    utils::Assert( bid >= 0 && bid < (int)boosters.size(), "interact:booster_index exceed existing bound" );
                    if( mparam.do_reboost == 0 && buffer_index >= 0 ){
                        this->pred_buffer[ buffer_index ] += boosters[ bid ]->Predict( feats, row_index, root_index );
                    }
                }
            }
            //-----------non public fields afterwards-------------
        protected:
            /*! \brief free space of the model */
            inline void FreeSpace( void ){
                for( size_t i = 0; i < boosters.size(); i ++ ){
                    delete boosters[i];
                }
                boosters.clear(); booster_info.clear(); mparam.num_boosters = 0;
            }
            /*! \brief configure a booster */
            inline void ConfigBooster( booster::IBooster *bst ){
                cfg.BeforeFirst();
                while( cfg.Next() ){
                    bst->SetParam( cfg.name(), cfg.val() );
                }
            }
            /*!
             * \brief get a booster to update
             * \return the booster created
             */
            inline booster::IBooster *GetUpdateBooster( void ){
                if( tparam.reupdate_booster != -1 ){
                    const int bid = tparam.reupdate_booster;
                    utils::Assert( bid >= 0 && bid < (int)boosters.size(), "interact:booster_index exceed existing bound" );
                    this->ConfigBooster( boosters[bid] );
                    return boosters[ bid ];
                }

                if( mparam.do_reboost == 0 || boosters.size() == 0 ){
                    mparam.num_boosters += 1;
                    boosters.push_back( booster::CreateBooster<FMatrixS>( mparam.booster_type ) );
                    booster_info.push_back( 0 );
                    this->ConfigBooster( boosters.back() );
                    boosters.back()->InitModel();
                }else{
                    this->ConfigBooster( boosters.back() );
                }
                return boosters.back();
            }
        protected:
            /*! \brief model parameters */
            struct ModelParam{
                /*! \brief number of boosters */
                int num_boosters;
                /*! \brief type of tree used */
                int booster_type;
                /*! \brief number of root: default 0, means single tree */
                int num_roots;
                /*! \brief number of features to be used by boosters */
                int num_feature;
                /*! \brief size of predicton buffer allocated for buffering boosting computation */
                int num_pbuffer;
                /*!
                 * \brief whether we repeatly update a single booster each round: default 0
                 *        set to 1 for linear booster, so that regularization term can be considered
                 */
                int do_reboost;
                /*! \brief reserved parameters */
                int reserved[ 32 ];
                /*! \brief constructor */
                ModelParam( void ){
                    num_boosters = 0;
                    booster_type = 0;
                    num_roots = num_feature = 0;
                    do_reboost = 0;
                    num_pbuffer = 0;
                    memset( reserved, 0, sizeof( reserved ) );
                }
                /*!
                 * \brief set parameters from outside
                 * \param name name of the parameter
                 * \param val  value of the parameter
                 */
                inline void SetParam( const char *name, const char *val ){
                    if( !strcmp("booster_type", name ) ){
                        booster_type = atoi( val );
                        // linear boost automatically set do reboost
                        if( booster_type == 1 ) do_reboost = 1;
                    }
                    if( !strcmp("num_pbuffer", name ) )      num_pbuffer = atoi( val );
                    if( !strcmp("do_reboost", name ) )       do_reboost  = atoi( val );
                    if( !strcmp("bst:num_roots", name ) )    num_roots = atoi( val );
                    if( !strcmp("bst:num_feature", name ) )  num_feature = atoi( val );
                }
            };
            /*! \brief training parameters */
            struct TrainParam{
                /*! \brief number of OpenMP threads */
                int nthread;
                /*!
                 * \brief index of specific booster to be re-updated, default = -1: update new booster
                 *  parameter this is part of trial interactive update mode
                 */
                int reupdate_booster;
                /*! \brief constructor */
                TrainParam( void ) {
                    nthread = 1;
                    reupdate_booster = -1;
                }
                /*!
                 * \brief set parameters from outside
                 * \param name name of the parameter
                 * \param val  value of the parameter
                 */
                inline void SetParam( const char *name, const char *val ){
                    if( !strcmp("nthread", name ) )                 nthread = atoi( val );
                    if( !strcmp("interact:booster_index", name ) )  reupdate_booster = atoi( val );
                }
            };
        protected:
            /*! \brief model parameters */
            ModelParam mparam;
            /*! \brief training parameters */
            TrainParam tparam;
        protected:
            /*! \brief component boosters */
            std::vector<booster::IBooster*> boosters;
            /*! \brief some information indicator of the booster, reserved */
            std::vector<int> booster_info;
            /*! \brief prediction buffer */
            std::vector<float>    pred_buffer;
            /*! \brief prediction buffer counter, record the progress so fart of the buffer */
            std::vector<unsigned> pred_counter;
            /*! \brief configurations saved for each booster */
            utils::ConfigSaver cfg;
        };
    };
};


#endif //MY_XGB_XGBOOST_GBMTREE_H
