#ifndef GH_CRC32_H
#define GH_CRC32_H

#include <stdint.h>

uint32_t gh_crc32_compute(const uint8_t *data, uint32_t len);

#endif /* GH_CRC32_H */
