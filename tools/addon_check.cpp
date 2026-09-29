/*
 * addon_check - check that an accelerant loads and exports its entry point,
 * without initializing any hardware.
 *
 * usage: addon_check <path to accelerant>
 */


#include <Accelerant.h>
#include <image.h>

#include <stdio.h>
#include <string.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <accelerant>\n", argv[0]);
		return 1;
	}

	image_id image = load_add_on(argv[1]);
	if (image < 0) {
		printf("load_add_on failed: %s\n", strerror(image));
		return 1;
	}

	GetAccelerantHook getHook;
	status_t status = get_image_symbol(image, B_ACCELERANT_ENTRY_POINT,
		B_SYMBOL_TYPE_ANY, (void**)&getHook);
	printf("entry point: %s\n", strerror(status));
	if (status == B_OK) {
		printf("init hook: %p, cursor bitmap hook (before init): %p\n",
			getHook(B_INIT_ACCELERANT, NULL),
			getHook(B_SET_CURSOR_BITMAP, NULL));
	}

	unload_add_on(image);
	return status == B_OK ? 0 : 1;
}
