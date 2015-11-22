#include <stddef.h>

typedef struct {
    void *p;
    size_t size;
} fatPtr_t;

typedef struct {
    const void *p;
    size_t size;
} constFatPtr_t;

typedef struct {
    fatPtr_t store;
    size_t used;
} store_t;

