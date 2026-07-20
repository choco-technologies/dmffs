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
 * @brief File entry metadata parsed from TLV
 */
typedef struct {
    char name[256];             //!< file name
    uint32_t data_offset;       //!< offset to file data in flash
    uint32_t data_size;         //!< size of file data
    uint32_t attr;              //!< file attributes
    uint32_t mtime;             //!< modification time
    uint32_t ctime;             //!< creation time
} dmffs_file_entry_t;

/**
 * @brief File handle structure
 */
typedef struct {
    dmffs_file_entry_t entry;   //!< file entry metadata
    long position;              //!< current read position
    dmfsi_context_t ctx;        //!< file system context
} dmffs_file_handle_t;

/**
 * @brief Directory handle structure
 */
typedef struct {
    dmfsi_context_t ctx;        //!< file system context
    uint32_t current_offset;    //!< current offset in flash for scanning
    int entry_index;            //!< current entry index
    char path[256];             //!< current directory path
    bool in_dir;                //!< true if currently inside a DIR entry
    uint32_t dir_end_offset;    //!< end offset of current DIR
} dmffs_dir_handle_t;

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

    /* Auto-detect base: "0x..." or "0X..." → hex, otherwise → decimal */
    if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
        ptr += 2;
        while (*ptr) {
            char c = *ptr;
            int digit;
            if (c >= '0' && c <= '9')      digit = c - '0';
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else { DMOD_LOG_ERROR("Invalid hex char in '%s': '%c'\n", hex_str, c); break; }
            result = (result << 4) | digit;
            ptr++;
        }
    } else {
        while (*ptr >= '0' && *ptr <= '9') {
            result = result * 10 + (*ptr - '0');
            ptr++;
        }
        if (*ptr != '\0') {
            DMOD_LOG_ERROR("Invalid numeric string: '%s'\n", hex_str);
        }
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
    // Also supports: "addr=0x08000000,size=0x100000"
    const char* ptr = config;
    while (*ptr) {
        // Parse key
        const char* key_start = ptr;
        while (*ptr && *ptr != '=' && *ptr != ';' && *ptr != ',') ptr++;
        size_t key_len = ptr - key_start;
        
        if (*ptr != '=') {
            // Invalid format
            DMOD_LOG_ERROR("Invalid config string format: '%s'\n", config);
            return false;
        }
        ptr++; // Skip '='
        
        // Parse value
        const char* value_start = ptr;
        while (*ptr && *ptr != ';' && *ptr != ',') ptr++;
        size_t value_len = ptr - value_start;
        
        // Process key-value pair - support both long and short key names
        if ((key_len == 10 && strncmp(key_start, "flash_addr", 10) == 0) ||
            (key_len == 4 && strncmp(key_start, "addr", 4) == 0)) {
            char value_str[20] = {0};
            strncpy(value_str, value_start, value_len < 19 ? value_len : 19);
            ctx->flash_addr = (const void*)parse_hex_string(value_str);
        } else if ((key_len == 10 && strncmp(key_start, "flash_size", 10) == 0) ||
                   (key_len == 4 && strncmp(key_start, "size", 4) == 0)) {
            char value_str[20] = {0};
            strncpy(value_str, value_start, value_len < 19 ? value_len : 19);
            ctx->flash_size = parse_hex_string(value_str);
        } else {
            DMOD_LOG_WARN("Unknown config key: '%.*s'\n", (int)key_len, key_start);
        }
        
        // Skip separator (semicolon or comma)
        if (*ptr == ';' || *ptr == ',') ptr++;
    }
    
    return true;
}

/**
 * @brief Read a TLV header from flash
 * 
 * @param ctx File system context
 * @param offset Offset in flash to read from
 * @param type Pointer to store TLV type
 * @param length Pointer to store TLV length
 * @return true if successful, false otherwise
 */
