/*
 * Read (and optionally write) a PCI configuration register through the
 * poke driver. Build on Haiku: g++ -o pci_config pci_config.cpp
 *
 * usage: pci_config <bus> <device> <function> <offset> <size> [value]
 * e.g.   pci_config 1 0 0 4 2        PCI command register of 01:00.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <OS.h>
#include <private/drivers/poke.h>


int
main(int argc, char** argv)
{
	if (argc != 6 && argc != 7) {
		fprintf(stderr, "usage: %s <bus> <device> <function> <offset> <size>"
			" [value]\n", argv[0]);
		return 1;
	}

	int fd = open("/dev/misc/poke", O_RDWR);
	if (fd < 0) {
		perror("/dev/misc/poke");
		return 1;
	}

	pci_io_args args = {};
	args.signature = POKE_SIGNATURE;
	args.bus = strtoul(argv[1], NULL, 0);
	args.device = strtoul(argv[2], NULL, 0);
	args.function = strtoul(argv[3], NULL, 0);
	args.offset = strtoul(argv[4], NULL, 0);
	args.size = strtoul(argv[5], NULL, 0);

	if (ioctl(fd, POKE_PCI_READ_CONFIG, &args, sizeof(args)) != 0) {
		perror("POKE_PCI_READ_CONFIG");
		return 1;
	}
	printf("%02x:%02x.%x offset %#04x: %#010" B_PRIx32 "\n", args.bus,
		args.device, args.function, args.offset, args.value);

	if (argc == 7) {
		args.value = strtoul(argv[6], NULL, 0);
		if (ioctl(fd, POKE_PCI_WRITE_CONFIG, &args, sizeof(args)) != 0) {
			perror("POKE_PCI_WRITE_CONFIG");
			return 1;
		}
		ioctl(fd, POKE_PCI_READ_CONFIG, &args, sizeof(args));
		printf("  now %#010" B_PRIx32 "\n", args.value);
	}
	close(fd);
	return 0;
}
