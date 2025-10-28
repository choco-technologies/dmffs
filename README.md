# dmffs

DMOD Flash File System - A read-only file system for embedded systems.

## Description

This project provides a DMOD library module for reading file systems from flash memory, and a companion application (`make_dmffs`) for creating file system images.

## Author

Patryk Kubiak

## License

MIT

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .
```

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE
```

## Components

### dmffs Library Module

A read-only file system implementation that reads TLV-formatted data from flash memory. Implements the DMFSI (DMOD File System Interface) for integration with DMOD applications.

### make_dmffs Application

A DMF application that converts a directory structure into a binary file system image. See [apps/make_dmffs/README.md](apps/make_dmffs/README.md) for detailed usage.

**Example:**
```bash
# Run via dmod_loader
dmod_loader make_dmffs.dmf ./flashfs ./out/flash-fs.bin
```

## Usage

### Creating a File System Image

1. Prepare your directory structure:
```bash
mkdir flashfs
echo "Hello World" > flashfs/hello.txt
echo "Config data" > flashfs/config.txt
```

2. Convert to binary image:
```bash
make_dmffs ./flashfs ./out/flash-fs.bin
```

3. Link the binary into your firmware or load it to flash memory

### Mounting the File System

```c
// Set flash parameters
Dmod_SetEnv("FLASH_FS_ADDR", "0x08080000");
Dmod_SetEnv("FLASH_FS_SIZE", "0x80000");

// Initialize file system
dmfsi_context_t ctx = dmfsi_dmffs_init(NULL);

// Open and read files
void* fp;
dmfsi_dmffs_fopen(ctx, &fp, "hello.txt", DMFSI_O_RDONLY, 0);
// ... read file ...
dmfsi_dmffs_fclose(ctx, fp);

// Clean up
dmfsi_dmffs_deinit(ctx);
```