static bool read_tlv_header(dmfsi_context_t ctx, uint32_t offset, uint32_t* type, uint32_t* length)
{
    if (!ctx || !type || !length) return false;
    
    uintptr_t flash_addr = (uintptr_t)ctx->flash_addr + offset;
    
    // Read type (4 bytes)
    if (Dmod_ReadMemory(flash_addr, type, sizeof(uint32_t)) != sizeof(uint32_t)) {
        return false;
    }
    
    // Read length (4 bytes)
    if (Dmod_ReadMemory(flash_addr + sizeof(uint32_t), length, sizeof(uint32_t)) != sizeof(uint32_t)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read TLV value from flash
 * 
 * @param ctx File system context
 * @param offset Offset in flash (after header)
 * @param buffer Buffer to store value
 * @param length Number of bytes to read
 * @return Number of bytes read
 */
static size_t read_tlv_value(dmfsi_context_t ctx, uint32_t offset, void* buffer, uint32_t length)
{
    if (!ctx || !buffer || length == 0) return 0;
    
    uintptr_t flash_addr = (uintptr_t)ctx->flash_addr + offset;
    return Dmod_ReadMemory(flash_addr, buffer, length);
}

/**
 * @brief Parse a file entry from TLV structure
 * 
 * @param ctx File system context
 * @param offset Offset to FILE TLV entry in flash
 * @param entry Pointer to store parsed file entry
 * @return Offset to next TLV entry, or 0 on error
 */
static uint32_t parse_file_entry(dmfsi_context_t ctx, uint32_t offset, dmffs_file_entry_t* entry)
{
    if (!ctx || !entry) return 0;
    
    uint32_t type, length;
    if (!read_tlv_header(ctx, offset, &type, &length)) {
        return 0;
    }
    
    if (type != DMFFS_TLV_TYPE_FILE) {
        return 0;
    }
    
    // Initialize entry
    memset(entry, 0, sizeof(dmffs_file_entry_t));
    entry->attr = DMFSI_ATTR_READONLY;
    
    // Parse nested TLVs within FILE entry
    uint32_t nested_offset = offset + 8; // Skip FILE TLV header
    uint32_t end_offset = offset + 8 + length;
    
    while (nested_offset < end_offset) {
        uint32_t nested_type, nested_length;
        if (!read_tlv_header(ctx, nested_offset, &nested_type, &nested_length)) {
            break;
        }
        
        uint32_t value_offset = nested_offset + 8;
        
        switch (nested_type) {
            case DMFFS_TLV_TYPE_NAME:
                if (nested_length > 0 && nested_length <= sizeof(entry->name) - 1) {
                    read_tlv_value(ctx, value_offset, entry->name, nested_length);
                    entry->name[nested_length] = '\0';
                } else if (nested_length > sizeof(entry->name) - 1) {
                    // Truncate to fit
                    read_tlv_value(ctx, value_offset, entry->name, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                } else {
                    // Zero-length name
                    entry->name[0] = '\0';
                }
                break;
                
            case DMFFS_TLV_TYPE_DATA:
                entry->data_offset = value_offset;
                entry->data_size = nested_length;
                break;
                
            case DMFFS_TLV_TYPE_DATE:
                if (nested_length >= sizeof(uint32_t)) {
                    read_tlv_value(ctx, value_offset, &entry->mtime, sizeof(uint32_t));
                    entry->ctime = entry->mtime;
                }
                break;
                
            case DMFFS_TLV_TYPE_ATTR:
                if (nested_length >= sizeof(uint32_t)) {
                    read_tlv_value(ctx, value_offset, &entry->attr, sizeof(uint32_t));
                }
                break;
                
            // Skip OWNER, GROUP and unknown tags
            default:
                break;
        }
        
        nested_offset += 8 + nested_length;
    }
    
    return end_offset;
}

/**
 * @brief Locate the flash offset range spanning a directory's contents,
 * walking one path segment at a time through nested DIR TLVs.
 *
 * Resolving multi-segment paths (e.g. "drivers/dmclk") by comparing the
 * whole remaining path against a single DIR TLV's name only ever matches
 * one level of nesting - deeper directories would silently appear empty
 * or missing. Walking segment-by-segment, narrowing the scan range to the
 * matched DIR's own contents before looking for the next segment, makes
 * nesting depth unlimited.
 *
 * @param ctx File system context
 * @param dir_path Directory path relative to root, no leading/trailing slash (NULL or empty means root)
 * @param out_start Set to the offset of the first TLV inside the directory
 * @param out_end Set to the offset just past the last TLV inside the directory
 * @param out_attr Optional: set to the resolved directory's ATTR TLV value (defaults to DIR|READONLY)
 * @param out_date Optional: set to the resolved directory's DATE TLV value (defaults to 0)
 * @return true if the directory was found (root is always found)
 */
static bool find_directory_range(dmfsi_context_t ctx, const char* dir_path,
                                  uint32_t* out_start, uint32_t* out_end,
                                  uint32_t* out_attr, uint32_t* out_date)
{
    uint32_t offset = 0;
    uint32_t end = ctx->flash_size;
    uint32_t type, length;

    if (read_tlv_header(ctx, offset, &type, &length) && type == DMFFS_TLV_TYPE_VERSION) {
        offset += 8 + length;
    }

    if (out_attr) *out_attr = DMFSI_ATTR_DIRECTORY | DMFSI_ATTR_READONLY;
    if (out_date) *out_date = 0;

    if (!dir_path || dir_path[0] == '\0') {
        *out_start = offset;
        *out_end = end;
        return true;
    }

    char path_copy[256];
    strncpy(path_copy, dir_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* segment = path_copy;
    while (segment != NULL && *segment != '\0') {
        char* next_slash = strchr(segment, '/');
        if (next_slash) {
            *next_slash = '\0';
        }

        bool found = false;
        while (offset < end) {
            if (!read_tlv_header(ctx, offset, &type, &length)) {
                break;
            }
            if (type == DMFFS_TLV_TYPE_END || type == DMFFS_TLV_TYPE_INVALID) {
                break;
            }

            if (type == DMFFS_TLV_TYPE_DIR) {
                uint32_t nested_offset = offset + 8;
                uint32_t dir_end = offset + 8 + length;
                char dir_name[256] = {0};
                uint32_t dir_attr = DMFSI_ATTR_DIRECTORY | DMFSI_ATTR_READONLY;
                uint32_t dir_date = 0;

                while (nested_offset < dir_end) {
                    uint32_t nested_type, nested_length;
                    if (!read_tlv_header(ctx, nested_offset, &nested_type, &nested_length)) {
                        break;
                    }
                    uint32_t value_offset = nested_offset + 8;

                    if (nested_type == DMFFS_TLV_TYPE_NAME && nested_length > 0 &&
                        nested_length <= sizeof(dir_name) - 1) {
                        read_tlv_value(ctx, value_offset, dir_name, nested_length);
                        dir_name[nested_length] = '\0';
                    } else if (nested_type == DMFFS_TLV_TYPE_ATTR && nested_length >= sizeof(uint32_t)) {
                        read_tlv_value(ctx, value_offset, &dir_attr, sizeof(uint32_t));
                        dir_attr |= DMFSI_ATTR_DIRECTORY;
                    } else if (nested_type == DMFFS_TLV_TYPE_DATE && nested_length >= sizeof(uint32_t)) {
                        read_tlv_value(ctx, value_offset, &dir_date, sizeof(uint32_t));
                    }

                    nested_offset += 8 + nested_length;
                }

                if (strcmp(dir_name, segment) == 0) {
                    offset = offset + 8;
                    end = dir_end;
                    found = true;
                    if (out_attr) *out_attr = dir_attr;
                    if (out_date) *out_date = dir_date;
                    break;
                }
            }

            offset += 8 + length;
        }

        if (!found) {
            return false;
        }

        segment = next_slash ? next_slash + 1 : NULL;
    }

    *out_start = offset;
    *out_end = end;
    return true;
}

/**
 * @brief Search for a file by path, supporting directories nested at any depth
 *
 * @param ctx File system context
 * @param path Full path to search for (e.g., "dir/subdir/file.txt" or "file.txt")
 * @param entry Pointer to store found file entry
 * @return true if file found, false otherwise
 */
static bool find_file_by_path(dmfsi_context_t ctx, const char* path, dmffs_file_entry_t* entry)
{
    if (!ctx || !path || !entry) return false;

    // Parse the path to get directory components
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Split path into directory and filename
    char* last_slash = strrchr(path_copy, '/');
    const char* filename = last_slash ? last_slash + 1 : path_copy;
    if (last_slash) {
        *last_slash = '\0'; // Terminate directory path
    }

    uint32_t offset, end;
    if (!find_directory_range(ctx, last_slash ? path_copy : "", &offset, &end, NULL, NULL)) {
        return false; // Directory not found
    }

    uint32_t type, length;
    while (offset < end) {
        if (!read_tlv_header(ctx, offset, &type, &length)) {
            break;
        }

        if (type == DMFFS_TLV_TYPE_END || type == DMFFS_TLV_TYPE_INVALID) {
            break;
        }

        if (type == DMFFS_TLV_TYPE_FILE) {
            uint32_t next_offset = parse_file_entry(ctx, offset, entry);

            if (next_offset == 0) {
                // Parse error - skip this TLV entry manually
                offset += 8 + length;
            } else if (strcmp(entry->name, filename) == 0) {
                return true;
            } else {
                offset = next_offset;
            }
        } else {
            offset += 8 + length;
        }
    }

    return false;
}

/**
 * @brief Check if flash contains valid TLV structure
 * 
 * @param ctx File system context
 * @return true if valid TLV structure found, false otherwise
 */
static bool has_valid_tlv_structure(dmfsi_context_t ctx)
{
    if (!ctx || !ctx->flash_addr) return false;
    
    uint32_t type, length;
    // Try to read first TLV header
    if (!read_tlv_header(ctx, 0, &type, &length)) {
        return false;
    }
    
    // Check for VERSION tag at start
    if (type == DMFFS_TLV_TYPE_VERSION) {
        return true;
    }
    
    // Or check for FILE/DIR tag
    if (type == DMFFS_TLV_TYPE_FILE || type == DMFFS_TLV_TYPE_DIR) {
        return true;
    }
    
    return false;
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
 * "flash_addr=0x08000000;flash_size=0x100000" or "addr=0x08000000,size=0x100000"
 * Both semicolon and comma separators are supported.
 * 
 * If no configuration string is provided, default parameters are loaded from environment variables. 
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
    if (!ctx || !fp || !path) {
        return DMFSI_ERR_INVALID;
    }
    
    // Only support read operations
    // Check if any write/create/trunc flags are set
    int access_mode = mode & 0x0003; // Mask out access mode bits
    if (access_mode == DMFSI_O_WRONLY || access_mode == DMFSI_O_RDWR ||
        (mode & (DMFSI_O_CREAT | DMFSI_O_TRUNC))) {
        return DMFSI_ERR_INVALID;
    }
    
    // Remove leading slash if present
    if (path[0] == '/') {
        path++;
    }
    
    // Try to find file in TLV structure
    if (!has_valid_tlv_structure(ctx)) {
        // No valid TLV structure - check if requesting data.bin fallback
        if (strcmp(path, "data.bin") == 0) {
            // Create handle for entire flash content
            dmffs_file_handle_t* handle = Dmod_Malloc(sizeof(dmffs_file_handle_t));
            if (!handle) {
                return DMFSI_ERR_GENERAL;
            }
            
            memset(handle, 0, sizeof(dmffs_file_handle_t));
            strncpy(handle->entry.name, "data.bin", sizeof(handle->entry.name) - 1);
            handle->entry.name[sizeof(handle->entry.name) - 1] = '\0';
            handle->entry.data_offset = 0;
            handle->entry.data_size = ctx->flash_size;
            handle->entry.attr = DMFSI_ATTR_READONLY;
            handle->position = 0;
            handle->ctx = ctx;
            
            *fp = handle;
            return DMFSI_OK;
        }
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // Search for file using path (supports directories)
    dmffs_file_entry_t entry;
    if (find_file_by_path(ctx, path, &entry)) {
        // Found the file
        dmffs_file_handle_t* handle = Dmod_Malloc(sizeof(dmffs_file_handle_t));
        if (!handle) {
            return DMFSI_ERR_GENERAL;
        }
        
        memcpy(&handle->entry, &entry, sizeof(dmffs_file_entry_t));
        handle->position = 0;
        handle->ctx = ctx;
        
        *fp = handle;
        return DMFSI_OK;
    }
    
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) {
        return DMFSI_ERR_INVALID;
    }
    
    Dmod_Free(fp);
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    if (!ctx || !fp || !buffer || !read) {
        return DMFSI_ERR_INVALID;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    *read = 0;
    
    // Check if we're at EOF
    if (handle->position >= handle->entry.data_size) {
        return DMFSI_OK;
    }
    
    // Calculate how much we can read
    size_t available = handle->entry.data_size - handle->position;
    size_t to_read = (size < available) ? size : available;
    
    // Read from flash
    uintptr_t flash_addr = (uintptr_t)ctx->flash_addr + handle->entry.data_offset + handle->position;
    size_t bytes_read = Dmod_ReadMemory(flash_addr, buffer, to_read);
    
    handle->position += bytes_read;
    *read = bytes_read;
    
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    if (written) {
        *written = 0;
    }
    // Read-only file system
    return DMFSI_ERR_INVALID;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _lseek, (dmfsi_context_t ctx, void* fp, long offset, int whence) )
{
    if (!ctx || !fp) {
        return -1;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    long new_position = 0;
    
    switch (whence) {
        case DMFSI_SEEK_SET:
            new_position = offset;
            break;
            
        case DMFSI_SEEK_CUR:
            new_position = handle->position + offset;
            break;
            
        case DMFSI_SEEK_END:
            new_position = handle->entry.data_size + offset;
            break;
            
        default:
            return -1;
    }
    
    // Validate position
    if (new_position < 0) {
        new_position = 0;
    }
    if (new_position > (long)handle->entry.data_size) {
        new_position = handle->entry.data_size;
    }
    
    handle->position = new_position;
    return new_position;
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
    if (!ctx || !fp) {
        return -1;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    
    // Check EOF
    if (handle->position >= handle->entry.data_size) {
        return -1;
    }
    
    uint8_t c;
    uintptr_t flash_addr = (uintptr_t)ctx->flash_addr + handle->entry.data_offset + handle->position;
    if (Dmod_ReadMemory(flash_addr, &c, 1) != 1) {
        return -1;
    }
    
    handle->position++;
    return (int)c;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    // Read-only file system - putc not supported
    if (!ctx || !fp) {
        return -1;
    }
    return -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _tell, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) {
        return -1;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    return handle->position;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) {
        return -1;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    return (handle->position >= handle->entry.data_size) ? 1 : 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, long, _size, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) {
        return -1;
    }
    
    dmffs_file_handle_t* handle = (dmffs_file_handle_t*)fp;
    return handle->entry.data_size;
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
    if (!ctx || !dp) {
        return DMFSI_ERR_INVALID;
    }
    
    dmffs_dir_handle_t* handle = Dmod_Malloc(sizeof(dmffs_dir_handle_t));
    if (!handle) {
        return DMFSI_ERR_GENERAL;
    }
    
    memset(handle, 0, sizeof(dmffs_dir_handle_t));
    handle->ctx = ctx;
    handle->entry_index = 0;
    handle->in_dir = false;
    
    // Normalize path
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        // Root directory
        handle->path[0] = '\0';
    } else {
        // Copy and normalize path
        strncpy(handle->path, path, sizeof(handle->path) - 1);
        handle->path[sizeof(handle->path) - 1] = '\0';
        
        // Remove leading slash
        if (handle->path[0] == '/') {
            memmove(handle->path, handle->path + 1, strlen(handle->path));
        }
    }
    
    // Check if we have valid TLV structure
    if (!has_valid_tlv_structure(ctx)) {
        // Invalid structure - for root only, we'll return data.bin
        if (handle->path[0] == '\0') {
            handle->entry_index = -1; // Special marker for data.bin
            *dp = handle;
            return DMFSI_OK;
        }
        Dmod_Free(handle);
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // Find the (possibly nested) directory's content range
    uint32_t start, end;
    if (!find_directory_range(ctx, handle->path, &start, &end, NULL, NULL)) {
        Dmod_Free(handle);
        return DMFSI_ERR_NOT_FOUND;
    }

    handle->current_offset = start;
    handle->dir_end_offset = end;
    handle->in_dir = (handle->path[0] != '\0');

    *dp = handle;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    if (!ctx || !dp || !entry) {
        return DMFSI_ERR_INVALID;
    }
    
    dmffs_dir_handle_t* handle = (dmffs_dir_handle_t*)dp;
    
    // Special case: no valid TLV structure, return data.bin for root
    if (handle->entry_index < 0) {
        strncpy(entry->name, "data.bin", sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->size = ctx->flash_size;
        entry->attr = DMFSI_ATTR_READONLY;
        entry->time = 0;
        handle->entry_index = 0;
        return DMFSI_OK;
    }
    
    // If we're inside a directory, only scan within that directory
    uint32_t end_offset = handle->in_dir ? handle->dir_end_offset : ctx->flash_size;
    
    while (handle->current_offset < end_offset) {
        uint32_t type, length;
        if (!read_tlv_header(ctx, handle->current_offset, &type, &length)) {
            break;
        }
        
        if (type == DMFFS_TLV_TYPE_END || type == DMFFS_TLV_TYPE_INVALID) {
            break;
        }
        
        if (type == DMFFS_TLV_TYPE_FILE) {
            dmffs_file_entry_t file_entry;
            uint32_t next_offset = parse_file_entry(ctx, handle->current_offset, &file_entry);
            
            if (next_offset == 0) {
                // Parse error - skip this TLV entry manually
                handle->current_offset += 8 + length;
            } else {
                handle->current_offset = next_offset;
                
                if (file_entry.name[0] != '\0') {
                    // Return this file
                    strncpy(entry->name, file_entry.name, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                    entry->size = file_entry.data_size;
                    entry->attr = file_entry.attr;
                    entry->time = file_entry.mtime;
                    handle->entry_index++;
                    return DMFSI_OK;
                }
            }
        } else if (type == DMFFS_TLV_TYPE_DIR) {
            // List a nested directory as an entry, regardless of nesting depth
            uint32_t nested_offset = handle->current_offset + 8;
            uint32_t dir_end = handle->current_offset + 8 + length;
            char dir_name[256] = {0};
            uint32_t dir_attr = DMFSI_ATTR_DIRECTORY | DMFSI_ATTR_READONLY;
            uint32_t dir_time = 0;
            
            // Parse DIR metadata
            while (nested_offset < dir_end) {
                uint32_t nested_type, nested_length;
                if (!read_tlv_header(ctx, nested_offset, &nested_type, &nested_length)) {
                    break;
                }
                
                uint32_t value_offset = nested_offset + 8;
                
                if (nested_type == DMFFS_TLV_TYPE_NAME && nested_length > 0) {
                    if (nested_length <= sizeof(dir_name) - 1) {
                        read_tlv_value(ctx, value_offset, dir_name, nested_length);
                        dir_name[nested_length] = '\0';
                    }
                } else if (nested_type == DMFFS_TLV_TYPE_ATTR && nested_length >= sizeof(uint32_t)) {
                    read_tlv_value(ctx, value_offset, &dir_attr, sizeof(uint32_t));
                    dir_attr |= DMFSI_ATTR_DIRECTORY; // Ensure directory flag
                } else if (nested_type == DMFFS_TLV_TYPE_DATE && nested_length >= sizeof(uint32_t)) {
                    read_tlv_value(ctx, value_offset, &dir_time, sizeof(uint32_t));
                }
                
                nested_offset += 8 + nested_length;
            }
            
            handle->current_offset += 8 + length;
            
            if (dir_name[0] != '\0') {
                // Return this directory
                strncpy(entry->name, dir_name, sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
                entry->size = 0;
                entry->attr = dir_attr;
                entry->time = dir_time;
                handle->entry_index++;
                return DMFSI_OK;
            }
        } else {
            // Skip this TLV
            handle->current_offset += 8 + length;
        }
    }
    
    return DMFSI_ERR_NOT_FOUND;
}

// Close directory

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    if (!ctx || !dp) {
        return DMFSI_ERR_INVALID;
    }
    
    Dmod_Free(dp);
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
    if (!ctx || !path) {
        return 0;
    }
    
    // Root directory always exists
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return 1;
    }
    
    // Check if valid TLV structure exists
    if (!has_valid_tlv_structure(ctx)) {
        return 0;
    }
    
    // Remove leading slash
    const char* dir_path = path;
    if (dir_path[0] == '/') {
        dir_path++;
    }

    uint32_t start, end;
    return find_directory_range(ctx, dir_path, &start, &end, NULL, NULL) ? 1 : 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    if (!ctx || !path || !stat) {
        return DMFSI_ERR_INVALID;
    }
    
    // Remove leading slash
    if (path[0] == '/') {
        path++;
    }
    
    // Check if we have valid TLV structure
    if (!has_valid_tlv_structure(ctx)) {
        // Only data.bin is available
        if (strcmp(path, "data.bin") == 0) {
            stat->size = ctx->flash_size;
            stat->attr = DMFSI_ATTR_READONLY;
            stat->ctime = 0;
            stat->mtime = 0;
            stat->atime = 0;
            return DMFSI_OK;
        }
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // Try to find file using path (supports directories)
    dmffs_file_entry_t entry;
    if (find_file_by_path(ctx, path, &entry)) {
        stat->size = entry.data_size;
        stat->attr = entry.attr;
        stat->ctime = entry.ctime;
        stat->mtime = entry.mtime;
        stat->atime = entry.mtime;
        return DMFSI_OK;
    }
    
    // Check if it's a directory (possibly nested)
    uint32_t start, end, dir_attr, dir_time;
    if (find_directory_range(ctx, path, &start, &end, &dir_attr, &dir_time)) {
        stat->size = 0;
        stat->attr = dir_attr;
        stat->ctime = dir_time;
        stat->mtime = dir_time;
        stat->atime = dir_time;
        return DMFSI_OK;
    }

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
