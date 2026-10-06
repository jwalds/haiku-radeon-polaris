/*
 * png_write.h: WritePng() for the test programs: RGBA8, rows top to
 * bottom, without compression (stored deflate blocks); needs no library.
 */
#ifndef PNG_WRITE_H
#define PNG_WRITE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint32_t sCrcTable[256];

static uint32_t
Crc(uint32_t crc, const uint8_t *data, size_t size)
{
	if (sCrcTable[1] == 0) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			sCrcTable[n] = c;
		}
	}
	crc = ~crc;
	for (size_t i = 0; i < size; i++)
		crc = sCrcTable[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}


static void
Put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}


static void
WriteChunk(FILE *file, const char *type, const uint8_t *data, uint32_t size)
{
	uint8_t header[8];
	Put32(header, size);
	memcpy(header + 4, type, 4);
	fwrite(header, 1, 8, file);
	fwrite(data, 1, size, file);
	uint32_t crc = Crc(Crc(0, (const uint8_t *)type, 4), data, size);
	uint8_t tail[4];
	Put32(tail, crc);
	fwrite(tail, 1, 4, file);
}


static int
WritePng(const char *path, const uint8_t *rgba, int width, int height)
{
	size_t rowSize = 1 + width * 4;
	size_t rawSize = rowSize * height;
	uint8_t *raw = malloc(rawSize);
	for (int y = 0; y < height; y++) {
		raw[y * rowSize] = 0;
		memcpy(raw + y * rowSize + 1, rgba + y * width * 4, width * 4);
	}
	size_t blocks = (rawSize + 65534) / 65535;
	size_t zSize = 2 + rawSize + blocks * 5 + 4;
	uint8_t *z = malloc(zSize), *p = z;
	*p++ = 0x78; *p++ = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t offset = 0; offset < rawSize; offset += 65535) {
		size_t length = rawSize - offset < 65535 ? rawSize - offset : 65535;
		*p++ = offset + length == rawSize;
		*p++ = length; *p++ = length >> 8;
		*p++ = ~length; *p++ = ~length >> 8;
		memcpy(p, raw + offset, length);
		p += length;
	}
	for (size_t i = 0; i < rawSize; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	Put32(p, (b << 16) | a);
	p += 4;

	FILE *file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	static const uint8_t signature[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
	fwrite(signature, 1, 8, file);
	uint8_t ihdr[13];
	Put32(ihdr, width);
	Put32(ihdr + 4, height);
	ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	WriteChunk(file, "IHDR", ihdr, 13);
	WriteChunk(file, "IDAT", z, p - z);
	WriteChunk(file, "IEND", NULL, 0);
	fclose(file);
	free(raw);
	free(z);
	return 0;
}

#endif
