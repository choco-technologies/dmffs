#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmffs.h"
#include <string.h>

// Maximum path length
#define MAX_PATH_LEN 512

// Output file handle
static void* output_file = NULL;

/**
 * @brief Build a path by concatenating directory and entry
 * Simple replacement for snprintf in DMOD_MODULE mode
 * 
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param dir Directory path
 * @param entry Entry name
 */
static void build_path(char* buffer, size_t buffer_size, const char* dir, const char* entry)
{
    size_t dir_len = strlen(dir);
    size_t entry_len = strlen(entry);
    
    // Check if we have enough space (dir + "/" + entry + null)
    if (dir_len + 1 + entry_len + 1 > buffer_size) {
        DMOD_LOG_ERROR("Path too long: %s/%s\n", dir, entry);
        buffer[0] = '\0';
        return;
    }
    
    // Copy directory
    strcpy(buffer, dir);
    
    // Add separator if needed
    if (dir_len > 0 && buffer[dir_len - 1] != '/') {
        buffer[dir_len] = '/';
        buffer[dir_len + 1] = '\0';
    }
    
    // Append entry
    strcat(buffer, entry);
}

/**
 * @brief Write a TLV header to the output file
 * 
 * @param type TLV type
 * @param length TLV length
 * @return true on success, false on error
 */
static bool write_tlv_header(uint32_t type, uint32_t length)
{
    if (!output_file) return false;
    
    size_t written = Dmod_FileWrite(&type, sizeof(uint32_t), 1, output_file);
    if (written != 1) {
        DMOD_LOG_ERROR("Failed to write TLV type\n");
        return false;
    }
    
    written = Dmod_FileWrite(&length, sizeof(uint32_t), 1, output_file);
    if (written != 1) {
        DMOD_LOG_ERROR("Failed to write TLV length\n");
        return false;
    }
    
    return true;
}

/**
 * @brief Write a TLV entry with data to the output file
 * 
 * @param type TLV type
 * @param data Data to write
 * @param length Length of data
 * @return true on success, false on error
 */
