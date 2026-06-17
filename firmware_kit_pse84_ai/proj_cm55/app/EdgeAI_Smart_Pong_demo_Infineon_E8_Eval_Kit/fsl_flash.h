#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t _unused;
} flash_config_t;

enum
{
    kStatus_FLASH_Success = 0,
    kStatus_FLASH_Fail = 1,
};

enum
{
    kFLASH_PropertyPflashBlockBaseAddr = 0,
    kFLASH_PropertyPflashTotalSize = 1,
    kFLASH_PropertyPflashSectorSize = 2,
};

#define kFLASH_ApiEraseKey (0xDEADBEEFu)

static inline int FLASH_Init(flash_config_t *cfg)
{
    (void)cfg;
    return kStatus_FLASH_Fail;
}

static inline int FLASH_GetProperty(flash_config_t *cfg, uint32_t property, uint32_t *value)
{
    (void)cfg;
    (void)property;
    if (value) *value = 0u;
    return kStatus_FLASH_Fail;
}

static inline int FLASH_Erase(flash_config_t *cfg, uint32_t addr, uint32_t len, uint32_t key)
{
    (void)cfg;
    (void)addr;
    (void)len;
    (void)key;
    return kStatus_FLASH_Fail;
}

static inline int FLASH_Program(flash_config_t *cfg, uint32_t addr, uint8_t *data, uint32_t len)
{
    (void)cfg;
    (void)addr;
    (void)data;
    (void)len;
    return kStatus_FLASH_Fail;
}
