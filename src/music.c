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

#include "music.h"
#include "history.h"

#include <ctype.h>

#define TRACKER_ROWS (MUSIC_PATTERN_ROWS / 4)
#define CHANNEL_COLS 8
#define TRACKER_COLS (TIC_SOUND_CHANNELS * CHANNEL_COLS)
#define PIANO_PATTERN_HEADER 10

#define CMD_STRING(_, c, ...) DEF2STR(c)
static const char MusicCommands[] = MUSIC_CMD_LIST(CMD_STRING);
#undef CMD_STRING

enum PianoEditColumns
{
    PianoChannel1Column = 0,
    PianoChannel2Column,
    PianoChannel3Column,
    PianoChannel4Column,
    PianoSfxColumn,
    PianoXYColumn,

    PianoColumnsCount
};

enum
{
    ColumnNote = 0,
    ColumnSemitone,
    ColumnOctave,
    ColumnSfxHi,
    ColumnSfxLow,
    ColumnCommand,
    ColumnParameter1,
    ColumnParameter2,
};


static const tic_sound_state* getMusicPos(Music* music)
{
    return &music->tic->ram.sound_state;
}


static tic_track* getTrack(Music* music)
{
    return &music->src->tracks.data[music->track];
}

static s32 getRows(Music* music)
{
    tic_track* track = getTrack(music);

    return MUSIC_PATTERN_ROWS - track->rows;
}

static void updateScroll(Music* music)
{
    music->scroll.pos = CLAMP(music->scroll.pos, 0, getRows(music) - TRACKER_ROWS);
}

static void updateTracker(Music* music)
{
    s32 row = music->tracker.edit.y;

    enum{Threshold = TRACKER_ROWS / 2};
    music->scroll.pos = CLAMP(music->scroll.pos, row - (TRACKER_ROWS - Threshold), row - Threshold);

    {
        s32 rows = getRows(music);
        if (music->tracker.edit.y >= rows) music->tracker.edit.y = rows - 1;
    }

    updateScroll(music);
}

static void upRow(Music* music)
{
    if (music->tracker.edit.y > -1)
    {
        music->tracker.edit.y--;
        updateTracker(music);
    }
}

static void downRow(Music* music)
{
    const tic_sound_state* pos = getMusicPos(music);
    // Don't move the cursor if the track is being played/recorded
    if(pos->music.track == music->track && music->follow) return;

    if (music->tracker.edit.y < getRows(music) - 1)
    {
        music->tracker.edit.y++;
        updateTracker(music);
    }
}

static void leftCol(Music* music)
{
    if (music->tracker.edit.x > 0)
    {
        music->tracker.edit.x--;
        updateTracker(music);
    }
}

static void rightCol(Music* music)
{
    if (music->tracker.edit.x < TRACKER_COLS - 1)
    {
        music->tracker.edit.x++;
        updateTracker(music);
    }
}

static void goHome(Music* music)
{
    music->tracker.edit.x -= music->tracker.edit.x % CHANNEL_COLS;
}

static void goEnd(Music* music)
{
    music->tracker.edit.x -= music->tracker.edit.x % CHANNEL_COLS;
    music->tracker.edit.x += CHANNEL_COLS-1;
}

static void pageUp(Music* music)
{
    music->tracker.edit.y -= TRACKER_ROWS;
    if(music->tracker.edit.y < 0) 
        music->tracker.edit.y = 0;

    updateTracker(music);
}

static void pageDown(Music* music)
{
    if (music->tracker.edit.y < getRows(music) - 1)

    music->tracker.edit.y += TRACKER_ROWS;
    s32 rows = getRows(music);

    if(music->tracker.edit.y >= rows) 
        music->tracker.edit.y = rows-1;
    
    updateTracker(music);
}

static void doTab(Music* music)
{
    s32 channel = (music->tracker.edit.x / CHANNEL_COLS + 1) % TIC_SOUND_CHANNELS;

    music->tracker.edit.x = channel * CHANNEL_COLS + music->tracker.edit.x % CHANNEL_COLS;

    updateTracker(music);
}

static void upFrame(Music* music)
{
    music->frame--;

    if(music->frame < 0)
        music->frame = 0;
}

