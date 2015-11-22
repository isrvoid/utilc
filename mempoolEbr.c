/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#include "mempoolEbr.h"

#include <string.h>
#include <assert.h>
#include <errno.h>

#include "kvec.h"
#include "utilMacros.h"
#include "unittestMacros.h"

// private declarations
// -----------------------------------------------------------------------------
static int testInitSettingsArg(mp_poolSettings_t s);
static void initClusterFifos(mp_pool_t *pool);

static unsigned int log2Envelope(size_t val);
static inline bool _idExists(const mp_pool_t *pool, mp_id_t id);
static int testIdExists(const mp_pool_t *pool, mp_id_t id);
static mp_id_t takeNextLocation(mp_pool_t *pool);
static inline uint8_t *getElementStore(const mp_pool_t *pool, mp_id_t location);

static void addFrontCluster(mp_pool_t *pool);
static void addClusterIndices(mp_pool_t *pool);
static void removeBackCluster(mp_pool_t *pool);

static void destroyClusterFifos(mp_pool_t *pool);

// interface functions
// -----------------------------------------------------------------------------
int mp_init(mp_pool_t *poolOut, mp_poolSettings_t settings) {
    int error = testInitSettingsArg(settings);
    if (error)
        return error;

    memset(poolOut, 0, sizeof(mp_pool_t));

    poolOut->elementSize = settings.elementSize;
    unsigned int elementIndexBitCount = log2Envelope(settings.elementsPerCluster);
    poolOut->clusterIndexOffset = elementIndexBitCount;
    poolOut->elementsPerCluster = (1 << elementIndexBitCount);
    poolOut->elementIndexMask = poolOut->elementsPerCluster - 1;

    size_t elementStoreSize = settings.elementSize + sizeof(mp_id_t);
    poolOut->clusterSize = elementStoreSize * poolOut->elementsPerCluster;

    kv_resize(mp_id_t, poolOut->locationLut, UM_BIT_COUNT(idxpyr_block_t));
    // FIXME kv_push(mp_id_t, poolOut->locationLut, (mp_id_t) -1); // advance past illegal id 0
    // init index pyramid
    poolOut->freeIds = idxpyr_make(UM_BIT_COUNT_LOG2(idxpyr_block_t));
    idxpyr_setAll(&poolOut->freeIds, true);
    idxpyr_set(&poolOut->freeIds, 0, false); // scratch illegal id 0

    initClusterFifos(poolOut);

    kv_resize(void *, poolOut->freeClusters, settings.freeClusterCountMax);
    addFrontCluster(poolOut);

    return 0;
}

void mp_alloc(mp_pool_t *pool, mp_id_t *idOut) {
    mp_id_t location = takeNextLocation(pool);
    size_t id = idxpyr_popFirst(&pool->freeIds);
    if (id == IDXPYR_EMPTY) {
        size_t newId = kv_size(pool->locationLut);
        id = (mp_id_t) newId;
        // FIXME ids are denser than clusters - move this assert to cluster creation
        assert(id == newId); // new id fits into mp_id_t
        kv_push(mp_id_t, pool->locationLut, location);
        idxpyr_increaseSize(&pool->freeIds, true);
        // FIXME ?
        idxpyr_set(&pool->freeIds, kv_size(pool->locationLut) - 1, false);
    } else {
        kv_A(pool->locationLut, id) = location;
    }

    uint8_t *elementIdStore = getElementStore(pool, location) + pool->elementSize;
    memcpy(elementIdStore, &id, sizeof(mp_id_t));

    *idOut = (mp_id_t) id;
}

int mp_free(mp_pool_t *pool, mp_id_t id) {
    int error = testIdExists(pool, id);
    if (error)
        return error;

    // FIXME free (overwrite with tail, update tails location)
    idxpyr_set(&pool->freeIds, id, false);
    // FIXME only use freeIds to check whether the location is free
    kv_A(pool->locationLut, id) = (mp_id_t) -1;
    return 0;
}

bool mp_idExists(mp_pool_t *pool, mp_id_t id) {
    return _idExists(pool, id);
}

int mp_get(mp_pool_t *pool, mp_id_t id, void *out) {
    int error = testIdExists(pool, id);
    if (error)
        return error;

    uint8_t *elem = getElementStore(pool, kv_A(pool->locationLut, id));
    memcpy(out, elem, pool->elementSize);
    return 0;
}

