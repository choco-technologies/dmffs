# ======================================================================
#               dmffs_rw Module Configuration
# ======================================================================
# The writable mount of dmffs: a complete DMFSI file system (mount -t dmffs_rw -o device=<node>), loaded by dmffs
# when a mount asks for writing. It is a module of its own so that dmffs, which is in the firmware, does not carry it.

set(DMOD_MODULE_NAME        dmffs_rw)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmffs_rw_module.c
    dmffs_rw.c
    dmffs_tree.c
    dmffs_medium.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${DMOD_MODULE_NAME} dmfsi_if)

# Locks with dmosi; talks to a block device node (dmdrvi: headers only)
dmod_link_modules(${DMOD_MODULE_NAME} dmosi dmdrvi)
