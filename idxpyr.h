/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "utilMacros.h"

typedef uint16_t idxpyr_block_t;
#define IDXPYR_EMPTY   ((size_t) -1)

// - 1 at the end removes edge cases (e.g. ((size_t) 1 << BIT_COUNT(size_t)))
#define IDXPYR_MAX_INDEX_COUNT_LOG2   (UM_BIT_COUNT(size_t) - 1)

#define IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2   (IDXPYR_MAX_INDEX_COUNT_LOG2 - UM_BIT_COUNT_LOG2(idxpyr_block_t))

#define IDXPYR_MAX_HEIGHT   (IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 / UM_BIT_COUNT_LOG2(idxpyr_block_t) \
        + !!(IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 % UM_BIT_COUNT_LOG2(idxpyr_block_t)))

typedef struct {
    unsigned int indexCountLog2;
    unsigned int height;
    idxpyr_block_t *rows[IDXPYR_MAX_HEIGHT];
    size_t storeSize;
    bool stateInit;
} idxpyr_t;

idxpyr_t idxpyr_make(unsigned int indexCountLog2, bool stateInit);
// if no index is found IDXPYR_EMPTY is returned
size_t idxpyr_getFirst(idxpyr_t *pyr);
size_t idxpyr_popFirst(idxpyr_t *pyr);

// index has to be within currently allocated size -- guarded by assert()
bool idxpyr_get(idxpyr_t *pyr, size_t index);
void idxpyr_set(idxpyr_t *pur, size_t index, bool state);
void idxpyr_setAll(idxpyr_t *pyr, bool state);
void idxpyr_increaseSize(idxpyr_t *pyr);

void idxpyr_destroy(idxpyr_t *pyr);
