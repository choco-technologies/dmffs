#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod.h"
#include "dmffs.h"
#include "dmffs_defs.h"
#include "dmfsi.h"
#include "string.h"

static const void* g_flash_addr = NULL;     //!< default flash address
static size_t g_flash_size = 0;             //!< default flash size

#define MAGIC_DMFSS_CTX 0x444D4653  // 'DMFS'

/**
 * @brief DMFSI context structure
 */
struct dmfsi_context
{
    uint32_t magic;             //!< magic number for validation
    const void* flash_addr;     //!< flash base address
    size_t flash_size;          //!< flash size in bytes
};

/**
 * @brief Simple hex string parser for embedded systems
 * 
 * @param hex_str String in format "0x1234ABCD" or "1234ABCD"
 * @return Parsed value or 0 if parsing failed
 */
static size_t parse_hex_string(const char* hex_str)
{
    if (!hex_str) return 0;
    
    size_t result = 0;
    const char* ptr = hex_str;
    
    // Skip "0x" prefix if present
    if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
        ptr += 2;
    }
    
    // Parse hex digits
    while (*ptr) {
        char c = *ptr;
        int digit_value = -1;
        
        if (c >= '0' && c <= '9') {
            digit_value = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            digit_value = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            digit_value = c - 'a' + 10;
        } else {
            // Invalid character, stop parsing
            DMOD_LOG_ERROR("Invalid character in hex (%s) string: '%c'\n", hex_str, c);
            break;
        }
        
        result = (result << 4) | digit_value;
        ptr++;
    }
    
    return result;
}

/**
 * @brief Parse configuration string for DMFFS
 * 
 * @param ctx DMFSI context
 * @param config Configuration string
 * @return true if parsing was successful, false otherwise
 */
static bool parse_config_string( dmfsi_context_t ctx, const char* config )
{
    // Example config string: "flash_addr=0x08000000;flash_size=0x100000"
    const char* ptr = config;
    while (*ptr) {
        // Parse key
        const char* key_start = ptr;
        while (*ptr && *ptr != '=' && *ptr != ';') ptr++;
        size_t key_len = ptr - key_start;
        
        if (*ptr != '=') {
            // Invalid format
            DMOD_LOG_ERROR("Invalid config string format: '%s'\n", config);
            return false;
        }
        ptr++; // Skip '='
        
        // Parse value
        const char* value_start = ptr;
        while (*ptr && *ptr != ';') ptr++;
        size_t value_len = ptr - value_start;
        
        // Process key-value pair
        if (key_len == 10 && strncmp(key_start, "flash_addr", 10) == 0) {
            char value_str[20] = {0};
            strncpy(value_str, value_start, value_len < 19 ? value_len : 19);
            ctx->flash_addr = (const void*)parse_hex_string(value_str);
        } else if (key_len == 10 && strncmp(key_start, "flash_size", 10) == 0) {
            char value_str[20] = {0};
            strncpy(value_str, value_start, value_len < 19 ? value_len : 19);
            ctx->flash_size = parse_hex_string(value_str);
        } else {
            DMOD_LOG_WARN("Unknown config key: '%.*s'\n", (int)key_len, key_start);
        }
        
        if (*ptr == ';') ptr++; // Skip ';'
    }
    
    return true;
}

/**
 * @brief Pre-initialization function for the module.
 * 
 * @note This function is optional. You can remove it if you don't need it.
 * 
 * This function is called when the module enabling is in progress.
 * 
 * You can use this function to load the required dependencies, such as 
 * other modules. Please be aware that the module is not fully initialized, 
 * so not all the API functions are available - you can check if the API
 * is connected by calling the Dmod_IsFunctionConnected() function.
 */
void dmod_preinit(void)
{
    if(Dmod_IsFunctionConnected( Dmod_Printf ))
    {
        Dmod_Printf("API is connected!\n");
    }
}

/**
 * @brief Initialization function for the module.
 * 
 * This function is called when the module is enabled.
 * Please use this function to initialize the module, for instance:
 * - initialize the module variables
 * - initialize the module hardware
 * - allocate memory
 */