int mp_getPtr(mp_pool_t *pool, mp_id_t id, void **data) {
    int error = testIdExists(pool, id);
    if (error)
        return error;

    *data = getElementStore(pool, kv_A(pool->locationLut, id));
    return 0;
}

int mp_set(mp_pool_t *pool, mp_id_t id, const void *in) {
    int error = testIdExists(pool, id);
    if (error)
        return error;

    uint8_t *elem = getElementStore(pool, kv_A(pool->locationLut, id));
    memcpy(elem, in, pool->elementSize);
    return 0;
}

void mp_destroy(mp_pool_t *pool) {
    kv_destroy(pool->locationLut);
    idxpyr_destroy(&pool->freeIds);

    destroyClusterFifos(pool);

    for (size_t i = 0; i < kv_size(pool->freeClusters); ++i)
        free(kv_A(pool->freeClusters, i));
    kv_destroy(pool->freeClusters);
}

// private functions
// -----------------------------------------------------------------------------
static int testInitSettingsArg(mp_poolSettings_t settings) {
    // FIXME test: enveloped elementsPerCluster <= 1 << sizeof(mp_id_t) / 2
    // also free cluster wouldn't make sense at this constellation
    // (max (elements in mp_id_t / elementsPerCluster) - catch
    if (!settings.elementSize) {
        errno = MP_ERROR_ELEMENT_SIZE;
        return -1;
    }
    if (!settings.elementsPerCluster) {
        errno = MP_ERROR_ELEMENTS_PER_CLUSTER;
        return -1;
    }
    return 0;
}

static void initClusterFifos(mp_pool_t *pool) {
    const unsigned int initialClusterIndexCountLog2 = 2;
    const unsigned int initialClusterIndexCount = 1 << initialClusterIndexCountLog2;

    pool->allocatedClusterIndices = circbuf_make(2);

    kv_init(pool->clusterLut);
    kv_resize(void *, pool->clusterLut, initialClusterIndexCount);
    kv_size(pool->clusterLut) = kv_max(pool->clusterLut);

    pool->unallocatedClusterIndices = circbuf_make(initialClusterIndexCountLog2);
    for (size_t i = 0; i < initialClusterIndexCount; ++i)
        circbuf_put(&pool->unallocatedClusterIndices, (void *) i);
}

// FIXME move bit stuff into own module
static int findLastSet(size_t val) {
    if (!val)
        return -1;

    int result = 0;
    if (val & 0xFFFFFFFF00000000)
        val >>= 32, result += 32;
    if (val & 0xFFFF0000)
        val >>= 16, result += 16;
    if (val & 0xFF00)
        val >>= 8, result += 8;
    if (val & 0xF0)
        val >>= 4, result += 4;
    if (val & 0xC)
        val >>= 2, result += 2;
    if (val & 0x2)
        result += 1;

    return result;
}

static inline bool multipleBits(size_t val) {
    return (val & ~(val - 1)) != val;
}

static unsigned int log2Envelope(size_t val) {
    if (!val)
        return 0;

    unsigned int lastSet = (unsigned int) findLastSet(val);
    return multipleBits(val) ? lastSet + 1 : lastSet;
}

static inline bool _idExists(const mp_pool_t *pool, mp_id_t id) {
    return id < kv_size(pool->locationLut) && kv_A(pool->locationLut, id) != (mp_id_t) -1;
}

static int testIdExists(const mp_pool_t *pool, mp_id_t id) {
    assert(id);
    if (!_idExists(pool, id)) {
        errno = MP_ERROR_INVALID_ID;
        return -1;
    }
    return 0;
}

static mp_id_t takeNextLocation(mp_pool_t *pool) {
    bool isFrontElementIndexAtEnd = (pool->frontElementIndex == pool->elementsPerCluster);
    if (isFrontElementIndexAtEnd)
        addFrontCluster(pool);
    // FIXME frontClusterIndex has nothing to do with clusterLut - it's the end of allocatedClusterIndices
    size_t frontClusterIndexIndex = CIRCBUF_FRONT_INDEX(pool->allocatedClusterIndices);
    size_t frontClusterIndex = (size_t) pool->allocatedClusterIndices.a[frontClusterIndexIndex];
    size_t location = frontClusterIndex << pool->clusterIndexOffset | pool->frontElementIndex;
    ++pool->frontElementCount;
    ++pool->frontElementIndex;
    return (mp_id_t) location;
}

