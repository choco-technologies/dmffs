# DMFFS - DMOD Flash File System

A lightweight, read-only file system module for embedded systems that stores and reads file systems from flash memory using an efficient TLV (Type-Length-Value) format.

## Overview

DMFFS (DMOD Flash File System) is a specialized file system implementation designed for embedded systems where flash memory is used to store read-only data. It is built on the **DMOD (Dynamic Modules)** framework and implements the **DMFSI (DMOD File System Interface)**, making it compatible with **DMVFS (DMOD Virtual File System)** and other DMOD-based applications.

### Key Features

- **Read-Only Operation**: Optimized for flash memory with no write or modification support
- **TLV Format**: Uses Type-Length-Value encoding for efficient, extensible storage
- **Zero-Copy Access**: Reads directly from flash memory without copying to RAM
- **Directory Support**: Organizes files in hierarchical directory structures
- **DMFSI Compatible**: Implements the standard DMOD file system interface
- **DMVFS Integration**: Can be mounted as a file system in DMVFS
- **Fallback Mode**: Provides `data.bin` for raw flash access when TLV structure is invalid
- **Small Footprint**: Minimal memory overhead, suitable for resource-constrained systems
- **Build Tool Included**: `make_dmffs` application to create file system images from directories

## Architecture

DMFFS is part of a modular embedded file system architecture built on DMOD:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│          (Your embedded application code)                    │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                 DMVFS (Virtual File System)                  │
│  • Unified file system interface                             │
│  • Multiple mount points                                     │
│  • Path resolution                                           │
│  https://github.com/choco-technologies/dmvfs                │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│          DMFSI (DMOD File System Interface)                  │
│  • Standardized POSIX-like API                               │
│  • Common interface for all file systems                     │
│  https://github.com/choco-technologies/dmfsi                │
└─────────────────────────────────────────────────────────────┘
                           │
         ┌─────────────────┼─────────────────┐
         ▼                 ▼                 ▼
┌────────────────┐  ┌─────────────┐  ┌─────────────┐
│ DMFFS (Flash)  │  │ FatFS (SD)  │  │ RamFS (RAM) │
│ Read-only TLV  │  │ Read-Write  │  │ Temporary   │
│ (This Project) │  │             │  │             │
└────────────────┘  └─────────────┘  └─────────────┘
         │                 │                 │
         ▼                 ▼                 ▼
┌────────────────┐  ┌─────────────┐  ┌─────────────┐
│ Flash Memory   │  │  SD Card    │  │  RAM        │
└────────────────┘  └─────────────┘  └─────────────┘
```

### Component Relationships

- **[DMOD](https://github.com/choco-technologies/dmod)**: The foundation providing dynamic module loading, inter-module communication, and resource management
- **[DMFSI](https://github.com/choco-technologies/dmfsi)**: Defines the standard file system interface that DMFFS implements
- **[DMVFS](https://github.com/choco-technologies/dmvfs)**: Virtual file system layer that can mount DMFFS at any path in a unified directory tree
- **DMFFS**: This project - implements DMFSI to provide read-only access to flash-stored file systems

### How It Works

1. **At Build Time**: Use `make_dmffs` to convert a directory structure into a binary TLV-formatted image
2. **At Link Time**: Link the binary image into your firmware at a specific flash address
3. **At Runtime**: 
   - Initialize DMFFS with flash address and size parameters
   - DMFFS reads the TLV structure directly from flash
   - Access files through DMFSI API or mount in DMVFS
   - No RAM copying required - files are read directly from flash

## TLV File System Format

DMFFS uses a Type-Length-Value (TLV) format for storing file system data in flash memory. This format is self-describing, extensible, and efficient for embedded systems.

### TLV Structure

Each TLV entry consists of:
- **Type** (4 bytes): Identifies the entry type
- **Length** (4 bytes): Size of the value field in bytes
- **Value** (variable): The actual data

### Supported TLV Types

| Type | Value | Description |
|------|-------|-------------|
| VERSION | 3 | File system version information (optional header) |
| FILE | 1 | File entry containing nested TLVs |
| DIR | 2 | Directory entry containing nested files/dirs |
| NAME | 4 | Name string for file/directory |
| DATA | 5 | File content data |
| DATE | 6 | Timestamp (modification time) |
| ATTR | 7 | File attributes (permissions, flags) |
| END | 0xFFFFFFFF | Marks end of TLV entries |

### Example File System Structure

```
[VERSION]
  └─ version_data

