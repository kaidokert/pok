#!/usr/bin/env python3
"""
POK Memory Layout Validator
Verifies that kernel, partition, and heap memory regions don't overlap.
Called during build to catch memory configuration errors early.
"""

import sys
import re
import argparse
from typing import List, Tuple, Optional

class MemoryRegion:
    def __init__(self, name: str, start: int, size: int):
        self.name = name
        self.start = start
        self.size = size
        self.end = start + size

    def overlaps(self, other: 'MemoryRegion') -> bool:
        """Check if two memory regions overlap"""
        return not (self.end <= other.start or other.end <= self.start)

    def __repr__(self):
        return f"{self.name}: 0x{self.start:08x} - 0x{self.end:08x} (size: 0x{self.size:x} = {self.size} bytes)"

def parse_hex_value(s: str) -> int:
    """Parse hex value from C define (handles 0x prefix, UL suffix, etc)"""
    s = s.strip()
    # Remove C-style suffixes (UL, U, L)
    s = re.sub(r'[UuLl]+$', '', s)
    # Handle hex
    if s.startswith('0x') or s.startswith('0X'):
        return int(s, 16)
    return int(s, 10)

def extract_defines(header_path: str) -> dict:
    """Extract #define values from C header file"""
    defines = {}
    try:
        with open(header_path, 'r') as f:
            for line in f:
                # Match #define NAME VALUE
                match = re.match(r'#define\s+(\w+)\s+([0-9a-fA-FxUuLl]+)', line)
                if match:
                    name, value = match.groups()
                    try:
                        defines[name] = parse_hex_value(value)
                    except ValueError:
                        pass  # Skip defines we can't parse
    except FileNotFoundError:
        print(f"ERROR: Cannot find header file: {header_path}", file=sys.stderr)
        sys.exit(1)
    return defines

def get_memory_regions(deployment_header: str, pm_header: Optional[str] = None) -> List[MemoryRegion]:
    """Extract memory region definitions from deployment.h and pm.c"""
    regions = []

    # Parse deployment.h for partition configurations
    defines = extract_defines(deployment_header)

    # Get SRAM configuration
    sram_start = defines.get('POK_CONFIG_SRAM_START', 0x20000000)
    sram_size = defines.get('POK_CONFIG_SRAM_SIZE', 0x20000)  # 128KB default

    # Kernel region (typically start of SRAM to first partition)
    kernel_start = sram_start
    kernel_size = defines.get('POK_CONFIG_KERNEL_ALLOCATED_SIZE', 0x8000)
    regions.append(MemoryRegion("Kernel", kernel_start, kernel_size))

    # Extract partition configurations
    nb_partitions = defines.get('POK_CONFIG_NB_PARTITIONS', 0)

    for i in range(nb_partitions):
        # Try different naming patterns used by deployment.h
        base_addr = None
        size = None

        # Pattern 1: POK_CONFIG_PARTITIONS_BASE[i]
        base_key = f'POK_CONFIG_PARTITIONS_BASE_{i}'
        size_key = f'POK_CONFIG_PARTITIONS_SIZE_{i}'

        # Pattern 2: POK_CONFIG_PART{i}_BASE_ADDR
        alt_base_key = f'POK_CONFIG_PART{i}_BASE_ADDR'
        alt_size_key = f'POK_CONFIG_PART{i}_SIZE'

        base_addr = defines.get(base_key) or defines.get(alt_base_key)
        size = defines.get(size_key) or defines.get(alt_size_key)

        if base_addr and size:
            regions.append(MemoryRegion(f"Partition {i}", base_addr, size))

    # Parse pm.c or pm.h for heap configuration if provided
    if pm_header:
        with open(pm_header, 'r') as f:
            content = f.read()

            # Look for heap_start assignment (various patterns)
            patterns = [
                r'pok_arm_pm_heap_start\s*=\s*pok_arm_pm_brk\s*=\s*(0x[0-9a-fA-F]+)',
                r'heap_start\s*=\s*(0x[0-9a-fA-F]+)',
            ]

            heap_start = None
            for pattern in patterns:
                match = re.search(pattern, content)
                if match:
                    heap_start = parse_hex_value(match.group(1))
                    break

            # Look for heap_end
            heap_end = None
            patterns = [
                r'pok_arm_pm_heap_end\s*=\s*(0x[0-9a-fA-F]+)',
                r'heap_end\s*=\s*(0x[0-9a-fA-F]+)',
            ]
            for pattern in patterns:
                match = re.search(pattern, content)
                if match:
                    heap_end = parse_hex_value(match.group(1))
                    break

            if heap_start and heap_end:
                heap_size = heap_end - heap_start
                regions.append(MemoryRegion("Heap", heap_start, heap_size))
            elif heap_start:
                # Use end of SRAM as heap_end if not specified
                heap_end = sram_start + sram_size
                heap_size = heap_end - heap_start
                regions.append(MemoryRegion("Heap", heap_start, heap_size))

    return regions