static inline uint8_t *getElementStore(const mp_pool_t *pool, mp_id_t location) {
    mp_id_t clusterIndex = location >> pool->clusterIndexOffset;
    size_t elementStoreSize = pool->elementSize + sizeof(mp_id_t);
    size_t elementStoreOffset = location & pool->elementIndexMask * elementStoreSize;
    return (uint8_t *) pool->clusterLut.a[clusterIndex] + elementStoreOffset;
}

static void addFrontCluster(mp_pool_t *pool) {
    bool isClusterIndexAvailable = pool->unallocatedClusterIndices.length;
    if (!isClusterIndexAvailable)
        addClusterIndices(pool);

    bool isFreeClusterAvailable = !kv_empty(pool->freeClusters);
    void *newFront = isFreeClusterAvailable ? kv_pop(pool->freeClusters) : malloc(pool->clusterSize);
    void *newFrontIndex = circbuf_popBack(&pool->unallocatedClusterIndices);
    circbuf_dynamicPut(&pool->allocatedClusterIndices, newFrontIndex);
    kv_A(pool->clusterLut, (size_t) newFrontIndex) = newFront;

    pool->frontElementCount = 0;
    pool->frontElementIndex = 0;
}

static void addClusterIndices(mp_pool_t *pool) {
    size_t unallocatedIndex = kv_max(pool->clusterLut);
    size_t unallocatedIndexCount = kv_max(pool->clusterLut);
    kv_resize(void *, pool->clusterLut, kv_max(pool->clusterLut) * 2);
    kv_size(pool->clusterLut) = kv_max(pool->clusterLut);

    for (size_t i = 0; i < unallocatedIndexCount; ++i)
        circbuf_dynamicPut(&pool->unallocatedClusterIndices, (void *) unallocatedIndex++);

    // TODO tiny optimization: check if unallocatedClusterIndices size increase is required,
    // carry it out and replace circbuf_dynamicPut() with circbuf_put()
}

static void removeBackCluster(mp_pool_t *pool) {
    assert(pool->allocatedClusterIndices.length);

    void *backIndex = circbuf_popBack(&pool->allocatedClusterIndices);
    circbuf_dynamicPut(&pool->unallocatedClusterIndices, backIndex);
    void *back = kv_A(pool->clusterLut, (size_t) backIndex);

    if (!kv_full(pool->freeClusters))
        kv_staticPush(pool->freeClusters, back);
    else
        free(back);
}

static void destroyClusterFifos(mp_pool_t *pool) {
    free(pool->unallocatedClusterIndices.a);

    circbuf_t allocated = pool->allocatedClusterIndices;
    size_t iter = allocated.start;
    for (size_t i = 0; i < allocated.length; ++i) {
        size_t index = (size_t) CIRCBUF_NEXT(allocated, iter);
        free(kv_A(pool->clusterLut, index));
    }
    free(pool->allocatedClusterIndices.a);

    kv_destroy(pool->clusterLut);
}

// unittest
// -----------------------------------------------------------------------------
#ifdef UNITTEST
static mp_pool_t initPool(size_t elementSize, size_t elementsPerCluster, size_t freeClusterCountMax) {
    mp_pool_t pool;
    mp_poolSettings_t s = { .elementSize = elementSize, .elementsPerCluster = elementsPerCluster,
        .freeClusterCountMax = freeClusterCountMax };

    int error = mp_init(&pool, s);
    assert(!error);
    return pool;
}

int mp_initWithPlausibleSettings(void) {
    mp_pool_t pool;
    mp_poolSettings_t s = { .elementSize = 4, .elementsPerCluster = 4, .freeClusterCountMax = 1 };

    int error = mp_init(&pool, s);
    ASSERT(!error);

    mp_destroy(&pool);
    return 0;
}

int mp_initWithElementSizeOfZeroFails(void) {
    mp_pool_t pool;
    mp_poolSettings_t s = { .elementSize = 0, .elementsPerCluster = 1, .freeClusterCountMax = 1 };

    int error = mp_init(&pool, s);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_ELEMENT_SIZE);

    return 0;
}

