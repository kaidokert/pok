/* Default processor count for single-processor ARM systems */
#ifndef POK_CONFIG_NB_PROCESSORS
#define POK_CONFIG_NB_PROCESSORS 1
#endif

/* Default processor affinity for single-processor ARM systems.
 * All partitions run on processor 0 (represented as bitmask value 1).
 * The array size is determined by POK_CONFIG_NB_PARTITIONS from deployment.h.
 */
#ifndef POK_CONFIG_PROCESSOR_AFFINITY
#ifdef POK_CONFIG_NB_PARTITIONS
#if POK_CONFIG_NB_PARTITIONS == 1
#define POK_CONFIG_PROCESSOR_AFFINITY {1}
#elif POK_CONFIG_NB_PARTITIONS == 2
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 3
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 4
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 5
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1, 1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 6
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1, 1, 1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 7
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1, 1, 1, 1, 1}
#elif POK_CONFIG_NB_PARTITIONS == 8
#define POK_CONFIG_PROCESSOR_AFFINITY {1, 1, 1, 1, 1, 1, 1, 1}
#else
#error "POK_CONFIG_PROCESSOR_AFFINITY not defined: unsupported partition count"
#endif
#else
#error                                                                         \
    "POK_CONFIG_NB_PARTITIONS must be defined before including multiprocessing.h"
#endif
#endif
