#ifndef __MAIN_H
#define __MAIN_H

#include <stdint.h>

typedef struct
{
  uint32_t dummy;
} UART_HandleTypeDef;

#ifndef FLASH_SECTOR_6
#define FLASH_SECTOR_6 6U
#endif

#ifndef FLASH_SECTOR_7
#define FLASH_SECTOR_7 7U
#endif

#endif /* __MAIN_H */
