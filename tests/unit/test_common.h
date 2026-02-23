#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

#define UT_ASSERT_TRUE(cond)                                                   \
  do                                                                           \
  {                                                                            \
    if (!(cond))                                                               \
    {                                                                          \
      printf("ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define UT_ASSERT_EQ_U32(exp, act)                                                          \
  do                                                                                         \
  {                                                                                          \
    if ((unsigned long)(exp) != (unsigned long)(act))                                       \
    {                                                                                        \
      printf("ASSERT_EQ_U32 failed at %s:%d: exp=%lu act=%lu\n",                            \
             __FILE__,                                                                       \
             __LINE__,                                                                       \
             (unsigned long)(exp),                                                           \
             (unsigned long)(act));                                                          \
      return 1;                                                                              \
    }                                                                                        \
  } while (0)

#endif /* TEST_COMMON_H */
