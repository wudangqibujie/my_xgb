#include <iostream>
#include "utils/xgb_random.h"
#include "utils/xgb_config.h"
#include "utils/xgboost_fmap.h"
#include "regression/xgboost_reg_data.h"
#include "regression/xgboost_reg.h"
#include <cstring>
#include "cstdio"


namespace xgboost{
    namespace regression{
        class RegBoostTask{
        public:
            inline int Run( int argc, char *argv[] ){
                if ( argc < 2 ){
                    printf("Usage: <config>\n");
                    return 0;
                }
                xgboost::utils::ConfigIterator itr( argv[1] );
                while ( itr.Next() ){
//                    std::cout << itr.name() << ":  " << itr.val() << std::endl;
                    this -> SetParam( itr.name(), itr.val() );
//                    std::cout << cfg.get_name_size() << std::endl;
                }

                for (int i = 2; i < argc; ++i) {
                    char name[256], val[256];
                    if( sscanf( argv[i], "%[^=]=%s", name, val ) == 2 ){
                        this->SetParam( name, val );
                    }
                }
                this->InitData();
                this->InitLearner();
                if( task == "dump" ){
                    this->TaskDump();
                    return 0;
                }
                if( task == "interact" ){
                    this->TaskInteractive(); return 0;
                }
                if( task == "dumppath" ){
                    this->TaskDumpPath(); return 0;
                }
                if( task == "eval" ){
                    this->TaskEval(); return 0;
                }
                if( task == "pred" ){
                    this->TaskPred();
                }else{
                    this->TaskTrain();
                }
                return 0;
            }

            inline void SetParam( const char *name, const char *val ){
                if ( !strcmp("silent", name ) )             silent = atoi( val );
                if ( !strcmp("use_buffer", name ) )         use_buffer = atoi( val );
                if ( !strcmp("seed", name ) )               xgboost::random::Seed( atoi(val) );
                if ( !strcmp("num_round", name) )           num_round = atoi( val );
                if ( !strcmp("save_period", name) )         save_period = atoi( val );
                if ( !strcmp("task", name) )                task = val;
                if ( !strcmp("data", name) )                train_path = val;
                if ( !strcmp("test:data", name) )           test_path = val;
                if ( !strcmp("model_in", name) )            model_in = val;
                if ( !strcmp("model_out",  name ) )         model_out   = val;
                if ( !strcmp("model_dir", name ) )          model_dir_path = val;
                if ( !strcmp("fmap",  name ) )              name_fmap = val;
                if ( !strcmp("name_dump",  name ) )         name_dump = val;
                if ( !strcmp("name_dumppath",  name ) )     name_dumppath = val;
                if ( !strcmp("name_pred",  name ) )         name_pred = val;
                if ( !strcmp("dump_stats", name ) )         dump_model_stats = atoi( val );
                if ( !strcmp("interact:action",  name ) )   interact_action = val;
                if ( !strncmp("batch:", name, 6 ) ){
                    cfg_batch.PushBack( name + 6, val );
                }
                if ( !strncmp("eval[", name, 5 ) ){
                    char evname[ 256 ];
                    utils::Assert( sscanf( name, "eval[%[^]]", evname ) == 1, "must specify evaluation name for display");
                    eval_data_names.push_back( std::string( evname ) );
                    eval_data_paths.push_back( std::string( val ) );
                }
                cfg.PushBack( name, val );
            }
        public:
            RegBoostTask( void ){
                silent = 0;
                use_buffer = 1;
                num_round = 10;
                save_period = 0;
                dump_model_stats = 0;
                task = "train";
                model_in = "NULL";
                model_out = "NULL";
                name_fmap = "NULL";
                name_pred = "pred.txt";
                name_dump = "pred.dump";
                name_dumppath = "dump.path.txt";
                model_dir_path = "./";
                interact_action = "update";
            }
            ~RegBoostTask( void ){
            }
        private:
            inline void InitData( void ){

                if( name_fmap != "NULL" ) fmap.LoadText( name_fmap.c_str() );
                if( task == "dump" ) return;
                if( task == "pred" || task == "dumppath" ){
                    data.CacheLoad( test_path.c_str(), silent!=0, use_buffer!=0 );
                }else{
                    // training
                    data.CacheLoad( train_path.c_str(), silent!=0, use_buffer!=0 );
                    utils::Assert( eval_data_names.size() == eval_data_paths.size() );
                    for( size_t i = 0; i < eval_data_names.size(); ++ i ){
                        deval.push_back( new DMatrix() );
                        deval.back()->CacheLoad( eval_data_paths[i].c_str(), silent!=0, use_buffer!=0 );
                    }
                }
                learner.SetData( &data, deval, eval_data_names );
            }