int mp_initWithZeroElementsPerClusterFails(void) {
    mp_pool_t pool;
    mp_poolSettings_t s = { .elementSize = 4, .elementsPerCluster = 0, .freeClusterCountMax = 1 };

    int error = mp_init(&pool, s);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_ELEMENTS_PER_CLUSTER);

    return 0;
}

int mp_initInitializesClusterFifos(void) {
    mp_pool_t pool = initPool(8, 8, 8);
    size_t reservedClusterIndexCount = kv_max(pool.clusterLut);
    ASSERT(reservedClusterIndexCount);
    size_t supportingClusterFifoElementCount = pool.allocatedClusterIndices.length
        + pool.unallocatedClusterIndices.length;
    ASSERT(reservedClusterIndexCount == supportingClusterFifoElementCount);
    return 0;
}

// alloc
int mp_firstAllocReturnsNonzeroId(void) {
    mp_pool_t pool = initPool(8, 8, 8);
    mp_id_t id = 0;
    mp_alloc(&pool, &id);
    ASSERT(id);

    mp_destroy(&pool);
    return 0;
}

int mp_allocReturnsDeterministicIds(void) {
    mp_pool_t pool = initPool(2, 3, 4);
    mp_id_t id;
    mp_alloc(&pool, &id);
    mp_id_t previousId = id;
    mp_free(&pool, id);
    mp_alloc(&pool, &id);
    ASSERT(previousId == id);
    previousId = id;
    mp_alloc(&pool, &id);
    ASSERT(id == previousId + 1);

    mp_destroy(&pool);
    return 0;
}

int mp_allocIncrementsFrontIndices(void) {
    mp_pool_t pool = initPool(1, 2, 3);
    mp_id_t id;
    mp_alloc(&pool, &id);
    ASSERT(pool.frontElementCount == 1);
    // assuming frontIndex points at next free element
    ASSERT(pool.frontElementIndex == 1);

    mp_destroy(&pool);
    return 0;
}

// free
int mp_plainFree(void) {
    mp_pool_t pool = initPool(7, 6, 5);
    mp_id_t id;
    mp_alloc(&pool, &id);
    int error = mp_free(&pool, id);
    ASSERT(!error);

    mp_destroy(&pool);
    return 0;
}

int mp_freeNonexistentElementFails(void) {
    mp_pool_t pool = initPool(3, 4, 5);
    int error = mp_free(&pool, 1);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_INVALID_ID);

    mp_destroy(&pool);
    return 0;
}

// idExists
int mp_allocatedIdExists(void) {
    mp_pool_t pool = initPool(2, 2, 2);
    mp_id_t id;
    mp_alloc(&pool, &id);
    ASSERT(mp_idExists(&pool, id));

    mp_destroy(&pool);
    return 0;
}

int mp_freedIdBecomesNonexistent(void) {
    mp_pool_t pool = initPool(2, 2, 2);

    mp_id_t id;
    mp_alloc(&pool, &id);
    mp_free(&pool, id);
    ASSERT(!mp_idExists(&pool, id));

    mp_destroy(&pool);
    return 0;
}

int mp_unallocatedIdsDoNotExist(void) {
    mp_pool_t pool = initPool(2, 2, 2);

    ASSERT(!mp_idExists(&pool, 1));
    ASSERT(!mp_idExists(&pool, 42));
    ASSERT(!mp_idExists(&pool, UINT16_MAX));

    mp_destroy(&pool);
    return 0;
}

int mp_zeroIdDoesNotExist(void) {
    mp_pool_t pool = initPool(2, 2, 2);

    mp_id_t ids[2];
    mp_alloc(&pool, ids);
    mp_alloc(&pool, ids + 1);
    ASSERT(!mp_idExists(&pool, 0));

    mp_destroy(&pool);
    return 0;
}

// get
int mp_plainGet(void) {
    mp_pool_t pool = initPool(sizeof(double), 2, 2);
    mp_id_t id;
    mp_alloc(&pool, &id);
    double pi = 3.141592654;
    mp_set(&pool, id, &pi);
    double verify;
    mp_get(&pool, id, &verify);
    ASSERT(memcmp(&pi, &verify, sizeof(pi)) == 0);

    mp_destroy(&pool);
    return 0;
}

