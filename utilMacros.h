#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(*(x)))

#define STATIC_ASSERT(cond, msg) typedef char static_assertion_##msg[!!(cond) * 2 - 1]

// for non 0 values LTE sizeof(void *) - invalid otherwise
#define UM_SMALL_LOG2(val) ((val) > 4 ? 3 : (val) > 2 ? 2 : (val) > 1 ? 1 : 0)

#define UM_BIT_COUNT_LOG2(val) (UM_SMALL_LOG2(sizeof(val)) + 3)

#define UM_BIT_COUNT(val) (sizeof(val) << 3)