int dmod_init(const Dmod_Config_t *Config)
{
    const char* flash_addr_str = Dmod_GetEnv( DMFFS_ENV_FLASH_ADDR );
    const char* flash_size_str = Dmod_GetEnv( DMFFS_ENV_FLASH_SIZE );
    
    // Parse flash address from hex string (e.g., "0x08000000")
    if (flash_addr_str) {
        size_t addr = parse_hex_string(flash_addr_str);
        g_flash_addr = (const void*)addr;
        
        DMOD_LOG_INFO("Flash address set to: 0x%08X\n", (unsigned int)addr);
    } else {
        DMOD_LOG_WARN("Flash address not configured. '%s' variable is not set (hex value required)\n", DMFFS_ENV_FLASH_ADDR);
    }
    
    // Parse flash size from hex string (e.g., "0x100000" for 1MB)
    if (flash_size_str) {
        size_t size = parse_hex_string(flash_size_str);
        g_flash_size = size;
        
        DMOD_LOG_INFO("Flash size set to: 0x%08X (%u bytes)\n", (unsigned int)size, (unsigned int)size);
    } else {
        DMOD_LOG_WARN("Flash size not configured. '%s' variable is not set (hex value required)\n", DMFFS_ENV_FLASH_SIZE);
    }
    
    return 0;
}

/**
 * @brief De-initialization function for the module.
 * 
 * This function is called when the module is disabled.
 * Please use this function to de-initialize the module, for instance:
 * - free memory
 * - de-initialize the module hardware
 * - de-initialize the module variables
 */
int dmod_deinit(void)
{
    Dmod_Printf("Goodbye, World!\n");
    return 0;
}

/**
 * @brief Initialize the file system
 * 
 * The function initializes the DMFFS file system with the provided configuration string.
 * The configuration string can specify flash parameters such as address and size, in format:
 * "flash_addr=0x08000000;flash_size=0x100000"
 * 
 * If no configuration string is provided, default parameters from environment variables. 
 * 
 * @param config (optional) Configuration string (FS-specific)
 * @return Context pointer on success, NULL on failure
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, dmfsi_context_t, _init, (const char* config) )
{
    dmfsi_context_t ctx = Dmod_Malloc( sizeof(struct dmfsi_context) );
    if(!ctx)
    {
        DMOD_LOG_ERROR("Failed to allocate DMFFS context\n");
        return NULL;
    }

    // Set default flash parameters
    ctx->magic = MAGIC_DMFSS_CTX;
    ctx->flash_addr = g_flash_addr;
    ctx->flash_size = g_flash_size;

    // Parse configuration string
    if (config && !parse_config_string(ctx, config)) {
        DMOD_LOG_ERROR("Failed to parse DMFFS configuration string: '%s'\n", config);
        Dmod_Free(ctx);
        return NULL;
    }

    return ctx;
}

/**
 * @brief Validate the file system context
 * @param ctx File system context
 * @return 1 if context is valid, 0 otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return ctx != NULL && ctx->magic == MAGIC_DMFSS_CTX;
}

/**
 * @brief Deinitialize the file system
 * @param ctx File system context
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _deinit, (dmfsi_context_t ctx) )
{
    if(dmfsi_dmffs_context_is_valid(ctx) == 0){
        return DMFSI_ERR_INVALID;
    }

    Dmod_Free( ctx );
    return DMFSI_OK;
}

/**
 * @brief Open a file
 * @param ctx File system context
 * @param fp Pointer to store the file handle
 * @param path Path to the file
 * @param mode Open mode (DMFSI_O_*)
 * @param attr File attributes (DMFSI_ATTR_*)
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _lseek, (dmfsi_context_t ctx, void* fp, long offset, int whence) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _tell, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _size, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _error, (dmfsi_context_t ctx, void* fp) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    return DMFSI_OK;
}

// Close directory

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    return DMFSI_OK;
}

// Create directory (only root supported)
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _mkdir, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return DMFSI_ERR_NO_SPACE;
}

// Check if directory exists
dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _chmod, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _utime, (dmfsi_context_t ctx, const char* path, uint32_t atime, uint32_t mtime) )
{
    // Not implemented, just return OK
    return DMFSI_OK;
}
