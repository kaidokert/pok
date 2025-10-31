/*
 *                               POK header
 *
 * The following file is a part of the POK project. Any modification should
 * be made according to the POK licence. You CANNOT use this file or a part
 * of a file for your own project.
 *
 * For more information on the POK licence, please see our LICENCE FILE
 *
 * Please follow the coding guidelines described in doc/CODING_GUIDELINES
 *
 *                                      Copyright (c) 2007-2025 POK team
 */

#ifdef POK_NEEDS_PORTS_QUEUEING
#include <core/sched.h>
#include <libc.h>
#include <middleware/port.h>
#include <types.h>

extern char *pok_ports_names[POK_CONFIG_NB_PORTS];
extern uint8_t pok_ports_kind[POK_CONFIG_NB_PORTS];

pok_ret_t pok_port_queueing_id(char *name, pok_port_id_t *id) {
  uint8_t i;

#ifdef POK_NEEDS_DEBUG
  printf("[PORT_QUEUEING_ID] Looking for port '%s'\n", name);
#endif

  for (i = 0; i < POK_CONFIG_NB_PORTS; i++) {
#ifdef POK_NEEDS_DEBUG
    printf("[PORT_QUEUEING_ID] Checking port %d: name='%s' kind=%d\n", i,
           pok_ports_names[i], pok_ports_kind[i]);
#endif
    if ((strcmp(name, pok_ports_names[i]) == 0) &&
        (pok_ports_kind[i] == POK_PORT_KIND_QUEUEING)) {

#ifdef POK_NEEDS_DEBUG
      printf("[PORT_QUEUEING_ID] Found matching port %d\n", i);
#endif

      if (!pok_own_port(POK_SCHED_CURRENT_PARTITION, i)) {
#ifdef POK_NEEDS_DEBUG
        printf("[PORT_QUEUEING_ID] Port %d not owned by partition %d\n", i,
               POK_SCHED_CURRENT_PARTITION);
#endif
        return POK_ERRNO_PORT;
      }

      *id = i;

#ifdef POK_NEEDS_DEBUG
      printf("[PORT_QUEUEING_ID] Success: port %d belongs to partition %d\n", i,
             POK_SCHED_CURRENT_PARTITION);
#endif
      return POK_ERRNO_OK;
    }
  }

#ifdef POK_NEEDS_DEBUG
  printf("[PORT_QUEUEING_ID] Port '%s' not found\n", name);
#endif
  return POK_ERRNO_NOTFOUND;
}
#endif
