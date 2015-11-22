/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#include "idxpyr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "unittestMacros.h"

#define INDEX_EXISTS()   (index < (size_t) 1 << pyr->indexCountLog2)

// private declarations
// -----------------------------------------------------------------------------
static unsigned int getHeight(unsigned int indexCountLog2); // starts with 1
static inline int countTrailingZeros(uint32_t val);

// interface functions
// -----------------------------------------------------------------------------
idxpyr_t idxpyr_make(unsigned int indexCountLog2, bool stateInit) {
    indexCountLog2 = MAX(indexCountLog2, UM_BIT_COUNT_LOG2(idxpyr_block_t));
    unsigned int height = getHeight(indexCountLog2);

    idxpyr_t result = { .indexCountLog2 = indexCountLog2, .height = height, .stateInit = stateInit };

    size_t rowOffset = 0;
    size_t lowerRowLength = (size_t) 1 << indexCountLog2; // in idxpyr_block_t
    for (unsigned int i = 1; i < height; ++i) {
        lowerRowLength >>= UM_BIT_COUNT_LOG2(idxpyr_block_t);
        rowOffset += lowerRowLength;
        result.rows[i] = (void *) rowOffset;
    }
    size_t totalBlockCount = rowOffset + 1;
    size_t storeSize = totalBlockCount * sizeof(idxpyr_block_t);
    idxpyr_block_t *store = malloc(storeSize);
    result.rows[0] = store;
    result.storeSize = storeSize;

    for (unsigned int i = 1; i < height; ++i)
        result.rows[i] = store + (size_t) result.rows[i];

    idxpyr_setAll(&result, stateInit);
    return result;
}

size_t idxpyr_getFirst(idxpyr_t *pyr) {
    idxpyr_block_t topBlock = *pyr->rows[pyr->height - 1];
    if (!topBlock)
        return IDXPYR_EMPTY;

    size_t lowerBlockIndex = (size_t) countTrailingZeros(topBlock);
    for (int i = (int) pyr->height - 2; i >= 0; --i) {
        idxpyr_block_t block = pyr->rows[i][lowerBlockIndex];
        unsigned int bit = (unsigned int) countTrailingZeros(block);
        lowerBlockIndex = (lowerBlockIndex << UM_BIT_COUNT_LOG2(idxpyr_block_t)) + bit;
    }
    
    return lowerBlockIndex;
}

size_t idxpyr_popFirst(idxpyr_t *pyr) {
    size_t result = idxpyr_getFirst(pyr);
    if (result != IDXPYR_EMPTY)
        idxpyr_set(pyr, result, false);

    return result;
}

bool idxpyr_get(idxpyr_t *pyr, size_t index) {
    assert(INDEX_EXISTS());
    unsigned int bit = index & UM_BIT_COUNT(idxpyr_block_t) - 1;
    size_t blockIndex = index >> UM_BIT_COUNT_LOG2(idxpyr_block_t);
    idxpyr_block_t block = pyr->rows[0][blockIndex];
    return block & 1 << bit;
}

void idxpyr_set(idxpyr_t *pyr, size_t index, bool state) {
    assert(INDEX_EXISTS());

    bool lowerBlockState = state;
    for (unsigned int i = 0; i < pyr->height; ++i) {
        unsigned int bit = index & UM_BIT_COUNT(idxpyr_block_t) - 1;
        index >>= UM_BIT_COUNT_LOG2(idxpyr_block_t);
        idxpyr_block_t block = pyr->rows[i][index];
        block = lowerBlockState ? (idxpyr_block_t) (block | 1 << bit) : (idxpyr_block_t) (block & ~(1 << bit));

        pyr->rows[i][index] = block;
        lowerBlockState = block;
    }
}

void idxpyr_setAll(idxpyr_t *pyr, bool state) {
    int setPattern = state ? 0xFF : 0;
    memset(pyr->rows[0], setPattern, pyr->storeSize);
    if (!state)
        return;

    // fix top block
    unsigned int lastRowBlockCountLog2 = pyr->indexCountLog2 - UM_BIT_COUNT_LOG2(idxpyr_block_t);
    unsigned int topBlockActiveBitCountLog2 = lastRowBlockCountLog2 % UM_BIT_COUNT_LOG2(idxpyr_block_t);
    bool isTopBlockFilled = !topBlockActiveBitCountLog2;
    if (isTopBlockFilled)
        return;

    idxpyr_block_t *topBlock = pyr->rows[pyr->height - 1];
    *topBlock = (idxpyr_block_t) (((size_t) 1 << (1 << topBlockActiveBitCountLog2)) - 1);
}