static void downFrame(Music* music)
{
    music->frame++;

    if(music->frame >= MUSIC_FRAMES)
        music->frame = MUSIC_FRAMES-1;
}

static bool checkPlayFrame(Music* music, s32 frame)
{
    const tic_sound_state* pos = getMusicPos(music);

    return pos->music.track == music->track &&
        pos->music.frame == frame;
}

static bool checkPlayRow(Music* music, s32 row)
{
    const tic_sound_state* pos = getMusicPos(music);

    return checkPlayFrame(music, music->frame) && pos->music.row == row;
}

static tic_track_pattern* getFramePattern(Music* music, s32 channel, s32 frame)
{
    s32 patternId = tic_tool_get_pattern_id(getTrack(music), frame, channel);

    return patternId ? &music->src->patterns.data[patternId - PATTERN_START] : NULL;
}

static tic_track_pattern* getPattern(Music* music, s32 channel)
{
    return getFramePattern(music, channel, music->frame);
}

static tic_track_pattern* getChannelPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    return getPattern(music, channel);
}

static s32 getNote(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return pattern->rows[music->tracker.edit.y].note - NoteStart;
}

static s32 getOctave(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return pattern->rows[music->tracker.edit.y].octave;
}

static s32 getSfx(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return tic_tool_get_track_row_sfx(&pattern->rows[music->tracker.edit.y]);
}

static inline tic_music_state getMusicState(Music* music)
{
    return music->tic->ram.sound_state.flag.music_state;
}

static inline void setMusicState(Music* music, tic_music_state state)
{
    music->tic->ram.sound_state.flag.music_state = state;
}

static void playNote(Music* music, const tic_track_row* row)
{
    tic_mem* tic = music->tic;

    if(getMusicState(music) == tic_music_stop && row->note >= NoteStart)
    {
        s32 channel = music->piano.col;
        sfx_stop(tic, channel);
        tic_api_sfx(tic, tic_tool_get_track_row_sfx(row), row->note - NoteStart, row->octave, TIC80_FRAMERATE / 4, channel, MAX_VOLUME, 0);
    }
}

static void setSfx(Music* music, s32 sfx)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    tic_tool_set_track_row_sfx(row, sfx);

    music->last.sfx = tic_tool_get_track_row_sfx(&pattern->rows[music->tracker.edit.y]);

    playNote(music, row);
}

static void setStopNote(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    pattern->rows[music->tracker.edit.y].note = NoteStop;
    pattern->rows[music->tracker.edit.y].octave = 0;
}

static void setNote(Music* music, s32 note, s32 octave, s32 sfx)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];
    row->note = note + NoteStart;
    row->octave = octave;
    tic_tool_set_track_row_sfx(row, sfx);

    playNote(music, row);
}

static void setOctave(Music* music, s32 octave)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    tic_track_row* row = &pattern->rows[music->tracker.edit.y];
    row->octave = octave;

    music->last.octave = octave;

    playNote(music, row);
}

static void setCommandDefaults(tic_track_row* row)
{
    switch(row->command)
    {
    case tic_music_cmd_volume:
        row->param2 = row->param1 = MAX_VOLUME;
        break;
    case tic_music_cmd_pitch:
        row->param1 = PITCH_DELTA >> 4;
        row->param2 = PITCH_DELTA & 0xf;
    default: break;
    }
}

static void setCommand(Music* music, tic_music_command command)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    tic_music_command prev = row->command;
    row->command = command;

    if(prev == tic_music_cmd_empty)
        setCommandDefaults(row);
}

static void setParam1(Music* music, u8 value)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    row->param1 = value;
}

static void setParam2(Music* music, u8 value)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    row->param2 = value;
}

static void playFrameRow(Music* music)
{
    tic_mem* tic = music->tic;

    tic_api_music(tic, music->track, music->frame, music->tracker.edit.y, true, music->sustain);
    
    setMusicState(music, tic_music_play_frame);
}

static void playFrame(Music* music)
{
    tic_mem* tic = music->tic;

    tic_api_music(tic, music->track, music->frame, -1, true, music->sustain);

    setMusicState(music, tic_music_play_frame);
}

static void playTrack(Music* music)
{
    tic_api_music(music->tic, music->track, -1, -1, true, music->sustain);
}