[DIR] "config"
  └─ [NAME] "config"
  └─ [FILE]
      └─ [NAME] "settings.txt"
      └─ [DATA] "key=value..."
      └─ [DATE] 1698765432

[FILE]
  └─ [NAME] "readme.txt"
  └─ [DATA] "Hello World..."
  └─ [DATE] 1698765432

[END]
```

### Benefits of TLV Format

- **Extensible**: New TLV types can be added without breaking compatibility
- **Self-Describing**: No separate metadata structures needed
- **Efficient**: Only stores what's needed; unused fields are omitted
- **Flash-Friendly**: Sequential layout matches flash memory characteristics
- **Simple Parsing**: Easy to traverse and validate

## Components

### 1. dmffs Library Module

The main DMOD library module that provides read-only file system functionality.

**Features:**
- Implements complete DMFSI interface
- Supports files and directories
- Zero-copy reads from flash memory
- Path-based file access with directory navigation
- Fallback mode providing `data.bin` for raw flash access

**API Functions (via DMFSI):**
- File operations: `dmfsi_dmffs_fopen()`, `dmfsi_dmffs_fclose()`, `dmfsi_dmffs_fread()`
- Directory operations: `dmfsi_dmffs_opendir()`, `dmfsi_dmffs_readdir()`, `dmfsi_dmffs_closedir()`
- File info: `dmfsi_dmffs_stat()`, `dmfsi_dmffs_size()`, `dmfsi_dmffs_lseek()`
- File system: `dmfsi_dmffs_init()`, `dmfsi_dmffs_deinit()`

### 2. make_dmffs Application

A DMOD application that converts directory structures into DMFFS binary images.

**Features:**
- Recursively scans directory trees
- Generates TLV-formatted binary images
- Preserves directory hierarchy
- Includes file names and content
- Creates flash-ready binary files

See [apps/make_dmffs/README.md](apps/make_dmffs/README.md) for detailed documentation.

## Usage

### Quick Start Guide

#### Step 1: Create a File System Image

First, prepare your files and use `make_dmffs` to create a binary image:

```bash
# 1. Create a directory with your files
mkdir flashfs
echo "Hello from DMFFS!" > flashfs/hello.txt
echo "Configuration data" > flashfs/config.txt
mkdir flashfs/data
echo "More content" > flashfs/data/file.txt

# 2. Build the project (if not already done)
mkdir -p build && cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .

# 3. Create the binary file system image
# Run via dmod_loader
dmod_loader _deps/dmod-build/dmf/make_dmffs.dmf --args "../flashfs ./flash-fs.bin"
```

This creates `flash-fs.bin` containing your files in TLV format.

#### Step 2: Link Binary into Firmware

Add the binary to your linker script:

```ld
/* In your linker script */
SECTIONS
{
    /* ... other sections ... */
    
    .flashfs :
    {
        _flash_fs_start = .;
        KEEP(*(.flashfs))
        _flash_fs_end = .;
    } > FLASH
}
```

Convert the binary to an object file:

```bash
# Create object file with binary data
objcopy -I binary -O elf32-littlearm -B arm \
    --rename-section .data=.flashfs \
    flash-fs.bin flash-fs.o

# Link into your firmware
arm-none-eabi-gcc ... flash-fs.o -o firmware.elf
```

#### Step 3: Use in Your Application

**Option A: Direct DMFSI API Usage**

```c
#include "dmod.h"
#include "dmfsi.h"

