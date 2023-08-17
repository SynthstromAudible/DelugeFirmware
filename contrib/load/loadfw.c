#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef USE_ALSA
#include <alsa/asoundlib.h>
#endif

#include "util/pack.h"

void usage_exit(char *name) {
	fprintf(stderr, "usage:   %s -o output.syx {handshake} path/firmware.bin \n", name);
	fprintf(stderr, "           (print to stdout with -o -)\n");
#ifdef USE_ALSA
	fprintf(stderr, "send to alsa port:\n"
			        "         %s -a {alsa_port} {handshake} path/firmware.bin \n", name);
#endif

	exit(1);
}

static bool use_alsa = false;
static FILE *output = NULL;
#ifdef USE_ALSA
static snd_rawmidi_t *output_port = NULL;
#endif

void send(uint8_t *data, size_t len) {
	if (use_alsa) {
#ifdef USE_ALSA
		int status = snd_rawmidi_write(output_port, data, len);
		if (status < 0) {
			fprintf(stderr, "error sending data: %s", snd_strerror(status));
			exit(1);
		}
		usleep(10);
#endif
	} else {
		fwrite(data, len, 1, output);
	}
}

int main(int argc, char **argv)
{
	init_crc_table();

	if (argc < 5) usage_exit(argv[0]);

	if (!strcmp(argv[1], "-a")) {
#ifdef USE_ALSA
		use_alsa = true;
#else
		fprintf(stderr, "error: compiled without alsa\n");
		exit(1);
#endif
	} else if (!strcmp(argv[1], "-o")) {
		use_alsa = false;
	} else {
		usage_exit(argv[0]);
	}

	char *port_or_file = argv[2];
	if (use_alsa) {
#ifdef USE_ALSA
		int status = snd_rawmidi_open(NULL, &output_port, port_or_file, 0);
		if (status < 0) {
			fprintf(stderr, "Cannot open alsa port: %s\n", snd_strerror(status));
			return 2;
		}
#endif
	} else {
		if (!strcmp(port_or_file, "-")) {
			output = stdout;
		} else {
			output = fopen(port_or_file, "wb");
			if (!output) {
				fprintf(stderr, "cannot open file for output\n");
				exit(1);
			}
		}
	}

	uint32_t handshake = 0;
	sscanf(argv[3], "%x", &handshake);

	FILE *readin = fopen(argv[4], "rb");
	if (!readin) {
		fprintf(stderr, "cannot open file for reading\n");
		exit(1);
	}
	static char buffer[4*1024*1024] = {0};
	int size = fread(buffer, 1, (sizeof buffer), readin);

	// TODO: we should use the embedded size (code_end - code_start)
	if (size & 3) {
		fprintf(stderr, "weird size :P\n");
		return 1;
	}

	int segs = ((size+511)&(~511)) >> 9;
	int total_bytes = segs*512;

	uint32_t crc = get_crc((unsigned char*)buffer, size);
	printf("transfer size: %d bytes (%d segments)\n", size, segs);
	printf("crc: %x\n", crc);

	uint8_t data[1024];
	for (int seg = 0; seg < segs; seg += 1) {
		int pos_low = seg & 0x7f;
		int pos_high = (seg >> 7) & 0x7f;
		int buf_pos = 512*seg;

		unsigned char *bytes = (unsigned char*)(buffer+buf_pos);

		data[0] = 0xf0;
		data[1] = 0x7d;
		data[2] = 3;  // debug
		data[3] = 1;  // send packet
		pack_8bit_to_7bit(data+4, 5, &handshake, 4);
		data[9] = pos_low;
		data[10] = pos_high;

		const int packed_size = 586; // ceil(512+512/7)
		pack_8bit_to_7bit(data+11, packed_size, bytes, 512);
		data[11+packed_size] = 0xf7;
		int len = packed_size+12;

		send(data, len);
	}

	data[0] = 0xf0;
	data[1] = 0x7d;
	data[2] = 3; // debug
	data[3] = 2; // run

	uint32_t fields[3] = {handshake, size, crc};
	// TODO: Litle-endian mandated
	pack_8bit_to_7bit(data+4, 14, fields, 12);

	data[14+4] = 0xf7;
	send(data, 19);

	if (use_alsa) {
#ifdef USE_ALSA
		snd_rawmidi_close(output_port);
#endif
	} else {
		fclose(output);
	}
}