static void stopTrack(Music* music)
{
    tic_api_music(music->tic, -1, -1, -1, false, music->sustain);
}

static void toggleFollowMode(Music* music)
{
    music->follow = !music->follow;
}

static void toggleSustainMode(Music* music)
{
    music->tic->ram.sound_state.flag.music_sustain = !music->sustain;
    music->sustain = !music->sustain;
}

static void resetSelection(Music* music)
{
    music->tracker.select.start = (tic_point){-1, -1};
    music->tracker.select.rect = (tic_rect){0, 0, 0, 0};
}

static void deleteSelection(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_rect rect = music->tracker.select.rect;

        if(rect.h <= 0)
        {
            rect.y = music->tracker.edit.y;
            rect.h = 1;
        }

        enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS};
        memset(&pattern->rows[rect.y], 0, RowSize * rect.h);
    }
}

typedef struct
{
    u8 size;
} ClipboardHeader;

static void copyPianoToClipboard(Music* music, bool cut)
{
    tic_track_pattern* pattern = getFramePattern(music, music->piano.col, music->frame);

    if(pattern)
    {
        ClipboardHeader header = {MUSIC_PATTERN_ROWS};

        enum{HeaderSize = sizeof(ClipboardHeader), Size = sizeof(tic_track_pattern) + HeaderSize};

        u8* data = malloc(Size);

        if(data)
        {
            memcpy(data, &header, HeaderSize);
            memcpy(data + HeaderSize, pattern->rows, sizeof(tic_track_pattern));

            free(data);

            if(cut)
            {
                memset(pattern->rows, 0, sizeof(tic_track_pattern));
                history_add(music->history);
            }
        }       
    }
}

static void copyPianoFromClipboard(Music* music)
{
    tic_track_pattern* pattern = getFramePattern(music, music->piano.col, music->frame);

    if(pattern && getSystem()->hasClipboardText())
    {
        char* clipboard = getSystem()->getClipboardText();

        if(clipboard)
        {
            s32 size = strlen(clipboard)/2;

            enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS, HeaderSize = sizeof(ClipboardHeader)};

            if(size > HeaderSize)
            {
                u8* data = malloc(size);

                tic_tool_str2buf(clipboard, strlen(clipboard), data, true);

                ClipboardHeader header = {0};

                memcpy(&header, data, HeaderSize);

                if(size == header.size * RowSize + HeaderSize 
                    && size == sizeof(tic_track_pattern) + HeaderSize)
                {
                    memcpy(pattern->rows, data + HeaderSize, header.size * RowSize);
                    history_add(music->history);
                }

                free(data);
            }

            getSystem()->freeClipboardText(clipboard);
        }
    }
}

static void copyTrackerToClipboard(Music* music, bool cut)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_rect rect = music->tracker.select.rect;

        if(rect.h <= 0)
        {
            rect.y = music->tracker.edit.y;
            rect.h = 1;
        }

        ClipboardHeader header = {rect.h};

        enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS, HeaderSize = sizeof(ClipboardHeader)};

        s32 size = rect.h * RowSize + HeaderSize;
        u8* data = malloc(size);

        if(data)
        {
            memcpy(data, &header, HeaderSize);
            memcpy(data + HeaderSize, &pattern->rows[rect.y], RowSize * rect.h);

            free(data);

            if(cut)
            {
                deleteSelection(music);
                history_add(music->history);
            }

            resetSelection(music);
        }       
    }
}

static void copyTrackerFromClipboard(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern && getSystem()->hasClipboardText())
    {
        char* clipboard = getSystem()->getClipboardText();

        if(clipboard)
        {
            s32 size = strlen(clipboard)/2;

            enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS, HeaderSize = sizeof(ClipboardHeader)};

            if(size > HeaderSize)
            {
                u8* data = malloc(size);

                tic_tool_str2buf(clipboard, strlen(clipboard), data, true);

                ClipboardHeader header = {0};

                memcpy(&header, data, HeaderSize);

                if(header.size * RowSize == size - HeaderSize)
                {
                    if(header.size + music->tracker.edit.y > MUSIC_PATTERN_ROWS)
                        header.size = MUSIC_PATTERN_ROWS - music->tracker.edit.y;

                    memcpy(&pattern->rows[music->tracker.edit.y], data + HeaderSize, header.size * RowSize);
                    history_add(music->history);
                }

                free(data);
            }

            getSystem()->freeClipboardText(clipboard);
        }
    }
}

