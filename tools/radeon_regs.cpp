/*
 * radeon_regs - peek/poke radeon_hd MMIO registers from userland (debug aid)
 *
 * Uses the radeon_hd kernel driver's shared info to clone the register
 * area, like the accelerant does. Register indices are dword indices
 * (Linux amdgpu "mm" numbering); the byte offset is index * 4.
 *
 * usage:
 *	radeon_regs read <index> [count]
 *	radeon_regs write <index> <value>
 *	radeon_regs dump		(same ranges as tools/linux-capture.sh)
 */


#include <OS.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "radeon_hd.h"


static const char* kDevicePath = "/dev/graphics/radeon_hd_010000";

static const uint32 kDumpRanges[][2] = {
	{0x00c0, 0x0100},	// VGA
	{0x0800, 0x0820},	// MC_VM
	{0x0b00, 0x0b10},	// HDP
	{0x1500, 0x1540},	// BIF
	{0x1a00, 0x1c00},	// CRTC0 pipe
	{0x4800, 0x5200},	// DIG, HPD, AUX, PHY
	{0x0100, 0x0200},	// DCCG, PLL
	{0x0300, 0x0400}	// DMIF, DCFE
};


static volatile uint32* sRegisters;
static size_t sRegistersSize;


static status_t
map_registers(const char* path)
{
	int device = open(path, B_READ_WRITE);
	if (device < 0) {
		fprintf(stderr, "cannot open %s\n", path);
		return B_ERROR;
	}

	radeon_get_private_data data;
	data.magic = RADEON_PRIVATE_DATA_MAGIC;
	if (ioctl(device, RADEON_GET_PRIVATE_DATA, &data,
			sizeof(radeon_get_private_data)) != 0) {
		fprintf(stderr, "RADEON_GET_PRIVATE_DATA failed\n");
		close(device);
		return B_ERROR;
	}

	radeon_shared_info* sharedInfo;
	area_id sharedArea = clone_area("radeon_regs shared info",
		(void**)&sharedInfo, B_ANY_ADDRESS, B_READ_AREA,
		data.shared_info_area);
	if (sharedArea < 0) {
		fprintf(stderr, "cannot clone shared info\n");
		close(device);
		return B_ERROR;
	}

	area_id registersArea = clone_area("radeon_regs registers",
		(void**)&sRegisters, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		sharedInfo->registers_area);
	if (registersArea < 0) {
		fprintf(stderr, "cannot clone registers\n");
		close(device);
		return B_ERROR;
	}

	area_info info;
	get_area_info(registersArea, &info);
	sRegistersSize = info.size;

	// the device stays open until we exit
	return B_OK;
}


static bool
valid_index(uint32 index)
{
	return index * 4 + 4 <= sRegistersSize;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s read <index> [count] | write <index> "
			"<value> | dump\n", argv[0]);
		return 1;
	}

	if (map_registers(kDevicePath) != B_OK)
		return 1;

	if (strcmp(argv[1], "read") == 0 && argc >= 3) {
		uint32 index = strtoul(argv[2], NULL, 0);
		uint32 count = argc >= 4 ? strtoul(argv[3], NULL, 0) : 1;
		for (uint32 i = 0; i < count && valid_index(index + i); i++) {
			printf("0x%04" B_PRIx32 " 0x%08" B_PRIx32 "\n", index + i,
				sRegisters[index + i]);
		}
	} else if (strcmp(argv[1], "write") == 0 && argc >= 4) {
		uint32 index = strtoul(argv[2], NULL, 0);
		uint32 value = strtoul(argv[3], NULL, 0);
		if (!valid_index(index))
			return 1;
		uint32 oldValue = sRegisters[index];
		sRegisters[index] = value;
		printf("0x%04" B_PRIx32 " 0x%08" B_PRIx32 " -> 0x%08" B_PRIx32
			" (now 0x%08" B_PRIx32 ")\n", index, oldValue, value,
			sRegisters[index]);
	} else if (strcmp(argv[1], "dump") == 0) {
		for (uint32 range = 0; range < B_COUNT_OF(kDumpRanges); range++) {
			for (uint32 index = kDumpRanges[range][0];
					index < kDumpRanges[range][1]; index++) {
				uint32 value = sRegisters[index];
				if (value != 0)
					printf("0x%04" B_PRIx32 " 0x%08" B_PRIx32 "\n", index, value);
			}
		}
	} else {
		fprintf(stderr, "unknown command\n");
		return 1;
	}

	return 0;
}
