/* rp2350.h - the RP2350 of the Ultimate Vectrex Multicart 2 (rp2350.c): two Cortex-M33 cores (cm33.c),
 * SRAM, PSRAM, SIO (GPIO, FIFOs, spinlocks), timers, DMA, PIO, the SD card's SPI, a UART for debug text.
 * The GPIO pins are the cartridge port: the system drives some of them (the Vectrex side: ~E, the data
 * bus while the VIA answers a read, PB6, /IRQ), the chip the rest. */
#ifndef RP2350_H
#define RP2350_H
#include <stdint.h>
#include <stddef.h>

typedef struct rp2350 rp2350_t;

rp2350_t *rp_create (void);
void rp_destroy (rp2350_t *rp);
/* a .um2 image ("2CMU" header, a RAM image for 0x20000000): 0, or -1 with a message in err */
int rp_load_um2 (rp2350_t *rp, const uint8_t *data, size_t n, char *err, size_t errn);
/* the pins driven from outside (the Vectrex): mask = which, val = their levels */
void rp_set_ext (rp2350_t *rp, uint64_t mask, uint64_t val);
/* run the chip for this many system clock cycles */
void rp_run (rp2350_t *rp, uint32_t cycles);
void rp_threads (rp2350_t *rp, int on);              /* core 0 on a host thread of its own (when it pays: both cores busy) */
/* the pins the chip drives: their levels, and in *oe which it drives */
uint64_t rp_pins (rp2350_t *rp, uint64_t *oe);
uint32_t rp_sys_hz (rp2350_t *rp);
/* the PT8211 audio DAC's samples since the last call (stereo, 16 bit, interleaved), its sample rate */
int rp_audio_take (rp2350_t *rp, int16_t *buf, int max_frames, int *rate);
/* stopped on a fault (the reason), or NULL */
const char *rp_fault (rp2350_t *rp);
/* an SD card image (a FAT file system, raw sectors) or NULL for none */
void rp_set_sd (rp2350_t *rp, const char *image_path);
#endif