static void copyToClipboard(Music* music, bool cut)
{
    switch (music->tab)
    {
    case MUSIC_TRACKER_TAB: copyTrackerToClipboard(music, cut); break;
    case MUSIC_PIANO_TAB: copyPianoToClipboard(music, cut); break;
    }
}

static void copyFromClipboard(Music* music)
{
    switch (music->tab)
    {
    case MUSIC_TRACKER_TAB: copyTrackerFromClipboard(music); break;
    case MUSIC_PIANO_TAB: copyPianoFromClipboard(music); break;
    }
}

static void setChannelPatternValue(Music* music, s32 patternId, s32 frame, s32 channel)
{
    tic_track* track = getTrack(music);

    u32 patternData = 0;
    for(s32 b = 0; b < TRACK_PATTERNS_SIZE; b++)
        patternData |= track->data[frame * TRACK_PATTERNS_SIZE + b] << (BITS_IN_BYTE * b);

    s32 shift = channel * TRACK_PATTERN_BITS;

    if(patternId < 0) patternId = MUSIC_PATTERNS;
    if(patternId > MUSIC_PATTERNS) patternId = 0;

    patternData &= ~(TRACK_PATTERN_MASK << shift);
    patternData |= patternId << shift;

    for(s32 b = 0; b < TRACK_PATTERNS_SIZE; b++)
        track->data[frame * TRACK_PATTERNS_SIZE + b] = (patternData >> (b * BITS_IN_BYTE)) & 0xff;

    history_add(music->history);
}

static void prevPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if (channel > 0)
    {
        music->tracker.edit.x = (channel-1) * CHANNEL_COLS;
        music->tracker.col = 1;
    }
}

static void nextPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if (channel < TIC_SOUND_CHANNELS-1)
    {
        music->tracker.edit.x = (channel+1) * CHANNEL_COLS;
        music->tracker.col = 0;
    }
}

static void colLeft(Music* music)
{
    if(music->tracker.col > 0)
        music->tracker.col--;
    else prevPattern(music);
}

static void colRight(Music* music)
{
    if(music->tracker.col < 1)
        music->tracker.col++;
    else nextPattern(music);
}

static void checkSelection(Music* music)
{
    if(music->tracker.select.start.x < 0 || music->tracker.select.start.y < 0)
    {
        music->tracker.select.start.x = music->tracker.edit.x;
        music->tracker.select.start.y = music->tracker.edit.y;
    }
}

static void updateSelection(Music* music)
{
    s32 rl = MIN(music->tracker.edit.x, music->tracker.select.start.x);
    s32 rt = MIN(music->tracker.edit.y, music->tracker.select.start.y);
    s32 rr = MAX(music->tracker.edit.x, music->tracker.select.start.x);
    s32 rb = MAX(music->tracker.edit.y, music->tracker.select.start.y);

    tic_rect* rect = &music->tracker.select.rect;
    *rect = (tic_rect){rl, rt, rr - rl + 1, rb - rt + 1};

    if(rect->x % CHANNEL_COLS + rect->w > CHANNEL_COLS)
        resetSelection(music);
}

static s32 setDigit(s32 pos, s32 val, s32 digit)
{
    enum {Base = 10};

    s32 div = 1;
    while(pos--) div *= Base;

    return val - (val / div % Base - digit) * div;
}

static s32 sym2dec(char sym)
{
    s32 val = -1;
    if (sym >= '0' && sym <= '9') val = sym - '0';
    return val;
}

static s32 sym2hex(char sym)
{
    s32 val = sym2dec(sym);
    if (sym >= 'a' && sym <= 'f') val = sym - 'a' + 10;

    return val;
}

