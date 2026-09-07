/* GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AX_CELLS 320
#define AX_CAPACITY 0x5a00
#define AX_TICK_MS 20
#define AX_STOPPED 255
/* Fixed-width, pointer-free snapshot; identical on host and Switch. */
struct ax_cell {
 uint32_t state, start, ip, delay, boundary_delay;
 uint32_t loop_ip[2], remaining[2], total[2];
};
struct ax_player {
 char name[64];
 uint32_t size, phase_ms, wait_cell; /* wait_cell is index + 1, or zero */
 uint8_t data[AX_CAPACITY];
 struct ax_cell cells[AX_CELLS];
};
typedef void (*ax_draw_fn)(const uint32_t descriptor[7], void *context);
void ax_reset(struct ax_player *a);
bool ax_load(struct ax_player *a,const char *name,const void *data,size_t size);
bool ax_valid(const struct ax_player *a);
bool ax_control(struct ax_player *a,unsigned command,unsigned bank,unsigned cell);
bool ax_tick(struct ax_player *a,ax_draw_fn draw,void *context);
bool ax_waiting(const struct ax_player *a);
