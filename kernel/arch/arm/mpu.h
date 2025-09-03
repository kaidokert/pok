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

#ifndef __POK_ARM_MPU_H__
#define __POK_ARM_MPU_H__

#include <types.h>
#include <errno.h>

/* ARM Cortex-M MPU Register Base */
#define MPU_BASE                0xE000ED90

/* MPU Registers */
#define MPU_TYPE                (*((volatile uint32_t *)(MPU_BASE + 0x00)))
#define MPU_CTRL                (*((volatile uint32_t *)(MPU_BASE + 0x04)))
#define MPU_RNR                 (*((volatile uint32_t *)(MPU_BASE + 0x08)))
#define MPU_RBAR                (*((volatile uint32_t *)(MPU_BASE + 0x0C)))
#define MPU_RASR                (*((volatile uint32_t *)(MPU_BASE + 0x10)))

/* MPU Control Register bits */
#define MPU_CTRL_ENABLE         (1 << 0)
#define MPU_CTRL_HFNMIENA       (1 << 1)
#define MPU_CTRL_PRIVDEFENA     (1 << 2)

/* MPU Region Base Address Register bits */
#define MPU_RBAR_VALID          (1 << 4)
#define MPU_RBAR_REGION_MASK    0x0F

/* MPU Region Attribute and Size Register bits */
#define MPU_RASR_ENABLE         (1 << 0)
#define MPU_RASR_SIZE_SHIFT     1
#define MPU_RASR_SIZE_MASK      (0x1F << MPU_RASR_SIZE_SHIFT)
#define MPU_RASR_SRD_SHIFT      8
#define MPU_RASR_SRD_MASK       (0xFF << MPU_RASR_SRD_SHIFT)
#define MPU_RASR_B              (1 << 16)
#define MPU_RASR_C              (1 << 17)
#define MPU_RASR_S              (1 << 18)
#define MPU_RASR_TEX_SHIFT      19
#define MPU_RASR_TEX_MASK       (0x7 << MPU_RASR_TEX_SHIFT)
#define MPU_RASR_AP_SHIFT       24
#define MPU_RASR_AP_MASK        (0x7 << MPU_RASR_AP_SHIFT)
#define MPU_RASR_XN             (1 << 28)

/* Access Permission encoding */
#define MPU_AP_NO_ACCESS        0x0
#define MPU_AP_PRIV_RW          0x1
#define MPU_AP_PRIV_RW_USER_RO  0x2
#define MPU_AP_ALL_RW           0x3
#define MPU_AP_PRIV_RO          0x5
#define MPU_AP_ALL_RO           0x6

/* Memory attributes */
#define MPU_ATTR_NORMAL         (MPU_RASR_C | MPU_RASR_B)
#define MPU_ATTR_DEVICE         0
#define MPU_ATTR_STRONGLY_ORDERED 0

/* Maximum number of MPU regions */
#define MPU_MAX_REGIONS         8

/* MPU region configuration structure */
typedef struct {
  uint32_t base_addr;
  uint32_t size;
  uint32_t attributes;
  uint8_t  region_id;
  uint8_t  enabled;
} mpu_region_t;

/* Function prototypes */
pok_ret_t pok_mpu_init(void);
pok_ret_t pok_mpu_configure_region(uint8_t region, uint32_t base_addr, 
                                   uint32_t size, uint32_t attributes);
pok_ret_t pok_mpu_enable_region(uint8_t region);
pok_ret_t pok_mpu_disable_region(uint8_t region);
pok_ret_t pok_mpu_enable(void);
pok_ret_t pok_mpu_disable(void);
uint8_t pok_mpu_get_region_count(void);
uint32_t pok_mpu_size_to_rasr(uint32_t size);

#endif /* !__POK_ARM_MPU_H__ */