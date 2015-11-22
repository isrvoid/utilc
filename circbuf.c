/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#include "circbuf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "unittestMacros.h"

// private declaration
// -----------------------------------------------------------------------------
circbuf_t circbuf_make(unsigned int capacityLog2) {
    const unsigned int ptrSizeLog2 = 1 + sizeof(void *) / 4;
    const unsigned int addressablePtrCountLog2 = sizeof(size_t) * 8 - ptrSizeLog2;
    assert(capacityLog2 <= addressablePtrCountLog2);
    circbuf_t buf = { .capacityLog2 = capacityLog2 };

    size_t capacity = (size_t) 1 << capacityLog2;
    buf.rotationMask = capacity - 1;
    buf.a = calloc(sizeof(void *), capacity);
    return buf;
}

void circbuf_put(circbuf_t *buf, void *elem) {
    assert(!CIRCBUF_FULL(*buf));

    size_t postEnd = buf->start + buf->length & buf->rotationMask;
    ++buf->length;
    buf->a[postEnd] = elem;
}

void circbuf_dynamicPut(circbuf_t *buf, void *elem) {
    if (CIRCBUF_FULL(*buf))
        circbuf_resize(buf, buf->capacityLog2 + 1);

    circbuf_put(buf, elem);
}

void *circbuf_popBack(circbuf_t *buf) {
    assert(buf->length);

    void *result = buf->a[buf->start];
    buf->start = buf->start + 1 & buf->rotationMask;
    --buf->length;
    return result;
}

void circbuf_increaseSize(circbuf_t *buf) {
    circbuf_resize(buf, buf->capacityLog2 + 1);
}

void circbuf_resize(circbuf_t *buf, unsigned int newCapacityLog2) {
    assert((1 << newCapacityLog2) >= buf->length);

    circbuf_t newBuf = circbuf_make(newCapacityLog2);
    size_t capacity = 1 << buf->capacityLog2;
    bool isWrapped = buf->length > capacity - buf->start;
    size_t n1, n2;
    if (isWrapped) {
        n1 = capacity - buf->start;
        n2 = buf->length - n1;
    } else {
        n1 = buf->length;
        n2 = 0;
    }
    memcpy(newBuf.a, buf->a + buf->start, sizeof(void *) * n1);
    memcpy(newBuf.a + n1, buf->a, sizeof(void *) * n2);
    newBuf.length = buf->length;

    free(buf->a);
    *buf = newBuf;
}

void circbuf_destroy(circbuf_t *buf) {
    for (size_t i = 0; i < buf->length; ++i) {
        size_t index = buf->start + i & buf->rotationMask;
        free(buf->a[index]);
    }
    free(buf->a);
}

#ifdef UNITTEST
#include "utilMacros.h"
static void saveDoubles(circbuf_t *buf, double *toSave, size_t count) {
    for (size_t i = 0; i < count; ++i)
        circbuf_put(buf, toSave++);
}

static bool verifyDoubles(circbuf_t *buf, double *verify, size_t count) {
    size_t iter = buf->start;
    for (size_t i = 0; i < count; ++i)
        if (memcmp(CIRCBUF_NEXT(*buf, iter), verify++, sizeof(double)))
            return false;

    return true;
}

int circbufMakeInitialization(void) {
    circbuf_t buf = circbuf_make(3);
    ASSERT(buf.a);
    ASSERT(buf.start == 0);
    ASSERT(buf.length == 0);
    ASSERT(buf.capacityLog2 == 3);
    ASSERT(buf.rotationMask == 0x7);

    circbuf_destroy(&buf);
    return 0;
}

int circbufPutIncrementsUsed(void) {
    circbuf_t buf = circbuf_make(3);
    circbuf_put(&buf, NULL);
    ASSERT(buf.length == 1);
    circbuf_put(&buf, NULL);
    ASSERT(buf.length == 2);

    circbuf_destroy(&buf);
    return 0;
}

int circbufPutInsertsAtTail(void) {
    double foo[] = { 42.3, 43.2 };
    circbuf_t buf = circbuf_make(2);
    buf.start = 2; // cause
    buf.length = 1; // wrapping
    circbuf_put(&buf, foo);
    ASSERT(buf.a[3] == foo);
    circbuf_put(&buf, foo + 1);
    ASSERT(buf.a[0] == foo + 1);

    free(buf.a);
    return 0;
}

int circbufDynamicPut(void) {
    double foo[] = { 1.1, 2.2, 3.3, 4.4, 5.5 };
    circbuf_t buf = circbuf_make(0);
    for (size_t i = 0; i < ARRAY_LENGTH(foo); ++i)
        circbuf_dynamicPut(&buf, foo + i);

    ASSERT(buf.capacityLog2 == 3);
    ASSERT(buf.length == ARRAY_LENGTH(foo));

    free(buf.a);
    return 0;
}

int circbufPopBack(void) {
    double foo[2] = { 1.1, 2.2 };
    circbuf_t buf = circbuf_make(3);
    buf.start = 7; // cause wrapping
    circbuf_put(&buf, foo);
    circbuf_put(&buf, foo + 1);
    ASSERT(circbuf_popBack(&buf) == foo);
    ASSERT(circbuf_popBack(&buf) == foo + 1);

    free(buf.a);
    return 0;
}

int circbufPopBackMovesStartAndDecrementsUsed(void) {
    circbuf_t buf = circbuf_make(1);
    buf.start = 1; // cause
    buf.length = 2; // wrapping
    circbuf_popBack(&buf);
    ASSERT(buf.start == 0);
    ASSERT(buf.length == 1);
    circbuf_popBack(&buf);
    ASSERT(buf.start == 1);
    ASSERT(buf.length == 0);

    circbuf_destroy(&buf);
    return 0;
}

int circbufSaveAndRetrieve(void) {
    double foo[3] = { 2.2, 3.3, 4.4 };
    circbuf_t buf = circbuf_make(2);
    buf.start = 3; // cause wrapping
    saveDoubles(&buf, foo, ARRAY_LENGTH(foo));

    ASSERT(verifyDoubles(&buf, foo, ARRAY_LENGTH(foo)));

    free(buf.a);
    return 0;
}

int circbufIncreaseSizeIncrementsCapacity(void) {
    circbuf_t buf = circbuf_make(0);
    circbuf_increaseSize(&buf);
    ASSERT(buf.capacityLog2 == 1);
    circbuf_increaseSize(&buf);
    ASSERT(buf.capacityLog2 == 2);

    circbuf_destroy(&buf);
    return 0;
}

int circbufResizeDoesntAffectUsed(void) {
    circbuf_t buf = circbuf_make(0);
    circbuf_put(&buf, NULL);
    circbuf_increaseSize(&buf);
    ASSERT(buf.length == 1);

    circbuf_destroy(&buf);
    return 0;
}

int circbufRetrieveAfterResize(void) {
    double foo[2] = { 1.1, 2.2 };
    circbuf_t buf = circbuf_make(1);
    buf.start = 1; // cause wrapping
    saveDoubles(&buf, foo, ARRAY_LENGTH(foo));
    circbuf_increaseSize(&buf);

    ASSERT(verifyDoubles(&buf, foo, ARRAY_LENGTH(foo)));

    free(buf.a);
    return 0;
}

#endif
