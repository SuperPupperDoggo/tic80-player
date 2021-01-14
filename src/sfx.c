// MIT License

// Copyright (c) 2017 Vadim Grigoruk @nesbox // grigoruk@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "sfx.h"
#include "history.h"

#define DEFAULT_CHANNEL 0

enum 
{
    SFX_WAVE_PANEL,
    SFX_VOLUME_PANEL,
    SFX_CHORD_PANEL,
    SFX_PITCH_PANEL,
};

static tic_sample* getEffect(Sfx* sfx)
{
    return sfx->src->samples.data + sfx->index;
}

static tic_waveform* getWaveformById(Sfx* sfx, s32 i)
{
    return &sfx->src->waveforms.items[i];
}





static void playSound(Sfx* sfx)
{
    if(sfx->play.active)
    {
        tic_sample* effect = getEffect(sfx);

        if(sfx->play.note != effect->note)
        {
            sfx->play.note = effect->note;

            sfx_stop(sfx->tic, DEFAULT_CHANNEL);
            tic_api_sfx(sfx->tic, sfx->index, effect->note, effect->octave, -1, DEFAULT_CHANNEL, MAX_VOLUME, SFX_DEF_SPEED);
        }
    }
    else
    {
        sfx->play.note = -1;
        sfx_stop(sfx->tic, DEFAULT_CHANNEL);
    }
}


static void tick(Sfx* sfx)
{
    tic_mem* tic = sfx->tic;

    sfx->play.active = false;
    sfx->hoverWave = -1;

    processKeyboard(sfx);
    processEnvelopesKeyboard(sfx);

    tic_api_cls(tic, tic_color_14);

    playSound(sfx);

    if(sfx->play.active)
        sfx->play.tick++;
    else 
        sfx->play.tick = 0;
}

static void onStudioEvent(Sfx* sfx, StudioEvent event)
{
    switch(event)
    {
    case TIC_TOOLBAR_CUT:   cutToClipboard(sfx); break;
    case TIC_TOOLBAR_COPY:  copyToClipboard(sfx); break;
    case TIC_TOOLBAR_PASTE: copyFromClipboard(sfx); break;
    case TIC_TOOLBAR_UNDO:  undo(sfx); break;
    case TIC_TOOLBAR_REDO:  redo(sfx); break;
    default: break;
    }
}

void initSfx(Sfx* sfx, tic_mem* tic, tic_sfx* src)
{
    if(sfx->history) history_delete(sfx->history);
    
    *sfx = (Sfx)
    {
        .tic = tic,
        .tick = tick,
        .src = src,
        .index = 0,
        .volwave = SFX_VOLUME_PANEL,
        .hoverWave = -1,
        .play = 
        {
            .note = -1,
            .active = false,
            .tick = 0,
        },

        .history = history_create(src, sizeof(tic_sfx)),
        .event = onStudioEvent,
    };
}

void freeSfx(Sfx* sfx)
{
    history_delete(sfx->history);
    free(sfx);
}
