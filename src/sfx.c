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


static void drawCanvasLeds(Sfx* sfx, s32 x, s32 y, s32 canvasTab)
{
    tic_mem* tic = sfx->tic;

    enum 
    {
        Cols = SFX_TICKS, Rows = 16, 
        Gap = 1, LedWidth = 3 + Gap, LedHeight = 1 + Gap,
        Width = LedWidth * Cols + Gap, 
        Height = LedHeight * Rows + Gap
    };

    {
        const tic_sfx_pos* pos = &tic->ram.sfxpos[DEFAULT_CHANNEL];
        s32 tickIndex = *(pos->data + canvasTab);

    }

    tic_rect rect = {x, y, Width - Gap, Height - Gap};

    tic_sample* effect = getEffect(sfx);
    tic_rect border = {-1};

    if(checkMousePos(&rect))
    {
        setCursor(tic_cursor_hand);

        s32 mx = getMouseX() - x;
        s32 my = getMouseY() - y;
        mx /= LedWidth;
        s32 vy = my /= LedHeight;
        border = (tic_rect){x + mx * LedWidth + Gap, y + my * LedHeight + Gap, LedWidth - Gap, LedHeight - Gap};

        switch(canvasTab)
        {
        case SFX_VOLUME_PANEL: vy = MAX_VOLUME - my; break;
        case SFX_WAVE_PANEL:   sfx->hoverWave = vy = my = Rows - my - 1; break;
        case SFX_CHORD_PANEL:  vy = my = Rows - my - 1; break;
        case SFX_PITCH_PANEL:  vy = my = Rows / 2 - my - 1; break;
        default: break;
        }

        SHOW_TOOLTIP("[x=%02i y=%02i]", mx, vy);

        if(checkMouseDown(&rect, tic_mouse_left))
        {
            switch(canvasTab)
            {
            case SFX_WAVE_PANEL:   effect->data[mx].wave = my; break;
            case SFX_VOLUME_PANEL: effect->data[mx].volume = my; break;
            case SFX_CHORD_PANEL:  effect->data[mx].chord = my; break;
            case SFX_PITCH_PANEL:  effect->data[mx].pitch = my; break;
            default: break;
            }

            history_add(sfx->history);
        }
    }

    for(s32 i = 0; i < Cols; i++)
    {
        switch(canvasTab)
        {
        case SFX_WAVE_PANEL:
            for(s32 j = 1, start = Height - LedHeight, value = effect->data[i].wave + 1; j <= value; j++, start -= LedHeight)
                tic_api_rect(tic, x + i * LedWidth + Gap, y + start, LedWidth-Gap, LedHeight-Gap, j == value ? tic_color_2 : tic_color_3);
            break;

        case SFX_VOLUME_PANEL:
            for(s32 j = 1, start = Height - LedHeight, value = Rows - effect->data[i].volume; j <= value; j++, start -= LedHeight)
                tic_api_rect(tic, x + i * LedWidth + Gap, y + start, LedWidth-Gap, LedHeight-Gap, j == value ? tic_color_9 : tic_color_10);
            break;

        case SFX_CHORD_PANEL:
            for(s32 j = 1, start = Height - LedHeight, value = effect->data[i].chord + 1; j <= value; j++, start -= LedHeight)
                tic_api_rect(tic, x + i * LedWidth + Gap, y + start, LedWidth-Gap, LedHeight-Gap, j == value ? tic_color_6 : tic_color_5);
            break;

        case SFX_PITCH_PANEL:
            for(s32 value = effect->data[i].pitch, j = MIN(0, value); j <= MAX(0, value); j++)
                tic_api_rect(tic, x + i * LedWidth + Gap, y + (Height / 2 - (j + 1) * LedHeight + Gap),
                    LedWidth-Gap, LedHeight-Gap, j == value ? tic_color_3 : tic_color_4);
            break;
        }
    }

    {
        tic_sound_loop* loop = effect->loops + canvasTab;
        if(loop->size > 0)
            for(s32 r = 0; r < Rows; r++)
            {
                tic_api_rect(tic, x + loop->start * LedWidth + 2, y + Gap + r * LedHeight, 1, 1, tic_color_12);
                tic_api_rect(tic, x + (loop->start + loop->size-1) * LedWidth + 2, y + Gap + r * LedHeight, 1, 1, tic_color_12);    
            }
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

static void undo(Sfx* sfx)
{
    history_undo(sfx->history);
}

static void redo(Sfx* sfx)
{
    history_redo(sfx->history);
}

static void copyToClipboard(Sfx* sfx)
{
    tic_sample* effect = getEffect(sfx);
    toClipboard(effect, sizeof(tic_sample), true);
}

static void resetSfx(Sfx* sfx)
{
    tic_sample* effect = getEffect(sfx);
    memset(effect, 0, sizeof(tic_sample));

    history_add(sfx->history);
}

static void cutToClipboard(Sfx* sfx)
{
    copyToClipboard(sfx);
    resetSfx(sfx);
}

static void copyFromClipboard(Sfx* sfx)
{
    tic_sample* effect = getEffect(sfx);

    if(fromClipboard(effect, sizeof(tic_sample), true, false))
        history_add(sfx->history);
}

static void processKeyboard(Sfx* sfx)
{
    tic_mem* tic = sfx->tic;

    if(tic->ram.input.keyboard.data == 0) return;

    bool ctrl = tic_api_key(tic, tic_key_ctrl);

    s32 keyboardButton = -1;

    static const s32 Keycodes[] = 
    {
        tic_key_z,
        tic_key_s,
        tic_key_x,
        tic_key_d,
    };

    if(ctrl)
    {

    }
    else
    {
        for(int i = 0; i < COUNT_OF(Keycodes); i++)
            if(tic_api_key(tic, Keycodes[i]))
                keyboardButton = i;        
    }

    tic_sample* effect = getEffect(sfx);

    if(keyboardButton >= 0)
    {
        effect->note = keyboardButton;
        sfx->play.active = true;
    }

    if(tic_api_key(tic, tic_key_space))
        sfx->play.active = true;
}

static void processEnvelopesKeyboard(Sfx* sfx)
{
    tic_mem* tic = sfx->tic;
    bool ctrl = tic_api_key(tic, tic_key_ctrl);

    switch(getClipboardEvent())
    {
    case TIC_CLIPBOARD_CUT: cutToClipboard(sfx); break;
    case TIC_CLIPBOARD_COPY: copyToClipboard(sfx); break;
    case TIC_CLIPBOARD_PASTE: copyFromClipboard(sfx); break;
    default: break;
    }

    if(ctrl)
    {
        if(keyWasPressed(tic_key_z))        undo(sfx);
        else if(keyWasPressed(tic_key_y))   redo(sfx);
    }

    else if(keyWasPressed(tic_key_left))    sfx->index--;
    else if(keyWasPressed(tic_key_delete))  resetSfx(sfx);
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
*/