void idxpyr_increaseSize(idxpyr_t *pyr) {
    idxpyr_t biggerPyr = idxpyr_make(pyr->indexCountLog2 + 1, pyr->stateInit);
    idxpyr_block_t *biggerPyrTopBlock = biggerPyr.rows[biggerPyr.height - 1];
    idxpyr_block_t biggerPyrTopBlockBkp = *biggerPyrTopBlock;

    size_t rowLength = (size_t) 1 << pyr->indexCountLog2;
    for (unsigned int i = 0; i < pyr->height; ++i) {
        rowLength >>= UM_BIT_COUNT_LOG2(idxpyr_block_t);
        memcpy(biggerPyr.rows[i], pyr->rows[i], rowLength * sizeof(idxpyr_block_t));
    }
    unsigned int lastRowBlockCountLog2 = pyr->indexCountLog2 - UM_BIT_COUNT_LOG2(idxpyr_block_t);
    // also nicely covers the case of height increase
    unsigned int topBlockBitCountToReset = 1 << (lastRowBlockCountLog2 % UM_BIT_COUNT_LOG2(idxpyr_block_t));
    idxpyr_block_t topBlock = *pyr->rows[pyr->height - 1];
    idxpyr_block_t resetBits = (topBlockBitCountToReset == 1) ? !!topBlock : topBlock;
    idxpyr_block_t resetMask = (idxpyr_block_t) (((size_t) 1 << topBlockBitCountToReset) - 1);
    *biggerPyrTopBlock = (biggerPyrTopBlockBkp & ~resetMask) | resetBits;

    idxpyr_destroy(pyr);
    *pyr = biggerPyr;
}

void idxpyr_destroy(idxpyr_t *pyr) {
    free(pyr->rows[0]);
    memset(pyr->rows, 0, sizeof(pyr->rows));
}

// private functions
// -----------------------------------------------------------------------------
static unsigned int getHeight(unsigned int indexCountLog2) {
    assert(indexCountLog2 >= UM_BIT_COUNT_LOG2(idxpyr_block_t) && indexCountLog2 <= IDXPYR_MAX_INDEX_COUNT_LOG2);

    unsigned int lastRowBlockCountLog2 = indexCountLog2 - UM_BIT_COUNT_LOG2(idxpyr_block_t);
    return 1 + lastRowBlockCountLog2 / UM_BIT_COUNT_LOG2(idxpyr_block_t)
        + !!(lastRowBlockCountLog2 % UM_BIT_COUNT_LOG2(idxpyr_block_t));
}

static inline int countTrailingZeros(uint32_t val) {
    if (!val)
        return -1;

    uint32_t leastBit = val & ~(val - 1);
    int result = 0;
    if (leastBit & 0xFFFF0000)
        result += 16;
    if (leastBit & 0xFF00FF00)
        result += 8;
    if (leastBit & 0xF0F0F0F0)
        result += 4;
    if (leastBit & 0xCCCCCCCC)
        result += 2;
    if (leastBit & 0xAAAAAAAA)
        result += 1;

    return result;
}

// unittest
// -----------------------------------------------------------------------------
#ifdef UNITTEST

int idxpyrMacro_maxLastRowBlockCountLog2(void) {
#define idxpyr_block_t uint8_t
    ASSERT(IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 == IDXPYR_MAX_INDEX_COUNT_LOG2 - 3);
#undef idxpyr_block_t

#define idxpyr_block_t uint16_t
    ASSERT(IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 == IDXPYR_MAX_INDEX_COUNT_LOG2 - 4);
#undef idxpyr_block_t

    return 0;
}

