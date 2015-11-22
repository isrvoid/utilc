#ifdef UNITTEST
#ifndef UNITTEST_MACROS_H_
#define UNITTEST_MACROS_H_

#ifdef ASSERT
#undef ASSERT
#endif

#define ASSERT(COND) if (!(COND)) return -1
/* _WEC - With Error Code  */
#define ASSERT_WEC(COND, EC) if (!(COND)) return (EC) ? (EC) : -1

#define MIN(a, b) ((a) < (b) ? a : b)
#define MAX(a, b) ((a) > (b) ? a : b)

#endif
#endif