int main(void)
{
    // Set flash parameters via environment variables
    Dmod_SetEnv("FLASH_FS_ADDR", "0x08080000");  // Your flash address
    Dmod_SetEnv("FLASH_FS_SIZE", "0x80000");      // Size of flash region
    
    // Initialize the file system
    dmfsi_context_t ctx = dmfsi_dmffs_init(NULL);
    if (!ctx) {
        printf("Failed to initialize DMFFS\n");
        return -1;
    }
    
    // Open and read a file
    void* fp;
    if (dmfsi_dmffs_fopen(ctx, &fp, "hello.txt", DMFSI_O_RDONLY, 0) == DMFSI_OK) {
        char buffer[256];
        size_t bytes_read;
        
        dmfsi_dmffs_fread(ctx, fp, buffer, sizeof(buffer) - 1, &bytes_read);
        buffer[bytes_read] = '\0';
        
        printf("Content: %s\n", buffer);
        dmfsi_dmffs_fclose(ctx, fp);
    }
    
    // List directory contents
    void* dp;
    if (dmfsi_dmffs_opendir(ctx, &dp, "/") == DMFSI_OK) {
        dmfsi_dir_entry_t entry;
        
        while (dmfsi_dmffs_readdir(ctx, dp, &entry) == DMFSI_OK) {
            printf("Found: %s (size: %lu bytes)\n", entry.name, entry.size);
        }
        
        dmfsi_dmffs_closedir(ctx, dp);
    }
    
    // Clean up
    dmfsi_dmffs_deinit(ctx);
    return 0;
}
```

**Option B: Mount in DMVFS (Recommended)**

```c
#include "dmod.h"
#include "dmvfs.h"

int main(void)
{
    // Initialize DMVFS
    dmvfs_init(8, 32);  // 8 mount points, 32 max open files
    
    // Mount DMFFS at /flash
    // Configuration string sets flash parameters
    dmvfs_mount_fs("dmffs", "/flash", "flash_addr=0x08080000;flash_size=0x80000");
    
    // Use standard VFS operations
    void* fp;
    if (dmvfs_fopen(&fp, "/flash/hello.txt", DMFSI_O_RDONLY, 0, 0) == 0) {
        char buffer[256];
        size_t bytes_read;
        
        dmvfs_fread(fp, buffer, sizeof(buffer) - 1, &bytes_read);
        buffer[bytes_read] = '\0';
        
        printf("Content: %s\n", buffer);
        dmvfs_fclose(fp);
    }
    
    // Access nested files
    if (dmvfs_fopen(&fp, "/flash/data/file.txt", DMFSI_O_RDONLY, 0, 0) == 0) {
        // ... read file ...
        dmvfs_fclose(fp);
    }
    
    // Unmount and cleanup
    dmvfs_unmount_fs("/flash");
    dmvfs_deinit();
    return 0;
}
```

### Configuration

DMFFS can be configured in two ways:

#### 1. Environment Variables (Global Default)

```c
// Set before calling dmfsi_dmffs_init(NULL)
Dmod_SetEnv("FLASH_FS_ADDR", "0x08080000");
Dmod_SetEnv("FLASH_FS_SIZE", "0x80000");

dmfsi_context_t ctx = dmfsi_dmffs_init(NULL);
```

#### 2. Configuration String (Per-Instance)

```c
// Pass configuration directly
dmfsi_context_t ctx = dmfsi_dmffs_init("flash_addr=0x08080000;flash_size=0x80000");
```

### Advanced Features

#### Directory Support

DMFFS supports hierarchical directory structures:

```c
// Create directory structure
mkdir flashfs/config
mkdir flashfs/data/logs