int idxpyrMacro_maxHeight(void) {
    bool doesntDivideEvenly;
#define idxpyr_block_t uint8_t
    doesntDivideEvenly = (IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 % 3);
    ASSERT(IDXPYR_MAX_HEIGHT == IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 / 3 + doesntDivideEvenly);
#undef idxpyr_block_t

#define idxpyr_block_t uint16_t
    doesntDivideEvenly = (IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 % 4);
    ASSERT(IDXPYR_MAX_HEIGHT == IDXPYR_MAX_LAST_ROW_BLOCK_COUNT_LOG2 / 4 + doesntDivideEvenly);
#undef idxpyr_block_t

    return 0;
}

int idxpyr_getHeight(void) {
    ASSERT(getHeight(UM_BIT_COUNT_LOG2(idxpyr_block_t)) == 1);
    ASSERT(getHeight(UM_BIT_COUNT_LOG2(idxpyr_block_t) + 1) == 2);
    unsigned int indexCountLog2 = 8;
    unsigned int indexCount = 1 << indexCountLog2;
    unsigned int height = 1;
    unsigned int maxIndexCount = UM_BIT_COUNT(idxpyr_block_t);
    while (maxIndexCount < indexCount) {
        ++height;
        maxIndexCount *= UM_BIT_COUNT(idxpyr_block_t);
    }
    ASSERT(getHeight(indexCountLog2) == height);

    return 0;
}

int idxpyr_countTrailingZeros() {
    ASSERT(countTrailingZeros(0) == -1);
    ASSERT(countTrailingZeros(0x0F00) == 8);
    ASSERT(countTrailingZeros(0x10000000) == 28);
    ASSERT(countTrailingZeros(0x20000000) == 29);
    ASSERT(countTrailingZeros(0x40000000) == 30);
    ASSERT(countTrailingZeros(0x80000000) == 31);

    return 0;
}

