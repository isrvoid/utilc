/*
 * Copyright:  Copyright Johannes Teichrieb 2015
 * License:    opensource.org/licenses/MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void **a;
    size_t start;
    size_t length;
    unsigned int capacityLog2;
    size_t rotationMask;
} circbuf_t;

#define CIRCBUF_FULL(buf) ((buf).length >= (1 << (buf).capacityLog2))

#define CIRCBUF_ITER_PP(buf, iter) (iter = iter + 1 & (buf).rotationMask)
#define CIRCBUF_NEXT(buf, iter) (iter &= (buf).rotationMask, (buf).a[iter++])
#define CIRCBUF_FRONT_INDEX(buf) ((buf).start + (buf).length - !!(buf).length & (buf).rotationMask)

// allocated and static memory shouldn't be mixed

circbuf_t circbuf_make(unsigned int capacityLog2);

void circbuf_put(circbuf_t *buf, void *elem);
// if no space is left, increases the size before putting
void circbuf_dynamicPut(circbuf_t *buf, void *elem);
void *circbuf_popBack(circbuf_t *buf);
void circbuf_increaseSize(circbuf_t *buf);
void circbuf_resize(circbuf_t *buf, unsigned int newCapacityLog2);
// calls free on every element and .a itself - for allocated elements only
// for static memory simply call free(buf.a)
void circbuf_destroy(circbuf_t *buf);
