#include <stdio.h>

int test_crc32_run(void);
int test_modbus_map_run(void);
int test_config_storage_run(void);

int main(void)
{
  int rc = 0;

  rc = test_crc32_run();
  if (rc != 0)
  {
    return rc;
  }

  rc = test_modbus_map_run();
  if (rc != 0)
  {
    return rc;
  }

  rc = test_config_storage_run();
  if (rc != 0)
  {
    return rc;
  }

  printf("All unit tests passed.\n");
  return 0;
}
