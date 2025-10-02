# POK ARM Cortex-M Basic Syscall Test

## Overview

This example demonstrates the **basic UART syscall functionality** of the POK ARM Cortex-M port. It verifies that the core syscall interface works: `printf()` → POK libpok → kernel syscall → ARM BSP → UART → console output.

## What This Example Proves

✅ **ARM Cortex-M Boot Sequence**: Reset vector, startup code, BSP initialization
✅ **UART Syscall Chain**: User-space printf calls reach UART via POK syscall interface
✅ **ARM BSP Integration**: STM32F4 peripheral drivers operational
✅ **Memory Layout**: Flash/RAM organization working in QEMU NetduinoPlus2
✅ **Build System**: ARM toolchain and POK build infrastructure functional

## What This Example Does NOT Test

❌ **Threading**: No `pok_thread_create()` or thread scheduling
❌ **Synchronization**: No mutexes, semaphores, or inter-thread communication
❌ **Partition Management**: No `pok_partition_set_mode()` or partition switching
❌ **Real-time Scheduling**: No POK scheduler, just simple busy loop
❌ **Memory Protection**: No MPU-based partition isolation

## Technical Details

### Code Path
The example bypasses POK threading with:
```c
/* ARM systems don't need partition mode switching */
(void)POK_PARTITION_MODE_NORMAL;
while(1) { /* ARM systems use infinite loop instead of pok_thread_wait_infinite */ }
```

The actual output comes from `/opt/m/crazy/pok/test_arm_firmware.c` which runs a simple printf loop to test the UART syscall path.

### Build & Test
```bash
# Build the ARM example
./build-basic-syscall-arm.sh

# Test with QEMU (shows actual UART output)
./run-pok-arm-simple.sh
```

### Expected Output
```
POK ARM Cortex-M Kernel starting...
ARM Cortex-M POK Test Firmware starting...
BSP initialized successfully
Test loop iteration: 0
Test loop iteration: 1
=== Heartbeat: ARM POK is running normally ===
...
```

## Status

This represents the **foundation** of the POK ARM port - basic syscall infrastructure working. To make it a complete POK implementation, the missing threading/scheduling/synchronization features need to be implemented.

## Next Steps

1. Implement ARM threading support (`pok_thread_create`, scheduler)
2. Add synchronization primitives (mutexes, semaphores)
3. Enable partition management and memory protection
4. Remove `#ifndef POK_ARCH_ARM` guards from kernel core
5. Test with real POK applications (mutexes, semaphores examples)
