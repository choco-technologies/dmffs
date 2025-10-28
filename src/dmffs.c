#include "dmod.h"
#include "dmffs.h"
#include "dmffs_defs.h"

static const void* g_flash_addr = NULL;
static size_t g_flash_size = 0;

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
        DMOD_LOG_ERROR("Flash address not configured. '%s' variable is not set (hex value required)\n", DMFFS_ENV_FLASH_ADDR);
        return -1;
    }
    
    // Parse flash size from hex string (e.g., "0x100000" for 1MB)
    if (flash_size_str) {
        size_t size = parse_hex_string(flash_size_str);
        g_flash_size = size;
        
        DMOD_LOG_INFO("Flash size set to: 0x%08X (%u bytes)\n", (unsigned int)size, (unsigned int)size);
    } else {
        DMOD_LOG_ERROR("Flash size not configured. '%s' variable is not set (hex value required)\n", DMFFS_ENV_FLASH_SIZE);
        return -1;
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
