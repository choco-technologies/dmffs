# #############################################################################
# 
# 	This is an example of a simple library module.
#
# #############################################################################
DMOD_DIR=/home/pkubiak/Projects/choco-technologies/public/dmod

# -----------------------------------------------------------------------------
#  Paths initialization
# -----------------------------------------------------------------------------
include $(DMOD_DIR)/paths.mk

# -----------------------------------------------------------------------------
#   Module configuration
# -----------------------------------------------------------------------------

# The name of the module
DMOD_MODULE_NAME=dmffs

# The version of the module
DMOD_MODULE_VERSION=0.1

# The name of the author
DMOD_AUTHOR_NAME=Patryk Kubiak

# The list of C sources
DMOD_CSOURCES=dmffs.c 

# The list of C++ sources
DMOD_CXXSOURCES=

# The list of include directories
DMOD_INC_DIRS=

# The list of libraries to link
DMOD_LIBS=

# The list of definitions
DMOD_DEFINITIONS=

# -----------------------------------------------------------------------------
#   List of MAL interfaces implemented by the module
# -----------------------------------------------------------------------------
DMOD_MAL_IMPLS=

# -----------------------------------------------------------------------------
#   List of DIF interfaces implemented by the module
# -----------------------------------------------------------------------------
DMOD_DIF_IMPLS=dmffs

# -----------------------------------------------------------------------------
#   Include the dmod app makefile
# -----------------------------------------------------------------------------
include $(DMOD_DMF_LIB_FILE_PATH)
