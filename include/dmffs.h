#ifndef DMFFS_H
#define DMFFS_H

#include "dmod.h"

#ifndef DMFFS_ENV_FLASH_ADDR
#define DMFFS_ENV_FLASH_ADDR "FLASH_FS_ADDR"
#endif

#ifndef DMFFS_ENV_FLASH_SIZE
#define DMFFS_ENV_FLASH_SIZE "FLASH_FS_SIZE"
#endif

/**
 * @brief TLV structure for flash file system metadata
 * 
 * @note This structure is used to store metadata in a Type-Length-Value format.
 */
typedef struct {
    uint32_t type;          //!< Type of the TLV entry
    uint32_t length;        //!< Length of the value field
    uint8_t  value[];       //!< Value field (flexible array member)
} tlv_t;

/**
 * @brief TLV types for flash file system
 */
typedef enum {
    DMFFS_TLV_TYPE_INVALID = 0,             //!< Invalid TLV type
    DMFFS_TLV_TYPE_FILE    = 1,             //!< File entry
    DMFFS_TLV_TYPE_DIR     = 2,             //!< Directory entry
    DMFFS_TLV_TYPE_VERSION = 3,             //!< Version information
    DMFFS_TLV_TYPE_NAME    = 4,             //!< Name entry
    DMFFS_TLV_TYPE_DATA    = 5,             //!< Data entry
    DMFFS_TLV_TYPE_END     = 0xFFFFFFFF     //!< End of TLV entries
} dmffs_tlv_type_t;

#endif // DMFFS_H