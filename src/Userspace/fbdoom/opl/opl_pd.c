//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 PureDarwin
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
//     PureDarwin OPL driver: software OPL3 emulation (opl3.c, the same
//     Nuked-lineage emulator chocolate-doom's SDL driver uses) with a
//     PULL model instead of an SDL audio callback. i_sound_pd.c calls
//     I_PD_OPL_Mix() once per output chunk; samples are rendered on
//     demand and the callback queue (which drives i_oplmusic.c's MIDI
//     sequencing) advances on the same sample clock, so event timing is
//     sample-accurate. Single-threaded by construction - every Lock/
//     Unlock is a no-op because the game loop is the only caller.
//     Structure directly mirrors opl_sdl.c minus SDL/mutexes.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"
#include "opl3.h"

static opl3_chip opl_chip;
int opl_opl3mode; /* OPL_REG_NEW state; i_oplmusic.c never enables it (OPL2-only score) */
static opl_callback_queue_t *callback_queue;
static uint64_t current_time;   // us of audio rendered since init
static uint64_t pause_offset;
static int opl_pd_paused;
static unsigned int register_num;
static unsigned int mixing_freq;
static int initialized;

typedef struct
{
    unsigned int rate;
    unsigned int enabled;
    unsigned int value;
    uint64_t expire_time;
} opl_timer_t;

static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    us = ((uint64_t) nsamples * OPL_SECOND) / mixing_freq;
    current_time += us;

    if (opl_pd_paused)
    {
        pause_offset += us;
    }

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        callback(callback_data);
    }
}

// Render nframes of OPL output and ADD it into dest (interleaved stereo
// 16-bit), advancing the sequencer callback clock in step. Called from
// i_sound_pd.c's mixer.
void I_PD_OPL_Mix(short *dest, unsigned int nframes)
{
    static short render_buf[512 * 2];
    unsigned int filled = 0;

    if (!initialized)
    {
        return;
    }

    while (filled < nframes)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;

        if (opl_pd_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = nframes - filled;
        }
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            if (next_callback_time <= current_time)
            {
                // Callback is due now (or overdue - e.g. OPL_Delay just
                // queued one at the current instant). Rendering 0 samples
                // would never reach the point where AdvanceTime() fires
                // it, so the outer loop would recompute this same
                // due-now callback forever. Render a minimal 1-sample
                // slice instead, which is enough for AdvanceTime to run
                // and pop it.
                nsamples = 1;
            }
            else
            {
                nsamples = (next_callback_time - current_time) * mixing_freq;
                nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;
                if (nsamples < 1)
                {
                    nsamples = 1;
                }
            }

            if (nsamples > nframes - filled)
            {
                nsamples = nframes - filled;
            }
        }

        while (nsamples > 0)
        {
            unsigned int chunk = nsamples > 512 ? 512 : (unsigned int)nsamples;

            OPL3_GenerateStream(&opl_chip, (Bit16s *) render_buf, chunk);

            for (unsigned int i = 0; i < chunk * 2; i++)
            {
                int mixed = (int)dest[filled * 2 + i] + (int)render_buf[i];
                if (mixed > 32767) mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                dest[filled * 2 + i] = (short)mixed;
            }

            AdvanceTime(chunk);
            filled += chunk;
            nsamples -= chunk;
        }
    }
}

// Advance the emulated clock by us synchronously, rendering into a
// scratch buffer and firing due callbacks. Backs OPL_Delay (see opl.c) -
// there is no separate audio thread in the pull model to make progress
// for a blocking wait.
void I_PD_OPL_AdvanceSync(uint64_t us)
{
    static short scratch[512 * 2];
    uint64_t nsamples;

    if (!initialized)
    {
        return;
    }

    nsamples = (us * mixing_freq) / OPL_SECOND;
    while (nsamples > 0)
    {
        unsigned int chunk = nsamples > 512 ? 512 : (unsigned int)nsamples;
        OPL3_GenerateStream(&opl_chip, (Bit16s *) scratch, chunk);
        AdvanceTime(chunk);
        nsamples -= chunk;
    }
}

static void OPL_PD_Shutdown(void)
{
    if (initialized)
    {
        OPL_Queue_Destroy(callback_queue);
        callback_queue = NULL;
        initialized = 0;
    }
}

static int OPL_PD_Init(unsigned int port_base)
{
    (void)port_base;

    opl_pd_paused = 0;
    pause_offset = 0;
    mixing_freq = opl_sample_rate;

    callback_queue = OPL_Queue_Create();
    current_time = 0;

    OPL3_Reset(&opl_chip, mixing_freq);
    opl_opl3mode = 0;

    initialized = 1;
    return 1;
}

static unsigned int OPL_PD_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;
        result |= 0x40;
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;
        result |= 0x20;
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        case OPL_REG_NEW:
            opl_opl3mode = value & 0x01;
            // fall through, the emulator wants the register too

        default:
            OPL3_WriteRegBuffered(&opl_chip, reg_num, value);
            break;
    }
}

static void OPL_PD_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_PD_SetCallback(uint64_t us, opl_callback_t callback,
                               void *data)
{
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
}

static void OPL_PD_ClearCallbacks(void)
{
    OPL_Queue_Clear(callback_queue);
}

static void OPL_PD_Lock(void)
{
}

static void OPL_PD_Unlock(void)
{
}

static void OPL_PD_SetPaused(int paused)
{
    opl_pd_paused = paused;
}

static void OPL_PD_AdjustCallbacks(float factor)
{
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
}

opl_driver_t opl_pd_driver =
{
    "PureDarwin",
    OPL_PD_Init,
    OPL_PD_Shutdown,
    OPL_PD_PortRead,
    OPL_PD_PortWrite,
    OPL_PD_SetCallback,
    OPL_PD_ClearCallbacks,
    OPL_PD_Lock,
    OPL_PD_Unlock,
    OPL_PD_SetPaused,
    OPL_PD_AdjustCallbacks,
};