            inline void InitLearner( void ){
                cfg.BeforeFirst();
                while( cfg.Next() ){
                    learner.SetParam( cfg.name(), cfg.val() );
                }
                if( model_in != "NULL" ){
                    utils::FileStream fi( utils::FopenCheck( model_in.c_str(), "rb") );
                    learner.LoadModel( fi );
                    fi.Close();
                }else{
                    utils::Assert( task == "train", "model_in not specified" );
                    learner.InitModel();
                }
                learner.InitTrainer();}

            inline void TaskTrain( void ){
                const time_t start    = time( NULL );
                unsigned long elapsed = 0;
                for( int i = 0; i < num_round; ++ i ){
                    elapsed = (unsigned long)(time(NULL) - start);
                    if( !silent ) printf("boosting round %d, %lu sec elapsed\n", i , elapsed );
                    learner.UpdateOneIter( i );
                    learner.EvalOneIter( i );
                    if( save_period != 0 && (i+1) % save_period == 0 ){
                        this->SaveModel( i );
                    }
                    elapsed = (unsigned long)(time(NULL) - start);
                }
                // always save final round
                if( save_period == 0 || num_round % save_period != 0 ){
                    if( model_out == "NULL" ){
                        this->SaveModel( num_round - 1 );
                    }else{
                        this->SaveModel( model_out.c_str() );
                    }
                }
                if( !silent ){
                    printf("\nupdating end, %lu sec in all\n", elapsed );
                }
            }

            inline void TaskEval( void ){
                learner.EvalOneIter( 0 );
            }

            inline void TaskInteractive( void ){
                const time_t start    = time( NULL );
                unsigned long elapsed = 0;
                int batch_action = 0;

                cfg_batch.BeforeFirst();
                while( cfg_batch.Next() ){
                    if( !strcmp( cfg_batch.name(), "run" ) ){
                        learner.UpdateInteract( interact_action );
                        batch_action += 1;
                    } else{
                        learner.SetParam( cfg_batch.name(), cfg_batch.val() );
                    }
                }

                if( batch_action == 0 ){
                    learner.UpdateInteract( interact_action );
                }
                utils::Assert( model_out != "NULL", "interactive mode must specify model_out" );
                this->SaveModel( model_out.c_str() );
                elapsed = (unsigned long)(time(NULL) - start);

                if( !silent ){
                    printf("\ninteractive update, %d batch actions, %lu sec in all\n", batch_action, elapsed );
                }
            }

            inline void TaskDump( void ){
                FILE *fo = utils::FopenCheck( name_dump.c_str(), "w" );
                learner.DumpModel( fo, fmap, dump_model_stats != 0 );
                fclose( fo );
            }
            inline void TaskDumpPath( void ){
                FILE *fo = utils::FopenCheck( name_dumppath.c_str(), "w" );
                learner.DumpPath( fo, data );
                fclose( fo );
            }

            inline void SaveModel( const char *fname ) const{
                utils::FileStream fo( utils::FopenCheck( fname, "wb" ) );
                learner.SaveModel( fo );
                fo.Close();
            }
            inline void SaveModel( int i ) const{
                char fname[256];
                sprintf( fname ,"%s/%04d.model", model_dir_path.c_str(), i+1 );
                this->SaveModel( fname );
            }

            inline void TaskPred( void ){
                std::vector<float> preds;
                if( !silent ) printf("start prediction...\n");
                learner.Predict( preds, data );
                if( !silent ) printf("writing prediction to %s\n", name_pred.c_str() );
                FILE *fo = utils::FopenCheck( name_pred.c_str(), "w" );
                for( size_t i = 0; i < preds.size(); i ++ ){
                    fprintf( fo, "%f\n", preds[i] );
                }
                fclose( fo );
            }
        private:
            int silent;
            int use_buffer;
            int num_round;
            int save_period;
            std::string interact_action;
            std::string train_path, test_path;
            std::string model_in;
            std::string model_out;
            std::string model_dir_path;
            std::string task;
            std::string name_pred;
            int dump_model_stats;
            std::string name_fmap;
            std::string name_dump;
            std::string name_dumppath;
            std::vector<std::string> eval_data_paths;
            std::vector<std::string> eval_data_names;
            xgboost::utils::ConfigSaver cfg;
            xgboost::utils::ConfigSaver cfg_batch;
        private:
            DMatrix data;
            std::vector<DMatrix*> deval;
            utils::FeatMap fmap;
            RegBoostLearner learner;

        };
    } // regression
}// xgboost



int main(int argc, char *argv[]) {
    std::cout << "Arg Num: " << argc << std::endl;
    std::cout << argv[1] << std::endl;

    xgboost::random::Seed(0);
    xgboost::regression::RegBoostTask task;
    task.Run( argc, argv );
    return 0;
}