static void processTrackerKeyboard(Music* music)
{
    tic_mem* tic = music->tic;

    if(tic->ram.input.keyboard.data == 0)
        return;

    if(tic_api_key(tic, tic_key_ctrl) || tic_api_key(tic, tic_key_alt))
        return;

    bool shift = tic_api_key(tic, tic_key_shift);

    if(shift)
    {
        if(keyWasPressed(tic_key_up)
            || keyWasPressed(tic_key_down)
            || keyWasPressed(tic_key_left)
            || keyWasPressed(tic_key_right)
            || keyWasPressed(tic_key_home)
            || keyWasPressed(tic_key_end)
            || keyWasPressed(tic_key_pageup)
            || keyWasPressed(tic_key_pagedown)
            || keyWasPressed(tic_key_tab))
        {
            checkSelection(music);
        }
    }

    if(keyWasPressed(tic_key_up))               upRow(music);
    else if(keyWasPressed(tic_key_delete))      
    {
        deleteSelection(music);
        history_add(music->history);
        downRow(music);
    }
    else if(keyWasPressed(tic_key_space)) 
    {
        const tic_track_pattern* pattern = getChannelPattern(music);
        if(pattern)
        {
            const tic_track_row* row = &pattern->rows[music->tracker.edit.y];
            playNote(music, row);            
        }
    }

    if(shift)
    {
        if(keyWasPressed(tic_key_up)
            || keyWasPressed(tic_key_down)
            || keyWasPressed(tic_key_left)
            || keyWasPressed(tic_key_right)
            || keyWasPressed(tic_key_home)
            || keyWasPressed(tic_key_end)
            || keyWasPressed(tic_key_pageup)
            || keyWasPressed(tic_key_pagedown)
            || keyWasPressed(tic_key_tab))
        {
            updateSelection(music);
        }
    }
    else resetSelection(music);

    static const tic_keycode Piano[] =
    {
        tic_key_z,
        tic_key_s,
        tic_key_x,
        tic_key_d,
        tic_key_c,
        tic_key_v,
        tic_key_g,
        tic_key_b,
        tic_key_h,
        tic_key_n,
        tic_key_j,
        tic_key_m,

        // octave +1
        tic_key_q,
        tic_key_2,
        tic_key_w,
        tic_key_3,
        tic_key_e,
        tic_key_r,
        tic_key_5,
        tic_key_t,
        tic_key_6,
        tic_key_y,
        tic_key_7,
        tic_key_u,
        
        // extra keys
        tic_key_i,
        tic_key_9,
        tic_key_o,
        tic_key_0,
        tic_key_p,
    };

    if (getChannelPattern(music))
    {
        s32 col = music->tracker.edit.x % CHANNEL_COLS;

        switch (col)
        {
        case ColumnNote:
        case ColumnSemitone:
            if (keyWasPressed(tic_key_1) || keyWasPressed(tic_key_a))
            {
                setStopNote(music);
                downRow(music);
            }
            else
            {
                tic_track_pattern* pattern = getChannelPattern(music);

                for (s32 i = 0; i < COUNT_OF(Piano); i++)
                {
                    if (keyWasPressed(Piano[i]))
                    {
                        s32 note = i % NOTES;
                        s32 octave = i / NOTES + music->last.octave;
                        s32 sfx = music->last.sfx;
                        setNote(music, note, octave, sfx);

                        downRow(music);

                        break;
                    }               
                }
            }
            break;
        case ColumnOctave:
            if(getNote(music) >= 0)
            {
                s32 octave = -1;

                char sym = getKeyboardText();

                if(sym >= '1' && sym <= '8') octave = sym - '1';

                if(octave >= 0)
                {
                    setOctave(music, octave);
                    downRow(music);
                }
            }
            break;
        case ColumnSfxHi:
        case ColumnSfxLow:
            if(getNote(music) >= 0)
            {
                s32 val = sym2dec(getKeyboardText());
                            
                if(val >= 0)
                {
                    s32 sfx = setDigit(col == 3 ? 1 : 0, getSfx(music), val);

                    setSfx(music, sfx);

                    if(col == 3) rightCol(music);
                    else downRow(music), leftCol(music);
                }
            }
            break;
        case ColumnCommand:
            {
                char sym = getKeyboardText();

                if(sym)
                {
                    const char* val = strchr(MusicCommands, toupper(sym));
                                
                    if(val)
                        setCommand(music, val - MusicCommands);
                }
            }
            break;
        case ColumnParameter1:
        case ColumnParameter2:
            {
                s32 val = sym2hex(getKeyboardText());

                if(val >= 0)
                {
                    col == ColumnParameter1
                        ? setParam1(music, val)
                        : setParam2(music, val);
                }
            }
            break;          
        }

        history_add(music->history);
    }
}