// Access nested files
dmfsi_dmffs_fopen(ctx, &fp, "config/settings.txt", DMFSI_O_RDONLY, 0);
dmfsi_dmffs_fopen(ctx, &fp, "data/logs/system.log", DMFSI_O_RDONLY, 0);
```

#### Fallback Mode

If the flash doesn't contain valid TLV structure, DMFFS provides a fallback `data.bin` file with the entire flash content:

```c
// Access raw flash as data.bin
void* fp;
dmfsi_dmffs_fopen(ctx, &fp, "data.bin", DMFSI_O_RDONLY, 0);
// Can read entire flash region as a single file
```

#### File Information

Get file metadata:

```c
dmfsi_stat_t stat;
if (dmfsi_dmffs_stat(ctx, "hello.txt", &stat) == DMFSI_OK) {
    printf("Size: %lu bytes\n", stat.size);
    printf("Modified: %u\n", stat.mtime);
    printf("Attributes: 0x%x\n", stat.attr);
}
```

## API Reference

DMFFS implements the complete DMFSI interface. Below are the key functions:

### Initialization & Cleanup

| Function | Description |
|----------|-------------|
| `dmfsi_dmffs_init(config)` | Initialize file system with optional config string |
| `dmfsi_dmffs_deinit(ctx)` | Deinitialize and free resources |
| `dmfsi_dmffs_context_is_valid(ctx)` | Check if context is valid |

### File Operations

| Function | Description |
|----------|-------------|
| `dmfsi_dmffs_fopen(ctx, fp, path, mode, attr)` | Open file (read-only) |
| `dmfsi_dmffs_fclose(ctx, fp)` | Close file |
| `dmfsi_dmffs_fread(ctx, fp, buffer, size, read)` | Read from file |
| `dmfsi_dmffs_lseek(ctx, fp, offset, whence)` | Seek to position |
| `dmfsi_dmffs_tell(ctx, fp)` | Get current position |
| `dmfsi_dmffs_size(ctx, fp)` | Get file size |
| `dmfsi_dmffs_eof(ctx, fp)` | Check end-of-file |
| `dmfsi_dmffs_getc(ctx, fp)` | Read single character |

### Directory Operations

| Function | Description |
|----------|-------------|
| `dmfsi_dmffs_opendir(ctx, dp, path)` | Open directory for reading |
| `dmfsi_dmffs_readdir(ctx, dp, entry)` | Read next directory entry |
| `dmfsi_dmffs_closedir(ctx, dp)` | Close directory |
| `dmfsi_dmffs_direxists(ctx, path)` | Check if directory exists |

### File System Operations

| Function | Description |
|----------|-------------|
| `dmfsi_dmffs_stat(ctx, path, stat)` | Get file/directory information |

### Read-Only Note

Write operations (`fwrite`, `putc`, `mkdir`, `unlink`, `rename`) are not supported and will return error codes.

For complete API documentation, see [DMFSI specification](https://github.com/choco-technologies/dmfsi).

## Limitations

DMFFS is designed specifically for read-only flash storage. Be aware of these limitations:

### What DMFFS Does NOT Support

- **Write Operations**: No file creation, modification, or deletion
- **File Attributes**: Limited attribute support (mainly read-only flag)
- **Permissions**: No POSIX permission system
- **Timestamps**: Timestamps are stored but not enforced
- **Symbolic Links**: Not supported
- **Extended Attributes**: Not supported
- **Dynamic Updates**: Flash content is static after creation

### Design Constraints

- **Flash Memory Only**: Designed for direct flash access via `Dmod_ReadMemory()`
- **Fixed Layout**: File system structure cannot be modified at runtime
- **Sequential Access Optimized**: Random access is supported but sequential is more efficient
- **Memory Usage**: Minimal RAM usage (only context and file handles)

### Use Cases

**Good For:**
- ✅ Configuration files stored in flash
- ✅ Embedded resource files (HTML, images, fonts)
- ✅ Firmware update packages
- ✅ Lookup tables and databases
- ✅ Application assets
- ✅ Boot loaders and recovery systems

**Not Good For:**
- ❌ Log files (requires write access)
- ❌ User-generated content
- ❌ Temporary files
- ❌ Frequently updated data
- ❌ Large files requiring heavy random access

## Project Structure

```
dmffs/
├── include/
│   └── dmffs.h              # Public header with TLV definitions
├── src/
│   └── dmffs.c              # Main DMFFS implementation
├── apps/
│   └── make_dmffs/
│       ├── make_dmffs.c     # Binary image creator
│       └── README.md        # Tool documentation
├── CMakeLists.txt           # CMake build configuration
├── Makefile                 # Make build configuration
├── README.md                # This file
├── LICENSE                  # MIT License
├── CRASH_ANALYSIS.md        # Historical crash analysis
└── TEST_RESULTS.md          # Test documentation
```

## Testing

The DMFFS module can be tested using the test suite from the DMVFS project:

```bash
# Build DMFFS
mkdir build && cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .

# Test with DMVFS fs_tester
# (Requires dmvfs repository)
fs_tester --read-only-fs \
  --test-file /mnt/hello.txt \
  --test-dir /mnt \
  _deps/dmod-build/dmf/dmffs.dmf
```

## Integration into Your Project

### Using CMake FetchContent

```cmake
include(FetchContent)

# Fetch DMFFS
FetchContent_Declare(
    dmffs
    GIT_REPOSITORY https://github.com/choco-technologies/dmffs.git
    GIT_TAG        main
)

# Set DMOD mode
set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")

FetchContent_MakeAvailable(dmffs)

