#include "gh_crc32.h"

uint32_t gh_crc32_compute(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint8_t bit;

  if (data == 0)
  {
    return 0U;
  }

  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1UL) != 0UL)
      {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return ~crc;
}
