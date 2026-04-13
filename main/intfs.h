#pragma once

#include "esp_err.h"

// Mount the wear-levelled FAT partition (label "locfd") at /int.
// This is the shared internal filesystem where the launcher stores
// per-event icons, themes, etc. Must be called early, before any
// /int/* path is opened.
esp_err_t intfs_init(void);
