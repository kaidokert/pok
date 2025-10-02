#include "deployment.h"
#include <core/error.h>
#include <core/kernel.h>
#include <core/partition.h>

/**************************/
/* Complete deployment.c */
/* for mutexes example   */
/**************************/

// No ports defined for mutexes example
char *pok_ports_names = "";

void pok_partition_error(uint8_t partition, uint32_t error) {
  // Simple error handling for mutexes example
  switch (partition) {
  case 0:
  case 1:
    pok_partition_set_mode(partition, POK_PARTITION_MODE_STOPPED);
    break;
  default:
    break;
  }
}
