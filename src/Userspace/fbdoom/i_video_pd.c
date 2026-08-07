//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM graphics for OpenOSX, via PDGOP (the same userland
//	IOGOPFramebuffer client the puredarwingop Xorg DDX driver uses - see
//	src/Libraries/PDGOP). No X server, no /dev/fb0: this opens the GOP
//	framebuffer directly, gets back a real mapped linear buffer plus
//	width/height/stride/bpp/pixelType, and writes converted pixels
//	straight into it every frame. This file (and i_input_pd.c) replace
//	fbDOOM's own i_video_fbdev.c/i_input_tty.c/i_joystick.c, which have
//	no OpenOSX equivalent - see this component's CMakeLists.txt.
//
//-----------------------------------------------------------------------------

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <PDGOP.h>

static PDGOPFramebuffer fb;
static int fb_scaling = 1;
int usemouse = 0;

/* 32bpp palette LUT, rebuilt in I_SetPalette. */
static uint32_t palette32[256];

int usegamma = 0;

/* Globals i_video.h declares extern, that fbDOOM's own video backends
 * (i_video_fbdev.c/i_video.c - neither built here) would otherwise define. */
byte *I_VideoBuffer;
boolean screenvisible;
boolean screensaver_mode = false;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int vanilla_keyboard_mapping = 1;

void I_InitGraphics(void)
{
    kern_return_t kr = PDGOPOpen(&fb);
    if (kr != 0) {
        printf("I_InitGraphics: PDGOPOpen failed at stage '%s' (0x%x)\n",
               PDGOPLastErrorStage(), kr);
        exit(-1);
    }

    if (fb.bpp != 32) {
        printf("I_InitGraphics: unsupported framebuffer bpp=%u (only 32bpp "
               "supported)\n", fb.bpp);
        exit(-1);
    }

    printf("I_InitGraphics: GOP framebuffer %ux%u stride=%u bpp=%u "
           "pixelType=%u\n", fb.width, fb.height, fb.stride, fb.bpp,
           fb.pixelType);
    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n",
           SCREENWIDTH, SCREENHEIGHT);

    int i = M_CheckParmWithArgs("-scaling", 1);
    if (i > 0) {
        fb_scaling = atoi(myargv[i + 1]);
        printf("I_InitGraphics: Scaling factor: %d\n", fb_scaling);
    } else {
        fb_scaling = fb.width / SCREENWIDTH;
        if ((int)(fb.height / SCREENHEIGHT) < fb_scaling)
            fb_scaling = fb.height / SCREENHEIGHT;
        if (fb_scaling < 1)
            fb_scaling = 1;
        printf("I_InitGraphics: Auto-scaling factor: %d\n", fb_scaling);
    }

    I_VideoBuffer = (byte *)Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);

    screenvisible = true;

    extern void I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics(void)
{
    Z_Free(I_VideoBuffer);
    PDGOPClose(&fb);
}

void I_StartFrame(void)
{
}

__attribute__((weak)) void I_GetEvent(void)
{
}

__attribute__((weak)) void I_StartTic(void)
{
    I_GetEvent();
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    unsigned x_off = ((fb.width - (unsigned)(SCREENWIDTH * fb_scaling)) / 2) * 4;
    unsigned y_off = ((fb.height - (unsigned)(SCREENHEIGHT * fb_scaling)) / 2);

    byte *src = I_VideoBuffer;
    uint8_t *base = (uint8_t *)(uintptr_t)fb.address;

    for (int y = 0; y < SCREENHEIGHT; y++) {
        for (int rep = 0; rep < fb_scaling; rep++) {
            uint32_t *out = (uint32_t *)(base +
                (uint64_t)(y_off + y * fb_scaling + rep) * fb.stride + x_off);
            for (int x = 0; x < SCREENWIDTH; x++) {
                uint32_t px = palette32[src[x]];
                for (int rx = 0; rx < fb_scaling; rx++)
                    *out++ = px;
            }
        }
        src += SCREENWIDTH;
    }
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    for (int i = 0; i < 256; i++) {
        uint8_t r = gammatable[usegamma][*palette++];
        uint8_t g = gammatable[usegamma][*palette++];
        uint8_t b = gammatable[usegamma][*palette++];
        /* 32bpp GOP framebuffers are BGRA/XRGB in practice (QEMU
         * stdvga/virtio-gpu, real UEFI GOP); pack as 0x00RRGGBB, which
         * matches little-endian BGRX byte order in memory. */
        palette32[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = INT_MAX;

    for (int i = 0; i < 256; i++) {
        int dr = (int)((palette32[i] >> 16) & 0xff) - r;
        int dg = (int)((palette32[i] >> 8) & 0xff) - g;
        int db = (int)(palette32[i] & 0xff) - b;
        int diff = dr * dr + dg * dg + db * db;
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

void I_SetWindowTitle(char *title)
{
    (void)title;
}

void I_GraphicsCheckCommandLine(void)
{
}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    (void)func;
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables(void)
{
}

void I_DisplayFPSDots(boolean dots_on)
{
    (void)dots_on;
}

void I_CheckIsScreensaver(void)
{
}