int idxpyr_plainMake(void) {
    idxpyr_t pyr = idxpyr_make(8, false);
    ASSERT(pyr.rows[0]);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_smallestIndexCountIsBlockBitCount(void) {
    idxpyr_t pyr = idxpyr_make(0, false);
    ASSERT(pyr.indexCountLog2 == UM_BIT_COUNT_LOG2(idxpyr_block_t));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_secondMakeParameterDeterminesInitialStateOfAllElements(void) {
    idxpyr_t pyr = idxpyr_make(6, true);
    ASSERT(idxpyr_getFirst(&pyr) == 0);
    ASSERT(idxpyr_get(&pyr, 63));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_makeCorrectlyInitializesTopBlock(void) {
    const idxpyr_block_t expectedTopRows[] = { (idxpyr_block_t) -1, 0x3, 0xF, 0xFF };
    unsigned int indexCountLog2 = UM_BIT_COUNT_LOG2(idxpyr_block_t);
    for (size_t i = 0; i < ARRAY_LENGTH(expectedTopRows); ++i) {
        idxpyr_t pyr = idxpyr_make(indexCountLog2++, true);

        idxpyr_block_t topRow = *pyr.rows[pyr.height - 1];
        ASSERT(topRow == expectedTopRows[i]);

        idxpyr_destroy(&pyr);
    }

    return 0;
}

int idxpyr_plainGetFirst(void) {
    idxpyr_t pyr = idxpyr_make(8, false);
    idxpyr_set(&pyr, 42, true);
    ASSERT(idxpyr_getFirst(&pyr) == 42);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_getFirstReturns_IDXPYR_EMPTY_forClearPyramid(void) {
    idxpyr_t pyr = idxpyr_make(3, false);
    ASSERT(idxpyr_getFirst(&pyr) == IDXPYR_EMPTY);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_getFirstWithSingleBlockPyramid(void) {
    idxpyr_t pyr = idxpyr_make(0, false);
    idxpyr_set(&pyr, 5, true);
    ASSERT(idxpyr_getFirst(&pyr) == 5);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_plainPopFirst(void) {
    idxpyr_t pyr = idxpyr_make(6, false);
    idxpyr_set(&pyr, 42, true);
    ASSERT(idxpyr_popFirst(&pyr) == 42);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_popFirstClearsReturnedIndex(void) {
    idxpyr_t pyr = idxpyr_make(6, false);
    idxpyr_set(&pyr, 42, true);
    idxpyr_popFirst(&pyr);
    ASSERT(!idxpyr_get(&pyr, 42));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_popFirstReturnsIdxpyrEmptyForClearPyramid(void) {
    idxpyr_t pyr = idxpyr_make(3, false);
    ASSERT(idxpyr_popFirst(&pyr) == IDXPYR_EMPTY);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_plainGet(void) {
    idxpyr_t pyr = idxpyr_make(5, false);
    ASSERT(!idxpyr_get(&pyr, 3));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_plainSet(void) {
    idxpyr_t pyr = idxpyr_make(8, false);
    idxpyr_set(&pyr, 42, true);
    ASSERT(idxpyr_get(&pyr, 42));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_setAllTrueSetsEveryIndex(void) {
    idxpyr_t pyr = idxpyr_make(7, false);
    idxpyr_setAll(&pyr, true);
    ASSERT(idxpyr_get(&pyr, 0));
    ASSERT(idxpyr_get(&pyr, 42));
    ASSERT(idxpyr_get(&pyr, 127));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_setAllFalseClearsEveryIndex(void) {
    idxpyr_t pyr = idxpyr_make(7, false);
    idxpyr_setAll(&pyr, true);

    idxpyr_setAll(&pyr, false);
    ASSERT(!idxpyr_get(&pyr, 0));
    ASSERT(!idxpyr_get(&pyr, 42));
    ASSERT(!idxpyr_get(&pyr, 127));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_plainIncreaseSize(void) {
    idxpyr_t pyr = idxpyr_make(6, false);
    idxpyr_increaseSize(&pyr);
    ASSERT(pyr.indexCountLog2 == 7);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_increaseSizeKeepsElements(void) {
    idxpyr_t pyr = idxpyr_make(7, false);
    idxpyr_set(&pyr, 42, true);
    idxpyr_increaseSize(&pyr);
    ASSERT(idxpyr_get(&pyr, 42));

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_increaseSizeInitializesNewElementsLikeMake(void) {
    idxpyr_t pyr = idxpyr_make(6, true);
    idxpyr_increaseSize(&pyr);
    ASSERT(idxpyr_get(&pyr, 64));
    ASSERT(idxpyr_get(&pyr, 127));

    idxpyr_destroy(&pyr);
    return 0;
}

static int testIncreaseSizeTopBlockStateCapturing(unsigned int indexCountLog2) {
    size_t indexCount = (size_t) 1 << indexCountLog2;
    idxpyr_t pyr = idxpyr_make(indexCountLog2, true);
    for (size_t i = 0; i < indexCount; ++i)
        idxpyr_popFirst(&pyr);

    idxpyr_increaseSize(&pyr);
    ASSERT(idxpyr_getFirst(&pyr) == indexCount);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_increaseSizeTopBlockCapturesPreviousState1(void) {
    unsigned int indexCountLog2 = UM_BIT_COUNT_LOG2(idxpyr_block_t) + 1;
    ASSERT(!testIncreaseSizeTopBlockStateCapturing(indexCountLog2++));
    ASSERT(!testIncreaseSizeTopBlockStateCapturing(indexCountLog2));
    return 0;
}

int idxpyr_increaseSizeTopBlockCapturesPreviousState2(void) {
    unsigned int indexCountLog2 = UM_BIT_COUNT_LOG2(idxpyr_block_t) + 3;
    size_t elementsToRemove = ((size_t) 1 << indexCountLog2) / 2 - 3;
    idxpyr_t pyr = idxpyr_make(indexCountLog2, true);
    for (size_t i = 0; i < elementsToRemove; ++i)
        idxpyr_popFirst(&pyr);

    idxpyr_increaseSize(&pyr);
    ASSERT(idxpyr_getFirst(&pyr) == elementsToRemove);

    idxpyr_destroy(&pyr);
    return 0;
}

int idxpyr_increaseSizeTopBlockCapturesPreviousStateAfterHeightIncrease(void) {
    unsigned int indexCountLog2 = UM_BIT_COUNT_LOG2(idxpyr_block_t);
    return testIncreaseSizeTopBlockStateCapturing(indexCountLog2);
}

#endif
