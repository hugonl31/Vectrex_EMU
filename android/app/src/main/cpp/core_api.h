/* core_api.h - the Vectrex core as a library (see core_api.c) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VX_MAX_LINES 4000           /* (as --live: at most 4000 lines a frame) */

/* one beam line, as the --live stream carries it */
typedef struct {
	uint16_t x0, y0, x1, y1;        /* emulator units, 0..33000 x 0..41000, y down */
	uint8_t bright;                 /* Z 0..127, bit 7 = the beam fading out after /BLANK */
	uint16_t energy;                /* beam energy on the line, 1/8 cycles */
	uint16_t t0;                    /* start cycle in the frame */
} vx_line_t;

typedef struct {
	vx_line_t lines[VX_MAX_LINES];
	int nlines;
	int nsamples;                   /* 44100 Hz; two_channels: interleaved console / speech module */
	int two_channels;
	const int16_t *pcm;             /* valid until the next vx_frame */
	uint32_t frame_t0;              /* the frame's drawing start, absolute cycles (low 32 bits) */
} vx_frame_t;

int vx_load (const unsigned char *bios, size_t bios_len, const unsigned char *cart, size_t cart_len, const char *sav_path);
void vx_frame (const unsigned char *msg, double rampoff, vx_frame_t *out);
void vx_close (void);

#ifdef __cplusplus
}
#endif