int mp_getNonexistentElementFails(void) {
    mp_pool_t pool = initPool(1, 4, 8);
    uint32_t dummy;
    int error = mp_get(&pool, 1, &dummy);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_INVALID_ID);

    mp_destroy(&pool);
    return 0;
}

// getPtr
int mp_getPtrToNonexistentElementFails(void) {
    mp_pool_t pool = initPool(2, 8, 1);
    uint32_t *dummy;
    int error = mp_getPtr(&pool, 2, (void **) &dummy);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_INVALID_ID);

    mp_destroy(&pool);
    return 0;
}

int mp_plainGetPtr(void) {
    mp_pool_t pool = initPool(sizeof(double), 2, 2);
    mp_id_t id;
    mp_alloc(&pool, &id);
    double aur = 1.618033989;
    mp_set(&pool, id, &aur);
    double *p = NULL;
    mp_getPtr(&pool, id, (void **) &p);
    ASSERT(memcmp(&aur, p, sizeof(aur)) == 0);

    mp_destroy(&pool);
    return 0;
}

// set
int mp_setNonexistentElementFails(void) {
    mp_pool_t pool = initPool(4, 8, 8);
    uint32_t dummy;
    int error = mp_set(&pool, 3, &dummy);
    ASSERT(error);
    ASSERT(errno == MP_ERROR_INVALID_ID);

    mp_destroy(&pool);
    return 0;
}

// private
// FIXME taken circbuf hast to be checked for clusterCount - replace clusterLut.length
int mp_plainAddFrontCluster(void) {
    mp_pool_t pool = initPool(3, 2, 1);
    size_t clusterCount = pool.allocatedClusterIndices.length;
    addFrontCluster(&pool);
    ASSERT(pool.allocatedClusterIndices.length == clusterCount + 1);
    ASSERT(pool.frontElementCount == 0);

    mp_destroy(&pool);
    return 0;
}

int mp_addFrontClusterResetsFrontIndices(void) {
    mp_pool_t pool = initPool(1, 2, 3);
    mp_id_t id;
    mp_alloc(&pool, &id);
    addFrontCluster(&pool);
    ASSERT(!pool.frontElementCount);
    ASSERT(!pool.frontElementIndex);

    mp_destroy(&pool);
    return 0;
}

int mp_plainRemoveBackCluster(void) {
    mp_pool_t pool = initPool(4, 4, 4);

    size_t clusterCount = pool.allocatedClusterIndices.length;
    size_t unallocatedIndexCount = pool.unallocatedClusterIndices.length;
    addFrontCluster(&pool);
    ASSERT(pool.unallocatedClusterIndices.length == unallocatedIndexCount - 1);
    removeBackCluster(&pool);
    ASSERT(pool.allocatedClusterIndices.length == clusterCount);
    ASSERT(pool.unallocatedClusterIndices.length == unallocatedIndexCount);

    mp_destroy(&pool);
    return 0;
}

int mp_findLastSet(void) {
    ASSERT(findLastSet(0) == -1);
    ASSERT(findLastSet(1 << 0) == 0);
    ASSERT(findLastSet(1 << 1) == 1);
    ASSERT(findLastSet(1 << 2) == 2);
    ASSERT(findLastSet(1 << 15) == 15);
    ASSERT(findLastSet(0x5555) == 14);
    ASSERT(findLastSet(0xAAAA) == 15);
    return 0;
}

int mp_testMultipleBits(void) {
    ASSERT(!multipleBits(0));
    ASSERT(!multipleBits(1));
    ASSERT(!multipleBits(1 << 15));
    ASSERT(multipleBits(0xC0));
    ASSERT(multipleBits(0x0A));
    ASSERT(multipleBits(3));
    return 0;
}

int mp_testGetLog2Envelope(void) {
    ASSERT(log2Envelope(0) == 0);
    ASSERT(log2Envelope(1) == 0);
    ASSERT(log2Envelope(2) == 1);
    ASSERT(log2Envelope(3) == 2);
    ASSERT(log2Envelope((1 << 15) + 1) == 16);
    size_t size_tBitCount = sizeof(size_t) * 8;
    size_t highest2PowNInSize_t = ((size_t) 1 << (size_tBitCount - 1));
    ASSERT(log2Envelope(highest2PowNInSize_t) == size_tBitCount - 1);
    return 0;
}

#endif