def check_overlaps(regions: List[MemoryRegion]) -> List[Tuple[MemoryRegion, MemoryRegion]]:
    """Check all region pairs for overlaps"""
    overlaps = []
    for i, r1 in enumerate(regions):
        for r2 in regions[i+1:]:
            if r1.overlaps(r2):
                overlaps.append((r1, r2))
    return overlaps

def check_gaps(regions: List[MemoryRegion], sram_start: int, sram_end: int) -> List[Tuple[int, int]]:
    """Check for gaps in memory layout (potential waste)"""
    sorted_regions = sorted(regions, key=lambda r: r.start)
    gaps = []

    # Check gap before first region
    if sorted_regions and sorted_regions[0].start > sram_start:
        gaps.append((sram_start, sorted_regions[0].start))

    # Check gaps between regions
    for i in range(len(sorted_regions) - 1):
        current_end = sorted_regions[i].end
        next_start = sorted_regions[i+1].start
        if next_start > current_end:
            gaps.append((current_end, next_start))

    # Check gap after last region
    if sorted_regions and sorted_regions[-1].end < sram_end:
        gaps.append((sorted_regions[-1].end, sram_end))

    return gaps

def main():
    parser = argparse.ArgumentParser(description='Validate POK memory layout')
    parser.add_argument('deployment_header', help='Path to deployment.h')
    parser.add_argument('--pm-source', help='Path to pm.c for heap configuration')
    parser.add_argument('--sram-start', type=lambda x: int(x, 0), default=0x20000000,
                        help='SRAM start address (default: 0x20000000)')
    parser.add_argument('--sram-size', type=lambda x: int(x, 0), default=0x20000,
                        help='SRAM size (default: 0x20000 = 128KB)')
    parser.add_argument('--warn-gaps', action='store_true',
                        help='Warn about gaps in memory layout')

    args = parser.parse_args()

    sram_end = args.sram_start + args.sram_size

    print("=" * 70)
    print("POK Memory Layout Validation")
    print("=" * 70)
    print(f"SRAM Region: 0x{args.sram_start:08x} - 0x{sram_end:08x} ({args.sram_size} bytes)")
    print()

    # Extract memory regions
    regions = get_memory_regions(args.deployment_header, args.pm_source)

    if not regions:
        print("WARNING: No memory regions found to validate!")
        print("Check that deployment.h contains POK_CONFIG_PARTITIONS_BASE_* defines")
        return 0

    print("Detected Memory Regions:")
    print("-" * 70)
    for region in sorted(regions, key=lambda r: r.start):
        print(f"  {region}")
    print()

    # Check for overlaps
    overlaps = check_overlaps(regions)

    if overlaps:
        print("❌ MEMORY OVERLAP DETECTED!")
        print("=" * 70)
        for r1, r2 in overlaps:
            print(f"ERROR: {r1.name} overlaps with {r2.name}")
            print(f"  {r1.name}: 0x{r1.start:08x} - 0x{r1.end:08x}")
            print(f"  {r2.name}: 0x{r2.start:08x} - 0x{r2.end:08x}")
            overlap_start = max(r1.start, r2.start)
            overlap_end = min(r1.end, r2.end)
            overlap_size = overlap_end - overlap_start
            print(f"  Overlap: 0x{overlap_start:08x} - 0x{overlap_end:08x} ({overlap_size} bytes)")
            print()
        print("=" * 70)
        print("BUILD FAILED: Fix memory layout before continuing!")
        return 1
    else:
        print("✅ No memory overlaps detected")

    # Check for out-of-bounds regions
    out_of_bounds = [r for r in regions if r.start < args.sram_start or r.end > sram_end]
    if out_of_bounds:
        print()
        print("❌ OUT-OF-BOUNDS MEMORY REGIONS DETECTED!")
        print("=" * 70)
        for region in out_of_bounds:
            print(f"ERROR: {region.name} exceeds SRAM bounds")
            print(f"  {region}")
            if region.start < args.sram_start:
                print(f"  Starts before SRAM (0x{args.sram_start:08x})")
            if region.end > sram_end:
                print(f"  Ends after SRAM (0x{sram_end:08x})")
        print("=" * 70)
        return 1
    else:
        print("✅ All regions within SRAM bounds")

    # Check for gaps (optional warning)
    if args.warn_gaps:
        gaps = check_gaps(regions, args.sram_start, sram_end)
        if gaps:
            print()
            print("⚠️  Memory Layout Gaps (potential waste):")
            print("-" * 70)
            for start, end in gaps:
                size = end - start
                print(f"  Gap: 0x{start:08x} - 0x{end:08x} ({size} bytes)")

    print()
    print("=" * 70)
    print("✅ Memory layout validation PASSED")
    print("=" * 70)
    return 0

if __name__ == '__main__':
    sys.exit(main())