static void processPatternKeyboard(Music* music)
{
    tic_mem* tic = music->tic;
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if(tic_api_key(tic, tic_key_ctrl) || tic_api_key(tic, tic_key_alt))
        return;

    if(keyWasPressed(tic_key_delete))       setChannelPatternValue(music, 0, music->frame, channel);
    else if(keyWasPressed(tic_key_tab))     nextPattern(music);
    else if(keyWasPressed(tic_key_down) 
        || keyWasPressed(tic_key_return)) 
        music->tracker.edit.y = music->scroll.pos;
    else
    {
        s32 val = sym2dec(getKeyboardText());

        if(val >= 0)
        {
            s32 pattern = setDigit(1 - music->tracker.col & 1, tic_tool_get_pattern_id(getTrack(music), 
                music->frame, channel), val);

            if(pattern <= MUSIC_PATTERNS)
            {
                setChannelPatternValue(music, pattern, music->frame, channel);

                if(music->tracker.col == 0)
                    colRight(music);                     
            }
        }
    }
}

static void updatePianoEditPos(Music* music)
{
    music->piano.edit.x = CLAMP(music->piano.edit.x, 0, PianoColumnsCount * 2 - 1);

    switch(music->piano.edit.x / 2)
    {
    case PianoSfxColumn:
    case PianoXYColumn:
        if(music->piano.edit.y < 0)
            music->scroll.pos += music->piano.edit.y;

        if(music->piano.edit.y > TRACKER_ROWS-1)
            music->scroll.pos += music->piano.edit.y - (TRACKER_ROWS - 1);

        updateScroll(music);
        break;
    }

    music->piano.edit.y = CLAMP(music->piano.edit.y, 0, MUSIC_FRAMES-1);
}

static void updatePianoEditCol(Music* music)
{
    if(music->piano.edit.x & 1)
    {
        music->piano.edit.x--;
        music->piano.edit.y++;
    }
    else music->piano.edit.x++;

    updatePianoEditPos(music);
}

static inline s32 rowIndex(Music* music, s32 row)
{
    return row + music->scroll.pos;
}

static tic_track_row* getPianoRow(Music* music)
{
    tic_track_pattern* pattern = getFramePattern(music, music->piano.col, music->frame);
    return pattern ? &pattern->rows[rowIndex(music, music->piano.edit.y)] : NULL;
}

static void processPianoKeyboard(Music* music)
{
    tic_mem* tic = music->tic;

    if(keyWasPressed(tic_key_up)) music->piano.edit.y--;
    else if(keyWasPressed(tic_key_down)) music->piano.edit.y++;
    else if(keyWasPressed(tic_key_left)) music->piano.edit.x--;
    else if(keyWasPressed(tic_key_right)) music->piano.edit.x++;
    else if(keyWasPressed(tic_key_home)) music->piano.edit.x = PianoChannel1Column;
    else if(keyWasPressed(tic_key_end)) music->piano.edit.x = PianoColumnsCount*2+1;
    else if(keyWasPressed(tic_key_pageup)) music->piano.edit.y -= TRACKER_ROWS;
    else if(keyWasPressed(tic_key_pagedown)) music->piano.edit.y += TRACKER_ROWS;

    updatePianoEditPos(music);

    if(keyWasPressed(tic_key_delete))
    {
        s32 col = music->piano.edit.x / 2;
        switch(col)
        {
        case PianoChannel1Column:
        case PianoChannel2Column:
        case PianoChannel3Column:
        case PianoChannel4Column:
            setChannelPatternValue(music, 00, music->piano.edit.y, col);
            break;
        case PianoSfxColumn:
            {
                tic_track_row* row = getPianoRow(music);
                if(row)
                {
                    tic_tool_set_track_row_sfx(row, 0);
                    history_add(music->history);
                }
            }
            break;
        case PianoXYColumn:
            {
                tic_track_row* row = getPianoRow(music);
                if(row)
                {
                    row->param1 = row->param2 = 0;
                    history_add(music->history);
                }
            }
            break;
        }
    }

    if(getKeyboardText())
    {
        s32 col = music->piano.edit.x / 2;
        s32 dec = sym2dec(getKeyboardText());
        s32 hex = sym2hex(getKeyboardText());
        tic_track_row* row = getPianoRow(music);

        switch(col)
        {
        case PianoChannel1Column:
        case PianoChannel2Column:
        case PianoChannel3Column:
        case PianoChannel4Column:
            if(dec >= 0)
            {
                s32 pattern = setDigit(1 - music->piano.edit.x & 1, 
                    tic_tool_get_pattern_id(getTrack(music), music->piano.edit.y, col), dec);

                if(pattern <= MUSIC_PATTERNS)
                {
                    setChannelPatternValue(music, pattern, music->piano.edit.y, col);
                    updatePianoEditCol(music);
                }
            }
            break;
        case PianoSfxColumn:
            if(row && row->note >= NoteStart && dec >= 0)
            {
                s32 sfx = setDigit(1 - music->piano.edit.x & 1, tic_tool_get_track_row_sfx(row), dec);
                tic_tool_set_track_row_sfx(row, sfx);
                history_add(music->history);

                music->last.sfx = tic_tool_get_track_row_sfx(row);

                updatePianoEditCol(music);
                playNote(music, row);
            }
            break;

        case PianoXYColumn:
            if(row && row->command > tic_music_cmd_empty && hex >= 0)
            {
                if(music->piano.edit.x & 1)
                    row->param2 = hex;
                else row->param1 = hex;

                history_add(music->history);

                updatePianoEditCol(music);
            }
            break;
        }
    }
}

