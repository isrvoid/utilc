#include "utilMacros.h"

#include <stdint.h>

#include "unittestMacros.h"

#ifdef UNITTEST
int utilMacros_elementCount(void) {
    char foo[3];
    void *bar[5];
    ASSERT(ARRAY_LENGTH(foo) == 3);
    ASSERT(ARRAY_LENGTH(bar) == 5);

    return 0;
}

int utilMacros_smallLog2(void) {
    ASSERT(UM_SMALL_LOG2(1) == 0);
    ASSERT(UM_SMALL_LOG2(2) == 1);
    ASSERT(UM_SMALL_LOG2(4) == 2);
    ASSERT(UM_SMALL_LOG2(8) == 3);

    return 0;
}

int utilMacros_bitCountLog2(void) {
    int8_t foo;
    ASSERT(UM_BIT_COUNT_LOG2(foo) == 3);
    uint16_t bar;
    ASSERT(UM_BIT_COUNT_LOG2(bar) == 4);
    int32_t fun;
    ASSERT(UM_BIT_COUNT_LOG2(fun) == 5);
    uint64_t hun;
    ASSERT(UM_BIT_COUNT_LOG2(hun) == 6);

    return 0;
}

int utlmac_bitCount(void) {
    ASSERT(UM_BIT_COUNT((uint8_t) 42) == 8);
    uint64_t foo;
    ASSERT(UM_BIT_COUNT(foo) == 64);

    return 0;
}

#endif
