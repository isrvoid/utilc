/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "kvec.h"

#include "compositeTypes.h"
#include "idxpyr.h"
#include "circbuf.h"

// FIXME implementation is broken and put on hold -- use mempool.h for faster and slimmer version

//  functions return -1 on error; errno can be checked for specific value
#define MP_ERROR_ELEMENT_SIZE 300
#define MP_ERROR_ELEMENTS_PER_CLUSTER 301
#define MP_ERROR_INVALID_ID 302

/* For small elements (e.g. < 8 bytes) elementsPerCluster should be bigger
   (e.g. >= 32) to reduce the overhead */

typedef struct {
    size_t elementSize;
    size_t elementsPerCluster; // suggested value - implementation might choose to modify it
    size_t freeClusterCountMax; // memory is freed more agressively with smaller values
} mp_poolSettings_t;

/* Defines ID size for every pool in the application. If there is no need for more than
   65K IDs uint16_t would reduce memory overhead. */
typedef uint16_t mp_id_t;

// opaque mempool type - shouldn't be changed directly
typedef struct {
    size_t elementSize;
    size_t elementsPerCluster;
    size_t clusterSize;
    size_t clusterIndexOffset;
    size_t elementIndexMask;
    // there is no need to address more locations than maximum number of keys - hence mp_id_t
    kvec_t(mp_id_t) locationLut;
    idxpyr_t freeIds;

    kvec_t(void *) clusterLut;
    circbuf_t allocatedClusterIndices;
    circbuf_t unallocatedClusterIndices;
    size_t front;
    size_t back;

    size_t frontElementCount;
    size_t frontElementIndex;
    size_t backElementIndex;

    kvec_t(void *) freeClusters;
} mp_pool_t;

int mp_init(mp_pool_t *poolOut, mp_poolSettings_t settings);
// id 0 is invalid and won't be returned
void mp_alloc(mp_pool_t *pool, mp_id_t *idOut);
int mp_free(mp_pool_t *pool, mp_id_t id);
bool mp_idExists(mp_pool_t *pool, mp_id_t id); // exception to no 0 id rule - simply returns false
// id 0 shouldn't be passed -- it's guarded agains by asserts
int mp_get(mp_pool_t *pool, mp_id_t id, void *out);
// result pointer shouldn't be saved - it could change after any mp_free call
int mp_getPtr(mp_pool_t *pool, mp_id_t id, void **data);
int mp_set(mp_pool_t *pool, mp_id_t id, const void *in);
void mp_destroy(mp_pool_t *pool);