static void selectAll(Music* music)
{
    resetSelection(music);

    s32 col = music->tracker.edit.x - music->tracker.edit.x % CHANNEL_COLS;

    music->tracker.select.start = (tic_point){col, 0};
    music->tracker.edit.x = col + CHANNEL_COLS-1;
    music->tracker.edit.y = MUSIC_PATTERN_ROWS-1;

    updateSelection(music);
}

static void processKeyboard(Music* music)
{
    tic_mem* tic = music->tic;

    switch(getClipboardEvent())
    {
    case TIC_CLIPBOARD_CUT: break;
    case TIC_CLIPBOARD_COPY: break;
    case TIC_CLIPBOARD_PASTE: break;
    default: break;
    }

    bool ctrl = tic_api_key(tic, tic_key_ctrl);
    bool shift = tic_api_key(tic, tic_key_shift);

    if (ctrl)
    {
        if(keyWasPressed(tic_key_a))            selectAll(music);
    }
    else
    {
        if(keyWasPressed(tic_key_return))
        {
            const tic_sound_state* pos = getMusicPos(music);
            pos->music.track < 0
                ? (shift && music->tab == MUSIC_TRACKER_TAB
                    ? playFrameRow(music) 
                    : playFrame(music))
                : stopTrack(music);
        }

        switch (music->tab)
        {
        case MUSIC_TRACKER_TAB:
            music->tracker.edit.y >= 0 
                ? processTrackerKeyboard(music)
                : processPatternKeyboard(music);
            break;
        case MUSIC_PIANO_TAB:
            processPianoKeyboard(music);
            break;
        }
    }
}

static void setIndex(Music* music, s32 delta, void* data)
{
    music->track += delta;
}

static void setTempo(Music* music, s32 delta, void* data)
{
    enum
    {
        Step = 10,
        Min = 40-DEFAULT_TEMPO,
        Max = 250-DEFAULT_TEMPO,
    };

    tic_track* track = getTrack(music);

    s32 tempo = track->tempo;
    tempo += delta * Step;

    if (tempo > Max) tempo = Max;
    if (tempo < Min) tempo = Min;

    track->tempo = tempo;

    history_add(music->history);
}

static void setSpeed(Music* music, s32 delta, void* data)
{
    enum
    {
        Step = 1,
        Min = 1-DEFAULT_SPEED,
        Max = 31-DEFAULT_SPEED,
    };

    tic_track* track = getTrack(music);

    s32 speed = track->speed;
    speed += delta * Step;

    if (speed > Max) speed = Max;
    if (speed < Min) speed = Min;

    track->speed = speed;

    history_add(music->history);
}

