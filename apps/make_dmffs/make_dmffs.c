#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmffs.h"
#include <string.h>
#include <stdio.h>

// Maximum path length
#define MAX_PATH_LEN 512

// Buffer for building paths
static char path_buffer[MAX_PATH_LEN];

// Output file handle
static void* output_file = NULL;

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
    
    void* dir = Dmod_OpenDir(dir_path);
    if (!dir) {
        DMOD_LOG_ERROR("Failed to open directory: %s\n", dir_path);
        return false;
    }
    
    // For subdirectories, we need to calculate the size first
    // We'll do a two-pass approach: first count, then write
    if (write_header) {
        // First pass: calculate the total size
        uint32_t dir_content_size = 0;
        const char* dir_name = strrchr(dir_path, '/');
        if (dir_name) {
            dir_name++; // Skip the '/'
        } else {
            dir_name = dir_path;
        }
        size_t name_len = strlen(dir_name);
        
        // NAME TLV size
        dir_content_size += 8 + name_len;
        
        // Count files and subdirectories
        const char* entry = NULL;
        while ((entry = Dmod_ReadDir(dir)) != NULL) {
            // Skip . and ..
            if (strcmp(entry, ".") == 0 || strcmp(entry, "..") == 0) {
                continue;
            }
            
            // Build full path
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir_path, entry);
            
            // Check if it's a directory by trying to open it as a directory
            void* test_dir = Dmod_OpenDir(path_buffer);
            if (test_dir) {
                // It's a directory - we need to recursively calculate its size
                // For now, we'll use a simplified approach and process without nested DIRs
                Dmod_CloseDir(test_dir);
                DMOD_LOG_WARN("Nested directories not fully supported yet, skipping: %s\n", path_buffer);
            } else {
                // It's a file - calculate FILE TLV size
                void* test_file = Dmod_FileOpen(path_buffer, "rb");
                if (test_file) {
                    size_t file_size = Dmod_FileSize(test_file);
                    size_t entry_name_len = strlen(entry);
                    uint32_t file_tlv_size = (8 + entry_name_len) + (8 + file_size);
                    dir_content_size += 8 + file_tlv_size; // FILE TLV header + content
                    Dmod_FileClose(test_file);
                }
            }
        }
        
        Dmod_CloseDir(dir);
        
        // Write DIR TLV header
        if (!write_tlv_header(DMFFS_TLV_TYPE_DIR, dir_content_size)) {
            return false;
        }
        
        // Write NAME TLV
        if (!write_tlv(DMFFS_TLV_TYPE_NAME, dir_name, name_len)) {
            return false;
        }
        
        // Reopen directory for second pass
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
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir_path, entry);
        
        // Check if it's a directory
        void* test_dir = Dmod_OpenDir(path_buffer);
        if (test_dir) {
            Dmod_CloseDir(test_dir);
            // It's a subdirectory - process recursively
            if (!write_header) {
                // Root level - write subdirectories with headers
                if (!process_directory_contents(path_buffer, base_path, true)) {
                    Dmod_CloseDir(dir);
                    return false;
                }
            } else {
                // Skip nested directories for now
                DMOD_LOG_WARN("Skipping nested directory: %s\n", path_buffer);
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
int dmod_main(int argc, const char* argv[])
{
    DMOD_LOG_INFO("make_dmffs - DMFFS Binary Generator\n");
    DMOD_LOG_INFO("Version 0.1\n\n");
    
    // Check arguments
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
