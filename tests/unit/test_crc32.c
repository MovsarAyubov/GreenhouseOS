#include "gh_crc32.h"
#include "test_common.h"

#include <string.h>

int test_crc32_run(void)
{
  static const uint8_t vec1[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  static const uint8_t vec2[] = {'a', 'b', 'c'};
  static const uint8_t empty[] = {0U};
  uint32_t crc;

  crc = gh_crc32_compute(vec1, (uint32_t)sizeof(vec1));
  UT_ASSERT_EQ_U32(0xCBF43926UL, crc);

  crc = gh_crc32_compute(vec2, (uint32_t)sizeof(vec2));
  UT_ASSERT_EQ_U32(0x352441C2UL, crc);

  crc = gh_crc32_compute(empty, 0U);
  UT_ASSERT_EQ_U32(0x00000000UL, crc);

  crc = gh_crc32_compute(NULL, 0U);
  UT_ASSERT_EQ_U32(0x00000000UL, crc);

  return 0;
}
