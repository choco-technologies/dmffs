# make_dmffs

A DMF application that converts a directory structure into a single binary file with DMFFS file system.

## Description

`make_dmffs` is a DMOD application that scans a directory tree and converts it into a binary file using the TLV (Type-Length-Value) format defined by the DMFFS file system. This binary can then be placed in the microcontroller's memory by the linker and read by the `dmffs` library.

## Usage

The application takes two arguments:
- Input directory path
- Output binary file path

### Example

```bash
make_dmffs ./flashfs ./out/flash-fs.bin
```

This will:
1. Scan all files in `./flashfs` directory
2. Create a binary file at `./out/flash-fs.bin` with the TLV structure
3. The binary can be linked into firmware and mounted using dmffs

## Running the Application

Since this is a DMF application, it needs to be run using:
- **dmod_loader** on a development system
- **Directly on embedded target** with DMOD system

### Example with dmod_loader

```bash
dmod_loader make_dmffs.dmf ./flashfs ./out/flash-fs.bin
```

## TLV Structure

The generated binary file uses the following TLV structure:

1. **VERSION** (optional) - File system version
2. **FILE** entries:
   - **NAME** - File name
   - **DATA** - File content
3. **DIR** entries (for subdirectories):
   - **NAME** - Directory name
   - **FILE** entries within directory
4. **END** - End marker

## Limitations

- Current implementation supports root-level files and one level of subdirectories
- Nested subdirectories are logged but not fully processed
- Read-only file system (no attributes like permissions are preserved)

## Building

The application is built as part of the dmffs project:

```bash
mkdir -p build
cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .
```

The output will be `build/_deps/dmod-build/dmf/make_dmffs.dmf`

## Integration with dmffs

The generated binary file can be:
1. Linked into firmware at a specific flash address
2. Mounted using dmffs library with configuration:
   - `FLASH_FS_ADDR` - Address where the binary is located
   - `FLASH_FS_SIZE` - Size of the flash region

Example:
```c
// Set environment variables
Dmod_SetEnv("FLASH_FS_ADDR", "0x08080000");
Dmod_SetEnv("FLASH_FS_SIZE", "0x80000");

// Mount the file system
dmfsi_context_t ctx = dmfsi_dmffs_init(NULL);

// Open files
void* fp;
dmfsi_dmffs_fopen(ctx, &fp, "test.txt", DMFSI_O_RDONLY, 0);
```

## Author

Patryk Kubiak

## Version

0.1
