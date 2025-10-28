#ifndef DMOD_MOD_DEFS_H_dmffs
#define DMOD_MOD_DEFS_H_dmffs

#include "dmod_defs.h"


#ifdef DMOD_dmffs
#  define dmod_dmffs_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmffs, MODULE, NAME)
#  define dmod_dmffs_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmffs, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmffs_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmffs , VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_dif(VERSION, RET, NAME, PARAMS)                    \
    typedef RET (*dmod_dmffs##NAME##_t) PARAMS; \
    extern const char* const DMOD_MAKE_DIF_SIG_NAME(dmffs, NAME);
#  define dmod_dmffs_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmffs, IMPL_MODULE, VERSION, RET, NAME, PARAMS)
#   define DMOD_MODULE_NAME        "dmffs"
#   define DMOD_MODULE_VERSION     "0.1"
#   define DMOD_AUTHOR_NAME        "Patryk;Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmffs
#  define dmod_dmffs_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#else 
#  define dmod_dmffs_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmffs , VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#endif
#  define dmod_dmffs_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
#  define dmod_dmffs_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmffs, VERSION, RET, NAME, PARAMS)
# ifdef ENABLE_DIF_REGISTRATIONS
#  define dmod_dmffs_dif(VERSION, RET, NAME, PARAMS)            \
                typedef RET (*dmod_dmffs##NAME##_t) PARAMS; \
                const char* const DMOD_MAKE_DIF_SIG_NAME(dmffs, NAME) = DMOD_MAKE_DIF_SIGNATURE(dmffs, VERSION, NAME);
#  else
#  define dmod_dmffs_dif(VERSION, RET, NAME, PARAMS)            \
                typedef RET (*dmod_dmffs##NAME##_t) PARAMS; \
                _DMOD_DIF_SIGNATURE_REGISTRATION(DMOD_MAKE_DIF_SIG_NAME(dmffs, NAME), DMOD_MAKE_DIF_SIGNATURE(dmffs, VERSION, NAME))
#  endif
#  define dmod_dmffs_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmffs, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmffs, IMPL_MODULE, VERSION, NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmffs, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmffs
