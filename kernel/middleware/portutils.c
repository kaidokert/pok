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

/**
 * \file    middleware/portutils.c
 * \date    2008-2009
 * \brief   Various functions for ports management.
 * \author  Julien Delange
 */

#if defined(POK_NEEDS_PORTS_SAMPLING) || defined(POK_NEEDS_PORTS_QUEUEING)

#include <core/time.h>
#include <libc.h>
#include <middleware/port.h>
#include <middleware/queue.h>
#include <types.h>

extern pok_port_t pok_ports[POK_CONFIG_NB_PORTS];
extern pok_queue_t pok_queue;
extern uint8_t pok_current_partition;

pok_port_size_t pok_port_available_size(uint8_t pid) {
  if (pok_ports[pid].full == TRUE) {
    return 0;
  }

  if (pok_ports[pid].off_b < pok_ports[pid].off_e) {
    return (pok_ports[pid].off_b - pok_ports[pid].off_e);
  } else {
    return (pok_ports[pid].size - pok_ports[pid].off_e + pok_ports[pid].off_b);
  }
}

pok_port_size_t pok_port_consumed_size(uint8_t pid) {
  if (pok_ports[pid].empty == TRUE) {
    return 0;
  }

  if (pok_ports[pid].off_b < pok_ports[pid].off_e) {
    return (pok_ports[pid].off_e - pok_ports[pid].off_b);
  } else {
    return (pok_ports[pid].size - pok_ports[pid].off_b + pok_ports[pid].off_e);
  }
}

pok_ret_t pok_port_get(const uint32_t pid, void *data,
                       const pok_port_size_t size) {

#ifdef POK_NEEDS_PORTS_QUEUEING
  pok_port_size_t tmp_size;
  pok_port_size_t tmp_size2;
#endif

#ifdef POK_NEEDS_DEBUG
  printf("[PORT_GET] pid=%d size=%d kind=%d empty=%d\n", pid, size,
         pok_ports[pid].kind, pok_ports[pid].empty);
#endif

  switch (pok_ports[pid].kind) {

#ifdef POK_NEEDS_PORTS_QUEUEING
  case POK_PORT_KIND_QUEUEING:
    if (pok_ports[pid].empty == TRUE) {
      printf("[PORT_GET] Queue is empty, returning EINVAL\n");
      return POK_ERRNO_EINVAL;
    }

    if (pok_ports[pid].size < size) {
      return POK_ERRNO_SIZE;
    }

#ifdef POK_NEEDS_DEBUG
    if (size == 4) {
      uint32_t queue_offset = pok_ports[pid].index + pok_ports[pid].off_b;
      uint32_t *queue_ptr = (uint32_t *)&pok_queue.data[queue_offset];
      printf("[Q_READ_PRE] pid=%d idx=%d off_b=%d queue_off=%u queue=0x%x "
             "queue_value=%u\n",
             pid, pok_ports[pid].index, pok_ports[pid].off_b, queue_offset,
             (uint32_t)queue_ptr, *queue_ptr);
    }
#endif

    if ((pok_ports[pid].off_b + size) > pok_ports[pid].size) {
      tmp_size = pok_ports[pid].size - pok_ports[pid].off_b;
      memcpy(data, &pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_b],
             tmp_size);
      tmp_size2 = size - tmp_size;
      memcpy(data + tmp_size, &pok_queue.data[pok_ports[pid].index], tmp_size2);
    } else {
      memcpy(data, &pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_b],
             size);
    }

#ifdef POK_NEEDS_DEBUG
    if (size == 4) {
      uint32_t *val_ptr = (uint32_t *)data;
      printf("[Q_READ_POST] pid=%d data=0x%x read_value=%u\n", pid,
             (uint32_t)data, *val_ptr);
    }
#endif

    pok_ports[pid].off_b = (pok_ports[pid].off_b + size) % pok_ports[pid].size;

    if (pok_ports[pid].off_b == pok_ports[pid].off_e) {
      pok_ports[pid].empty = TRUE;
      pok_ports[pid].full = FALSE;
    }

    return POK_ERRNO_OK;
#endif

#ifdef POK_NEEDS_PORTS_SAMPLING
  case POK_PORT_KIND_SAMPLING:
    if (pok_ports[pid].empty == TRUE) {
      return POK_ERRNO_EMPTY;
    }

    if (size > pok_ports[pid].size) {
      return POK_ERRNO_SIZE;
    }

    memcpy(data, &pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_b],
           size);

#ifdef POK_NEEDS_DEBUG
    if (size == 4) {
      uint32_t *val_ptr = (uint32_t *)data;
      uint32_t *src_ptr =
          (uint32_t *)&pok_queue
              .data[pok_ports[pid].index + pok_ports[pid].off_b];
      printf("[PORT_READ] pid=%d idx=%d off_b=%d size=%d data_addr=0x%x "
             "src_addr=0x%x value=%u\n",
             pid, pok_ports[pid].index, pok_ports[pid].off_b, size,
             (uint32_t)data, (uint32_t)src_ptr, *val_ptr);
    }
#endif

    return POK_ERRNO_OK;
#endif

  default:
    return POK_ERRNO_EINVAL;
  }
}

pok_ret_t pok_port_write(const uint8_t pid, const void *data,
                         const pok_port_size_t size) {
#ifdef POK_NEEDS_PORTS_QUEUEING
  pok_port_size_t tmp_size;
  pok_port_size_t tmp_size2;
#endif

  switch (pok_ports[pid].kind) {
#ifdef POK_NEEDS_PORTS_QUEUEING
  case POK_PORT_KIND_QUEUEING:
    if (pok_ports[pid].full == TRUE) {
      return POK_ERRNO_SIZE;
    }

    if (size > pok_ports[pid].size) {
      return POK_ERRNO_SIZE;
    }

#ifdef POK_NEEDS_DEBUG
    if (size == 4) {
      uint32_t *val_ptr = (uint32_t *)data;
      uint32_t queue_offset = pok_ports[pid].index + pok_ports[pid].off_e;
      uint32_t *queue_ptr = (uint32_t *)&pok_queue.data[queue_offset];
      printf("[Q_WRITE] pid=%d idx=%d off_e=%d queue_off=%u data=0x%x "
             "queue=0x%x value=%u\n",
             pid, pok_ports[pid].index, pok_ports[pid].off_e, queue_offset,
             (uint32_t)data, (uint32_t)queue_ptr, *val_ptr);
    }
#endif

    if ((pok_ports[pid].off_e + size) > pok_ports[pid].size) {
      tmp_size = pok_ports[pid].size - pok_ports[pid].off_e;
      memcpy(&pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_e], data,
             tmp_size);

      tmp_size2 = size - tmp_size;
      memcpy(&pok_queue.data[pok_ports[pid].index], data + tmp_size, tmp_size2);
    } else {
      memcpy(&pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_e], data,
             size);
    }

    pok_ports[pid].off_e = (pok_ports[pid].off_e + size) % pok_ports[pid].size;

    if (pok_ports[pid].off_e == pok_ports[pid].off_b) {
      pok_ports[pid].full = TRUE;
    }

    pok_ports[pid].empty = FALSE;

    return POK_ERRNO_OK;
#endif

#ifdef POK_NEEDS_PORTS_SAMPLING
  case POK_PORT_KIND_SAMPLING:

    if (size > pok_ports[pid].size) {
      return POK_ERRNO_SIZE;
    }

#ifdef POK_NEEDS_DEBUG
    if (size == 4) {
      uint32_t *val_ptr = (uint32_t *)data;
      printf("[PORT_WRITE] pid=%d idx=%d off_e=%d size=%d data_addr=0x%x "
             "value=%u\n",
             pid, pok_ports[pid].index, pok_ports[pid].off_e, size,
             (uint32_t)data, *val_ptr);
    }
#endif

    memcpy(&pok_queue.data[pok_ports[pid].index + pok_ports[pid].off_e], data,
           size);

    pok_ports[pid].empty = FALSE;
    pok_ports[pid].last_receive = POK_GETTICK();

    return POK_ERRNO_OK;
#endif

  default:
    return POK_ERRNO_EINVAL;
  }
}

/*
 * This function is designed to transfer data from one port to another
 * It is called when we transfer all data from one partition to the
 * others.
 */
pok_ret_t pok_port_transfer(const uint8_t pid_dst, const uint8_t pid_src) {
  pok_port_size_t len = 0;
  pok_port_size_t src_len_consumed = 0;

  if (pok_ports[pid_src].empty == TRUE) {
    return POK_ERRNO_EMPTY;
  }

  if (pok_ports[pid_src].kind == POK_PORT_KIND_QUEUEING) {
    len = pok_port_available_size(pid_dst);
  } else {
    if (pok_ports[pid_src].size != pok_ports[pid_dst].size) {
      return POK_ERRNO_SIZE;
    }

    len = pok_ports[pid_src].size;
  }

  if (pok_ports[pid_src].kind == POK_PORT_KIND_QUEUEING) {
    src_len_consumed = pok_port_consumed_size(pid_src);

    if (src_len_consumed == 0) {
      return POK_ERRNO_SIZE;
    }

    if (len > src_len_consumed) {
      len = src_len_consumed;
    }
    /*
     * Here, we check the size of data produced in the source port.
     * If there is more free space in the destination port, the size
     * of copied data will be the occupied size in the source port.
     */
  }

  if (len == 0) {
    return POK_ERRNO_SIZE;
  }
  /*
   * Len is the size to copy. If size is null, it's better to return
   * directly
   */

  memcpy(&pok_queue.data[pok_ports[pid_dst].index + pok_ports[pid_dst].off_e],
         &pok_queue.data[pok_ports[pid_src].index + pok_ports[pid_src].off_b],
         len);

  if (pok_ports[pid_src].kind == POK_PORT_KIND_QUEUEING) {
    pok_ports[pid_dst].off_e =
        (pok_ports[pid_dst].off_e + len) % pok_ports[pid_dst].size;
    pok_ports[pid_src].off_b =
        (pok_ports[pid_src].off_b + len) % pok_ports[pid_src].size;

    if (pok_ports[pid_src].off_b == pok_ports[pid_src].off_e) {
      pok_ports[pid_src].empty = TRUE;
      pok_ports[pid_src].full = FALSE;
    }
  } else {
    pok_ports[pid_src].empty = TRUE;
  }

  pok_ports[pid_src].full = FALSE;
  pok_ports[pid_dst].empty = FALSE;

  return POK_ERRNO_OK;
}

#endif

#if defined(POK_NEEDS_PORTS_SAMPLING) || defined(POK_NEEDS_PORTS_QUEUEING) ||  \
    defined(POK_NEEDS_PORTS_VIRTUAL)
bool_t pok_own_port(const uint8_t partition, const uint8_t port) {
  if (port > POK_CONFIG_NB_PORTS) {
    return FALSE;
  }

#ifdef POK_CONFIG_PARTITIONS_PORTS
  uint8_t owner = ((uint8_t[])POK_CONFIG_PARTITIONS_PORTS)[port];
#ifdef POK_NEEDS_DEBUG
  printf("[OWN_PORT] partition=%d port=%d owner=%d\n", partition, port, owner);
#endif
  if (owner == partition) {
    return TRUE;
  }
#endif

  return FALSE;
}

#endif
