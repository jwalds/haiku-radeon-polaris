/*
 * modeset_test - load radeon_hd.accelerant and set a display mode directly,
 * without app_server (debug aid; run while booted in fail-safe video mode).
 *
 * usage:
 *	modeset_test list
 *	modeset_test set <width> <height> [refresh]
 *	modeset_test fill <width> <height> [refresh]	(set + draw test pattern)
 */


#include <Accelerant.h>
#include <OS.h>
#include <image.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static const char* kDevicePath = "/dev/graphics/radeon_hd_010000";
static const char* kAccelerantPath
	= "/boot/home/config/non-packaged/add-ons/accelerants/radeon_hd.accelerant";


static uint32
mode_refresh(const display_mode& mode)
{
	uint32 total = mode.timing.h_total * mode.timing.v_total;
	if (total == 0)
		return 0;
	return (mode.timing.pixel_clock * 1000 + total / 2) / total;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s list | set <w> <h> [hz] | fill <w> <h> "
			"[hz]\n", argv[0]);
		return 1;
	}

	int device = open(kDevicePath, B_READ_WRITE);
	if (device < 0) {
		fprintf(stderr, "cannot open %s\n", kDevicePath);
		return 1;
	}

	image_id image = load_add_on(kAccelerantPath);
	if (image < 0) {
		fprintf(stderr, "cannot load %s\n", kAccelerantPath);
		return 1;
	}

	GetAccelerantHook getHook;
	if (get_image_symbol(image, B_ACCELERANT_ENTRY_POINT, B_SYMBOL_TYPE_ANY,
			(void**)&getHook) != B_OK) {
		fprintf(stderr, "no accelerant entry point\n");
		return 1;
	}

	init_accelerant initAccelerant
		= (init_accelerant)getHook(B_INIT_ACCELERANT, NULL);
	status_t status = initAccelerant(device);
	if (status != B_OK) {
		fprintf(stderr, "init_accelerant failed: %s\n", strerror(status));
		return 1;
	}

	accelerant_mode_count getModeCount
		= (accelerant_mode_count)getHook(B_ACCELERANT_MODE_COUNT, NULL);
	get_mode_list getModeList
		= (get_mode_list)getHook(B_GET_MODE_LIST, NULL);
	set_display_mode setDisplayMode
		= (set_display_mode)getHook(B_SET_DISPLAY_MODE, NULL);
	get_frame_buffer_config getFrameBufferConfig
		= (get_frame_buffer_config)getHook(B_GET_FRAME_BUFFER_CONFIG, NULL);

	uint32 modeCount = getModeCount();
	display_mode* modes = new display_mode[modeCount];
	getModeList(modes);

	if (strcmp(argv[1], "list") == 0) {
		for (uint32 i = 0; i < modeCount; i++) {
			printf("%4" B_PRIu16 "x%-4" B_PRIu16 " %3" B_PRIu32 " Hz  "
				"%6" B_PRIu32 " kHz  space 0x%" B_PRIx32 "\n",
				modes[i].virtual_width, modes[i].virtual_height,
				mode_refresh(modes[i]), modes[i].timing.pixel_clock,
				modes[i].space);
		}
		return 0;
	}

	if (argc < 4)
		return 1;

	uint32 width = strtoul(argv[2], NULL, 0);
	uint32 height = strtoul(argv[3], NULL, 0);
	uint32 refresh = argc >= 5 ? strtoul(argv[4], NULL, 0) : 60;

	display_mode* found = NULL;
	for (uint32 i = 0; i < modeCount; i++) {
		if (modes[i].virtual_width == width
			&& modes[i].virtual_height == height
			&& modes[i].space == B_RGB32_LITTLE
			&& mode_refresh(modes[i]) == refresh) {
			found = &modes[i];
			break;
		}
	}
	if (found == NULL) {
		fprintf(stderr, "mode %" B_PRIu32 "x%" B_PRIu32 "@%" B_PRIu32
			" not in list\n", width, height, refresh);
		return 1;
	}

	printf("setting %" B_PRIu16 "x%" B_PRIu16 " @ %" B_PRIu32 " kHz\n",
		found->virtual_width, found->virtual_height,
		found->timing.pixel_clock);
	status = setDisplayMode(found);
	printf("set_display_mode: %s\n", strerror(status));

	if (strcmp(argv[1], "fill") == 0 && status == B_OK) {
		frame_buffer_config config;
		getFrameBufferConfig(&config);

		// Map the frame buffer ourselves via its physical address, in case
		// the pointer from the accelerant is not usable from userland.
		void* frameBuffer;
		area_id area = map_physical_memory("modeset_test fb",
			(phys_addr_t)config.frame_buffer_dma,
			config.bytes_per_row * height, B_ANY_ADDRESS,
			B_READ_AREA | B_WRITE_AREA, &frameBuffer);
		if (area < 0) {
			fprintf(stderr, "cannot map frame buffer: %s\n",
				strerror(area));
			return 1;
		}

		// vertical colour bars: white, red, green, blue, black
		static const uint32 kColors[] = {
			0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff, 0xff000000
		};
		for (uint32 y = 0; y < height; y++) {
			uint32* row = (uint32*)((uint8*)frameBuffer
				+ y * config.bytes_per_row);
			for (uint32 x = 0; x < width; x++)
				row[x] = kColors[x * B_COUNT_OF(kColors) / width];
		}
		printf("test pattern drawn (%" B_PRIu32 " bytes per row)\n",
			config.bytes_per_row);
	}

	// keep the accelerant state; do not uninit so the mode stays
	return 0;
}
