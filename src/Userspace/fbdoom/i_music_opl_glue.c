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
//	Adapts fbDOOM's module-style i_oplmusic.c (music_opl_module, the
//	real DMX-style OPL music driver playing the WAD's GENMIDI
//	instrument patches through software OPL3 emulation - see opl/) to
//	the direct I_* music API the rest of this port calls. This
//	replaces the old i_music_pd.c triangle-wave synthesizer: right
//	notes, right timing, but a bare fundamental tone will never sound
//	like the OPL guitar DOOM's soundtrack was written for.
//
//	The OPL sequencer is sample-clocked: i_sound_pd.c pulls rendered
//	music via I_PD_OPL_Mix() (opl/opl_pd.c) as part of filling each
//	audio chunk, and MIDI event callbacks fire on that same clock.
//

#include "doomtype.h"
#include "i_sound.h"

extern music_module_t music_opl_module;

static boolean opl_ok;

void I_InitMusic(void)
{
    opl_ok = music_opl_module.Init();
    if (!opl_ok)
        printf("I_InitMusic: OPL music init failed (no GENMIDI lump?); "
               "music disabled\n");
}

void I_ShutdownMusic(void)
{
    if (opl_ok)
        music_opl_module.Shutdown();
}

void I_SetMusicVolume(int volume)
{
    if (opl_ok)
        music_opl_module.SetMusicVolume(volume);
}

void I_PauseSong(void)
{
    if (opl_ok)
        music_opl_module.PauseMusic();
}

void I_ResumeSong(void)
{
    if (opl_ok)
        music_opl_module.ResumeMusic();
}

void *I_RegisterSong(void *data, int len)
{
    if (!opl_ok)
        return NULL;
    return music_opl_module.RegisterSong(data, len);
}

void I_UnRegisterSong(void *handle)
{
    if (opl_ok)
        music_opl_module.UnRegisterSong(handle);
}

void I_PlaySong(void *handle, boolean looping)
{
    if (opl_ok)
        music_opl_module.PlaySong(handle, looping);
}

void I_StopSong(void)
{
    if (opl_ok)
        music_opl_module.StopSong();
}

boolean I_MusicIsPlaying(void)
{
    if (!opl_ok)
        return false;
    return music_opl_module.MusicIsPlaying();
}
