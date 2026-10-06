/*
 * gpumem: prints the GPU memory the RadeonGfx server has allocated, through
 * the amdgpu info queries (as Linux' amdgpu_top would):
 *   vram=<bytes> vis_vram=<bytes> gtt=<bytes>
 * The test runner compares it before and after a client to find leaks.
 *
 * build: gcc -o gpumem gpumem.c -I<install>/develop/headers/libdrm
 *        -L<install>/lib -ldrm -ldrm_amdgpu
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <xf86drm.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>


int
main(void)
{
	drmDevicePtr devices[8];
	int count = drmGetDevices2(0, devices, 8);
	if (count <= 0) {
		fprintf(stderr, "gpumem: no DRM device\n");
		return 1;
	}

	int fd = -1;
	for (int i = 0; i < count && fd < 0; i++) {
		if ((devices[i]->available_nodes & (1 << DRM_NODE_RENDER)) != 0)
			fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR);
	}
	drmFreeDevices(devices, count);
	if (fd < 0) {
		fprintf(stderr, "gpumem: can't open the render node\n");
		return 1;
	}

	uint32_t major, minor;
	amdgpu_device_handle device;
	if (amdgpu_device_initialize(fd, &major, &minor, &device) != 0) {
		fprintf(stderr, "gpumem: amdgpu_device_initialize() failed\n");
		return 1;
	}

	uint64_t vram = 0, visVram = 0, gtt = 0;
	if (amdgpu_query_info(device, AMDGPU_INFO_VRAM_USAGE, sizeof(vram),
			&vram) != 0
		|| amdgpu_query_info(device, AMDGPU_INFO_VIS_VRAM_USAGE,
			sizeof(visVram), &visVram) != 0
		|| amdgpu_query_info(device, AMDGPU_INFO_GTT_USAGE, sizeof(gtt),
			&gtt) != 0) {
		fprintf(stderr, "gpumem: amdgpu_query_info() failed\n");
		return 1;
	}
	printf("vram=%llu vis_vram=%llu gtt=%llu\n", (unsigned long long)vram,
		(unsigned long long)visVram, (unsigned long long)gtt);

	amdgpu_device_deinitialize(device);
	close(fd);
	return 0;
}