static bool write_tlv(uint32_t type, const void* data, uint32_t length)
{
    if (!write_tlv_header(type, length)) {
        return false;
    }
    
    if (length > 0 && data) {
        size_t written = Dmod_FileWrite(data, 1, length, output_file);
        if (written != length) {
            DMOD_LOG_ERROR("Failed to write TLV data (%u bytes)\n", length);
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Process a single file and write it to the output in TLV format
 * 
 * @param filepath Full path to the file
 * @param filename Just the filename (without directory path)
 * @return true on success, false on error
 */
static bool process_file(const char* filepath, const char* filename)
{
    DMOD_LOG_INFO("Processing file: %s (name: %s)\n", filepath, filename);
    
    // Open the input file
    void* input_file = Dmod_FileOpen(filepath, "rb");
    if (!input_file) {
        DMOD_LOG_ERROR("Failed to open file: %s\n", filepath);
        return false;
    }
    
    // Get file size
    size_t file_size = Dmod_FileSize(input_file);
    DMOD_LOG_INFO("File size: %u bytes\n", (unsigned int)file_size);
    
    // Calculate the total size of FILE TLV:
    // NAME TLV (8 + name_len) + DATA TLV (8 + file_size)
    size_t name_len = strlen(filename);
    uint32_t file_tlv_size = (8 + name_len) + (8 + file_size);
    
    // Write FILE TLV header
    if (!write_tlv_header(DMFFS_TLV_TYPE_FILE, file_tlv_size)) {
        Dmod_FileClose(input_file);
        return false;
    }
    
    // Write NAME TLV
    if (!write_tlv(DMFFS_TLV_TYPE_NAME, filename, name_len)) {
        Dmod_FileClose(input_file);
        return false;
    }
    
    // Write DATA TLV header
    if (!write_tlv_header(DMFFS_TLV_TYPE_DATA, file_size)) {
        Dmod_FileClose(input_file);
        return false;
    }
    
    // Copy file data in chunks
    uint8_t buffer[1024];
    size_t total_read = 0;
    while (total_read < file_size) {
        size_t to_read = (file_size - total_read) > sizeof(buffer) ? sizeof(buffer) : (file_size - total_read);
        size_t read = Dmod_FileRead(buffer, 1, to_read, input_file);
        
        if (read == 0) {
            DMOD_LOG_ERROR("Failed to read from file: %s\n", filepath);
            Dmod_FileClose(input_file);
            return false;
        }
        
        size_t written = Dmod_FileWrite(buffer, 1, read, output_file);
        if (written != read) {
            DMOD_LOG_ERROR("Failed to write file data\n");
            Dmod_FileClose(input_file);
            return false;
        }
        
        total_read += read;
    }
    
    Dmod_FileClose(input_file);
    DMOD_LOG_INFO("File processed successfully: %s\n", filename);
    
    return true;
}

/**
 * @brief Forward declaration for recursive directory processing
 */
static bool process_directory_contents(const char* dir_path, const char* base_path, bool write_header);

/**
 * @brief Calculate the size of a directory's TLV content (recursive)
 * 
 * @param dir_path Full path to the directory
 * @return Total size in bytes, or 0 on error
 */
static uint32_t calculate_directory_size(const char* dir_path)
{
    uint32_t total_size = 0;
    char path_buffer[MAX_PATH_LEN];
    
    void* dir = Dmod_OpenDir(dir_path);
    if (!dir) {
        return 0;
    }
    
    // Get directory name
    const char* dir_name = strrchr(dir_path, '/');
    if (dir_name) {
        dir_name++; // Skip the '/'
    } else {
        dir_name = dir_path;
    }
    size_t name_len = strlen(dir_name);
    
    // NAME TLV size
    total_size += 8 + name_len;
    
    // Calculate size of all entries
    const char* entry = NULL;
    while ((entry = Dmod_ReadDir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry, ".") == 0 || strcmp(entry, "..") == 0) {
            continue;
        }
        
        // Build full path
        build_path(path_buffer, sizeof(path_buffer), dir_path, entry);
        if (path_buffer[0] == '\0') {
            continue; // Skip if path is too long
        }
        
        // Check if it's a directory
        void* test_dir = Dmod_OpenDir(path_buffer);
        if (test_dir) {
            Dmod_CloseDir(test_dir);
            // It's a directory - calculate its size recursively
            uint32_t subdir_size = calculate_directory_size(path_buffer);
            if (subdir_size > 0) {
                total_size += 8 + subdir_size; // DIR TLV header + content
            }
        } else {
            // It's a file - calculate FILE TLV size
            void* test_file = Dmod_FileOpen(path_buffer, "rb");
            if (test_file) {
                size_t file_size = Dmod_FileSize(test_file);
                size_t entry_name_len = strlen(entry);
                uint32_t file_tlv_size = (8 + entry_name_len) + (8 + file_size);
                total_size += 8 + file_tlv_size; // FILE TLV header + content
                Dmod_FileClose(test_file);
            }
        }
    }
    
    Dmod_CloseDir(dir);
    return total_size;
}

/**
 * @brief Process a directory recursively and write it to the output in TLV format
 * 
 * @param dir_path Full path to the directory
 * @param base_path Base path to remove from full paths
 * @param write_header Whether to write DIR TLV header (false for root)
 * @return true on success, false on error
 */
static bool process_directory_contents(const char* dir_path, const char* base_path, bool write_header)
{
    DMOD_LOG_INFO("Processing directory: %s (write_header: %d)\n", dir_path, write_header);
    
    char path_buffer[MAX_PATH_LEN];
    
    void* dir = Dmod_OpenDir(dir_path);
    if (!dir) {
        DMOD_LOG_ERROR("Failed to open directory: %s\n", dir_path);
        return false;
    }
    
    // For subdirectories, calculate size and write header
    if (write_header) {
        const char* dir_name = strrchr(dir_path, '/');
        if (dir_name) {
            dir_name++; // Skip the '/'
        } else {
            dir_name = dir_path;
        }
        size_t name_len = strlen(dir_name);
        
        // Calculate total directory content size
        Dmod_CloseDir(dir);
        uint32_t dir_content_size = calculate_directory_size(dir_path);
        
        if (dir_content_size == 0) {
            DMOD_LOG_ERROR("Failed to calculate directory size: %s\n", dir_path);
            return false;
        }
        
        // Write DIR TLV header
        if (!write_tlv_header(DMFFS_TLV_TYPE_DIR, dir_content_size)) {
            return false;
        }
        
        // Write NAME TLV
        if (!write_tlv(DMFFS_TLV_TYPE_NAME, dir_name, name_len)) {
            return false;
        }
        
        // Reopen directory for processing
        dir = Dmod_OpenDir(dir_path);
        if (!dir) {
            DMOD_LOG_ERROR("Failed to reopen directory: %s\n", dir_path);
            return false;
        }
    }
    
    // Process directory contents
    const char* entry = NULL;
    while ((entry = Dmod_ReadDir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry, ".") == 0 || strcmp(entry, "..") == 0) {
            continue;
        }
        
        // Build full path
        build_path(path_buffer, sizeof(path_buffer), dir_path, entry);
        if (path_buffer[0] == '\0') {
            continue; // Skip if path is too long
        }
        
        // Check if it's a directory
        void* test_dir = Dmod_OpenDir(path_buffer);
        if (test_dir) {
            Dmod_CloseDir(test_dir);
            // It's a subdirectory - process recursively with header
            if (!process_directory_contents(path_buffer, base_path, true)) {
                Dmod_CloseDir(dir);
                return false;
            }
        } else {
            // It's a file
            if (!process_file(path_buffer, entry)) {
                Dmod_CloseDir(dir);
                return false;
            }
        }
    }
    
    Dmod_CloseDir(dir);
    DMOD_LOG_INFO("Directory processed successfully: %s\n", dir_path);
    
    return true;
}

/**
 * @brief Main application entry point
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success, non-zero on error
 */
int main(int argc, const char* argv[])
{
    DMOD_LOG_INFO("make_dmffs - DMFFS Binary Generator\n");
    DMOD_LOG_INFO("Version 0.1\n\n");
    
    // Check arguments
    // When run via dmod_loader with --args, argc doesn't include program name
    // So we expect argc=2 (input_dir, output_file)
    if (argc != 3) {
        DMOD_LOG_ERROR("Usage: make_dmffs <input_directory> <output_file>\n");
        DMOD_LOG_ERROR("Example: make_dmffs ./flashfs ./out/flash-fs.bin\n");
        return 1;
    }
    
    const char* input_dir = argv[1];
    const char* output_path = argv[2];
    
    DMOD_LOG_INFO("Input directory: %s\n", input_dir);
    DMOD_LOG_INFO("Output file: %s\n", output_path);
    
    // Check if input directory exists
    void* test_dir = Dmod_OpenDir(input_dir);
    if (!test_dir) {
        DMOD_LOG_ERROR("Input directory does not exist or cannot be opened: %s\n", input_dir);
        return 1;
    }
    Dmod_CloseDir(test_dir);
    
    // Open output file
    output_file = Dmod_FileOpen(output_path, "wb");
    if (!output_file) {
        DMOD_LOG_ERROR("Failed to open output file: %s\n", output_path);
        return 1;
    }
    
    // Write VERSION TLV (optional)
    const char* version = "1.0";
    if (!write_tlv(DMFFS_TLV_TYPE_VERSION, version, strlen(version))) {
        DMOD_LOG_ERROR("Failed to write VERSION TLV\n");
        Dmod_FileClose(output_file);
        return 1;
    }
    
    // Process the directory (root level - don't write DIR header)
    bool success = process_directory_contents(input_dir, input_dir, false);
    
    if (success) {
        // Write END TLV
        if (!write_tlv_header(DMFFS_TLV_TYPE_END, 0)) {
            DMOD_LOG_ERROR("Failed to write END TLV\n");
            success = false;
        }
    }
    
    // Close output file
    Dmod_FileClose(output_file);
    output_file = NULL;
    
    if (success) {
        DMOD_LOG_INFO("\nSuccess! Created DMFFS binary: %s\n", output_path);
        return 0;
    } else {
        DMOD_LOG_ERROR("\nFailed to create DMFFS binary\n");
        return 1;
    }
}

/**
 * @brief Module initialization (optional)
 */
void dmod_preinit(void)
{
    // Nothing to do
}

/**
 * @brief Module initialization
 */
int dmod_init(const Dmod_Config_t *Config)
{
    // Nothing to do
    return 0;
}

/**
 * @brief Module deinitialization
 */
int dmod_deinit(void)
{
    // Nothing to do
    return 0;
}