# Link to your target
target_link_libraries(your_target PRIVATE dmffs)
```

### Manual Integration

1. Clone the repository: `git clone https://github.com/choco-technologies/dmffs.git`
2. Add as subdirectory: `add_subdirectory(dmffs)`
3. Link library: `target_link_libraries(your_target dmffs)`

## Examples

### Example 1: Simple File Reading

```c
#include "dmod.h"
#include "dmfsi.h"

void read_config_file(void)
{
    // Initialize DMFFS
    Dmod_SetEnv("FLASH_FS_ADDR", "0x08080000");
    Dmod_SetEnv("FLASH_FS_SIZE", "0x10000");
    
    dmfsi_context_t ctx = dmfsi_dmffs_init(NULL);
    
    // Read configuration
    void* fp;
    if (dmfsi_dmffs_fopen(ctx, &fp, "config.txt", DMFSI_O_RDONLY, 0) == DMFSI_OK) {
        char buffer[512];
        size_t read;
        
        dmfsi_dmffs_fread(ctx, fp, buffer, sizeof(buffer) - 1, &read);
        buffer[read] = '\0';
        
        // Parse configuration
        printf("Config: %s\n", buffer);
        
        dmfsi_dmffs_fclose(ctx, fp);
    }
    
    dmfsi_dmffs_deinit(ctx);
}
```

### Example 2: Directory Traversal

```c
void list_all_files(dmfsi_context_t ctx, const char* path)
{
    void* dp;
    
    if (dmfsi_dmffs_opendir(ctx, &dp, path) == DMFSI_OK) {
        dmfsi_dir_entry_t entry;
        
        while (dmfsi_dmffs_readdir(ctx, dp, &entry) == DMFSI_OK) {
            if (entry.attr & DMFSI_ATTR_DIRECTORY) {
                printf("DIR:  %s\n", entry.name);
            } else {
                printf("FILE: %s (%lu bytes)\n", entry.name, entry.size);
            }
        }
        
        dmfsi_dmffs_closedir(ctx, dp);
    }
}
```

### Example 3: Using with DMVFS

```c
#include "dmvfs.h"

int main(void)
{
    // Initialize DMVFS with DMFFS mounted
    dmvfs_init(8, 32);
    dmvfs_mount_fs("dmffs", "/flash", "flash_addr=0x08080000;flash_size=0x10000");
    
    // Use standard VFS operations
    void* config_file;
    dmvfs_fopen(&config_file, "/flash/config.txt", DMFSI_O_RDONLY, 0, 0);
    
    // Read HTML for web server
    void* html_file;
    dmvfs_fopen(&html_file, "/flash/web/index.html", DMFSI_O_RDONLY, 0, 0);
    
    // Clean up
    dmvfs_fclose(config_file);
    dmvfs_fclose(html_file);
    dmvfs_unmount_fs("/flash");
    dmvfs_deinit();
    
    return 0;
}
```

## Troubleshooting

### Problem: Files not found

**Solution:** Check that:
- Flash address is correct: `Dmod_SetEnv("FLASH_FS_ADDR", "0x...")`
- Flash size is sufficient: `Dmod_SetEnv("FLASH_FS_SIZE", "0x...")`
- Binary image was linked correctly in firmware
- TLV structure is valid (use fallback `data.bin` to verify flash content)

### Problem: Crash during initialization

**Solution:**
- Verify `Dmod_ReadMemory()` is implemented correctly
- Check flash address is accessible
- Ensure flash region is properly initialized

### Problem: Cannot access subdirectory files

**Solution:**
- Use forward slashes: `"dir/file.txt"` not `"dir\file.txt"`
- Do not use leading slash with direct DMFSI API
- Use full path with DMVFS: `"/flash/dir/file.txt"`

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

## Related Projects

- **[DMOD](https://github.com/choco-technologies/dmod)** - Dynamic module system
- **[DMFSI](https://github.com/choco-technologies/dmfsi)** - File system interface specification
- **[DMVFS](https://github.com/choco-technologies/dmvfs)** - Virtual file system implementation

## License

MIT License - See [LICENSE](LICENSE) file for details.

## Author

Patryk Kubiak

## Support

For questions, issues, or feature requests, please open an issue on GitHub:
https://github.com/choco-technologies/dmffs/issues