static void setRows(Music* music, s32 delta, void* data)
{
    enum
    {
        Step = 1,
        Min = 0,
        Max = MUSIC_PATTERN_ROWS - TRACKER_ROWS,
    };

    tic_track* track = getTrack(music);
    s32 rows = track->rows;
    rows -= delta * Step;

    if (rows < Min) rows = Min;
    if (rows > Max) rows = Max;

    track->rows = rows;

    updateTracker(music);

    history_add(music->history);
}

static void setChannelPattern(Music* music, s32 delta, s32 channel)
{
    tic_track* track = getTrack(music);
    s32 frame = music->frame;

    u32 patternData = 0;
    for(s32 b = 0; b < TRACK_PATTERNS_SIZE; b++)
        patternData |= track->data[frame * TRACK_PATTERNS_SIZE + b] << (BITS_IN_BYTE * b);

    s32 shift = channel * TRACK_PATTERN_BITS;
    s32 patternId = (patternData >> shift) & TRACK_PATTERN_MASK;

    setChannelPatternValue(music, patternId + delta, music->frame, channel);
}

static inline bool noteBeat(Music* music, s32 row)
{
    return row % (music->beat34 ? 3 : 4) == 0;
}

static const char* getPatternLabel(Music* music, s32 frame, s32 channel)
{
    static char index[sizeof "--"];

    strcpy(index, "--");

    s32 pattern = tic_tool_get_pattern_id(getTrack(music), frame, channel);

    if(pattern)
        sprintf(index, "%02i", pattern);

    return index;
}


static void tick(Music* music)
{
    tic_mem* tic = music->tic;

    // process scroll
    {
        tic80_input* input = &tic->ram.input;

        if(input->mouse.scrolly)
        {
            if(tic_api_key(tic, tic_key_ctrl))
            {
            }
            else
            {       
                enum{Scroll = NOTES_PER_BEAT};
                s32 delta = input->mouse.scrolly > 0 ? -Scroll : Scroll;

                music->scroll.pos += delta;

                updateScroll(music);
            }
        }
    }

    processKeyboard(music);

    if(music->follow)
    {
        const tic_sound_state* pos = getMusicPos(music);

        if(pos->music.track == music->track && 
            music->tracker.edit.y >= 0 &&
            pos->music.row >= 0)
        {
            music->frame = pos->music.frame;
            music->tracker.edit.y = pos->music.row;
            updateTracker(music);
        }
    }

    for (s32 i = 0; i < TIC_SOUND_CHANNELS; i++)
        if(!music->on[i])
            tic->ram.registers[i].volume = 0;

    tic_api_cls(music->tic, tic_color_14);

    switch (music->tab)
    {
    case MUSIC_TRACKER_TAB: break;
    case MUSIC_PIANO_TAB: break;
    }

    music->tickCounter++;
}

static void onStudioEvent(Music* music, StudioEvent event)
{
    switch (event)
    {
    case TIC_TOOLBAR_CUT: break;
    case TIC_TOOLBAR_COPY: break;
    case TIC_TOOLBAR_PASTE: break;
    case TIC_TOOLBAR_UNDO: break;
    case TIC_TOOLBAR_REDO: break;
    default: break;
    }
}

void initMusic(Music* music, tic_mem* tic, tic_music* src)
{
    if (music->history) history_delete(music->history);

    *music = (Music)
    {
        .tic = tic,
        .tick = tick,
        .src = src,
        .track = 0,
        .beat34 = false,
        .frame = 0,
        .follow = true,
        .sustain = false,
        .scroll = 
        {
            .pos = 0,
            .start = 0,
            .active = false,
        },
        .last =
        {
            .octave = 3,
            .sfx = 0,
        },
        .on = {true, true, true, true},
        .tracker =
        {
            .edit = {0, 0},

            .select = 
            {
                .start = {0, 0},
                .rect = {0, 0, 0, 0},
                .drag = false,
            },
        },

        .piano =
        {
            .col = 0,
            .edit = {0, 0},
            .note = {-1, -1, -1, -1},
        },

        .tickCounter = 0,
        .tab = MUSIC_PIANO_TAB,
        .history = history_create(src, sizeof(tic_music)),
        .event = onStudioEvent,
    };

    resetSelection(music);
}

void freeMusic(Music* music)
{
    history_delete(music->history);
    free(music);
}
