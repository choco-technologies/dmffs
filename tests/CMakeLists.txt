set(DMOD_MODULE_NAME        test_dmffs)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmffs_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(${DMOD_MODULE_NAME} dmffs_if dmfsi_if)
dmod_link_modules(${DMOD_MODULE_NAME} dmosi)

# Register the test module with ctest: dmod_loader finds dmffs.dmf and its other dependencies in DMOD_DMF_DIR. The
# test's exit code equals its number of failed steps (see dmod_add_test).
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf
    )
else()
    message(WARNING "dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
endif()

# The tree of a writable mount (src/dmffs_tree.c) on its own, with the host compiler and sanitizers: the module
# loader is not needed to check a data structure and an image format.
find_program(HOST_CC NAMES cc gcc)
if(HOST_CC)
    set(TREE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tree_test.c ${CMAKE_CURRENT_SOURCE_DIR}/../src/dmffs_tree.c)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tree_test
        COMMAND ${HOST_CC} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
            -I${CMAKE_CURRENT_SOURCE_DIR}/stubs -I${CMAKE_CURRENT_SOURCE_DIR}/../src -I${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${TREE_SOURCES} -o ${CMAKE_CURRENT_BINARY_DIR}/tree_test
        DEPENDS ${TREE_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../src/dmffs_tree.h ${CMAKE_CURRENT_SOURCE_DIR}/stubs/dmod.h)
    add_custom_target(tree_test_build ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/tree_test)
    add_test(NAME tree_test COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tree_test)
endif()
