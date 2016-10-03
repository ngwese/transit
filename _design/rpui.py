#!/usr/bin/env python3

# copy/paste seq in play mode using shift button
# clear seq in play mode using shift+meta button
# copy/paste stage in trck edit mode using shift button
# clean stage in track edit mode using shift+meta button
# whatever sequence is playing should be selected on entering edit mode

import asyncio
import monome

def clip(value, low, high):
    if value < low:
        return low
    if value > high:
        return high
    return value

DEFAULT_DURATION = 16

class Stage(object):
    def __init__(self, position, duration, width=50, end_of_stage=False):
        self.position = position
        self.duration = duration # in 64ths
        self.width = width       # percentage of duration
        self.eos = end_of_stage  # trigger on eos

    def set_duration(self, d):
        self.duration = clip(d, 1, 63)
        print("{} duration: {}".format(self, self.duration))

    def get_duration_remainder(self, div):
        return self.duration % div
    
    def nudge_duration(self, nudge):
        self.set_duration(self.duration + nudge)

    def set_width(self, w):
        self.width = clip(w, 1, 63) # wrong; percentage?
        print("{} width: {}".format(self, self.duration))

    def get_width_remainder(self, div):
        return self.width % div

    def nudge_width(self, nudge):
        self.set_width(self.width + nudge)
        
    def __repr__(self):
        return "stage({}, {})".format(self.position, self.duration)

    
class Sequence(object):
    def __init__(self):
        self._stages = [None for _ in range(64)]

    def __getitem__(self, i):
        return self._stages[i]
    
    def get_stage(self, i):
        s = self._stages[i]
        if s is None:
            s = Stage(i, DEFAULT_DURATION)
            self._stages[i] = s
        return s

    
class Track(object):
    def __init__(self, i):
        self.index = i
        self.sequence = None
        self.stage = 0

        self._sequences = [None for _ in range(16)]

    def __getitem__(self, i):
        return self._sequences[i]

    def get_sequence(self, n):
        seq = self._sequences[n]
        if seq is None:
            seq = Sequence()
            self._sequences[n] = seq
        return seq

    def __repr__(self):
        return "track({})".format(self.index)

L1 = 3
L2 = 6
L3 = 9

PLAY = 'play'
TRACK_EDIT = 'track'
CLIP_EDIT = 'clip'

CONTROL_ROW = 7
EDIT_1_ROW = 6
EDIT_2_ROW = 5
PLAY_BAR_ROW = 4
TRACK_KEYS = [0, 1, 2, 3]
META_KEY = 15

DURATION_COL = 0
WIDTH_COL = 8

class Transit(monome.Monome):
    def __init__(self):
        super().__init__('/monome')
        self._focus = None
        self._tracks = [Track(i) for i in range(4)]
        self._queue_column = None
        self._seq_selected = None
        self._stage_selected = None
        
        self._phase = 0


    def grid_key(self, x, y, s):
        #print("key:", x, y, s)

        if x == CONTROL_ROW:
            if y in TRACK_KEYS:
                self.do_track_select(y, s)
            elif y == META_KEY:
                self.do_meta(s)
        elif x == EDIT_1_ROW:
            self.do_edit_1(y, s)
        elif x == EDIT_2_ROW:
            self.do_edit_2(y, s)
        elif x == PLAY_BAR_ROW:
            self.do_play_bar(y, s)
        else:
            self.do_lower(x, y, s)
        
        self.draw(immediate=True)

    def do_track_select(self, column, s):
        idx = TRACK_KEYS.index(column)
        if not idx < 0:
            if s == 1:
                # focus on the given track and its clip
                chosen = self._tracks[idx]
                if self.meta or self.mode == TRACK_EDIT:
                    self._focus = chosen
                    print("track edit:", chosen)
                    self.mode = TRACK_EDIT
                elif self._focus == chosen:
                    self._focus = None    # toggel off
                    print("focus off");
                else:
                    self._focus = chosen
                    print("focus:", idx)

    def do_meta(self, s):
        self.meta = True if s == 1 else False
        if self.mode != PLAY and self.meta:
            print("return to play")
            self.mode = PLAY
            self._seq_selected = None
            self._stage_selected = None

    def do_edit_1(self, column, s):
        if s == 0:
            return

        if self.mode == TRACK_EDIT and self._stage_selected is not None:
            if column < WIDTH_COL:
                # do duration
                self._stage_selected.set_duration((column + 1) * 8)
            else:
                self._stage_selected.set_width((column - WIDTH_COL + 1) * 8)
            

    def do_edit_2(self, column, s):
        if s == 0:
            return

        if self.mode == TRACK_EDIT and self._stage_selected is not None:
            if column < WIDTH_COL:
                # do duration
                high = self._stage_selected.duration // 8
                self._stage_selected.set_duration((high * 8) + column + 1)
            else:
                high = self._stage_selected.width // 8
                self._stage_selected.set_width((high * 8) + column - WIDTH_COL + 1)

    def do_play_bar(self, column, s):
        #print("play:", column, s)
        if self.mode == PLAY:
            if s == 1:
                if self._queue_column == column:
                    self._queue_column = None
                else:
                    print("que:", column)
                    self._queue_column = column
        elif self.mode == TRACK_EDIT:
            if s == 1:
                self._seq_selected = self._focus.get_sequence(column)
                print(self._seq_selected)


    def do_lower(self, row, column, s):
        if self.mode == PLAY and s == 1:
            track = self._tracks[3 - row]
            seq = track[column]
            if seq:
                print("queue:", column, track)
            else:
                print("stop:", track)
        elif self.mode == TRACK_EDIT and s == 1:
            #print("lower:", row, column, s)
            seq = self._seq_selected
            if seq:
                stage_num = self._grid_to_stage(row, column)
                self._stage_selected = seq.get_stage(stage_num)
                print(self._stage_selected)
        
            
    def ready(self):
        self.mode = PLAY
        self.meta = False
        
        asyncio.async(self.play())

    @asyncio.coroutine
    def play(self):
        while True:
            self.draw()
            yield from asyncio.sleep(0.1)

    def trigger(self, i):
        print("triggered", i)
        
    def draw(self, immediate=False):
        if not immediate:
            self._phase ^= 1
        
        b = self._new_buffer()

        # meta hint
        if self.mode != PLAY:
            b.led_level_set(CONTROL_ROW, META_KEY, L2)
        
        # track select
        for column in range(4):
            if self._focus and self._focus.index == column:
                highlight = 5
            else:
                highlight = 0
            b.led_level_set(CONTROL_ROW, column, L2 + highlight)

        # edit param
        if self.mode == TRACK_EDIT:
            if self._stage_selected is not None:
                duration_high, duration_low = self._duration_to_grid(self._stage_selected)
                for column in range(duration_high):
                    b.led_level_set(EDIT_1_ROW, DURATION_COL + column, L2)
                for column in range(duration_low):
                    b.led_level_set(EDIT_2_ROW, DURATION_COL + column, L2)
                # accentuate beginning
                b.led_level_set(EDIT_1_ROW, DURATION_COL, L3 + 3)

                width_high, width_low = self._width_to_grid(self._stage_selected)
                for column in range(width_high):
                    b.led_level_set(EDIT_1_ROW, WIDTH_COL + column, L2)
                for column in range(width_low):
                    b.led_level_set(EDIT_2_ROW, WIDTH_COL + column, L2)
                # accentuate beginning
                b.led_level_set(EDIT_1_ROW, WIDTH_COL, L3 + 3)

        
        # play bar
        for column in range(self.height):
            hilight = 0
            if self.mode == PLAY:
                base = L2
                if self._queue_column == column and self._phase:
                    hilight = 5
            elif self.mode == TRACK_EDIT:
                base = L1
                seq = self._focus[column]
                if seq:
                    base = L2
                if self._seq_selected is not None and self._seq_selected == seq:
                    base = L2
                    hilight = 5

            b.led_level_set(PLAY_BAR_ROW, column, base + hilight)

        # lower
        if self.mode == PLAY:
            for t in range(4):
                track = self._tracks[t]
                for s in range(16):
                    seq = track[s]
                    if seq:
                        b.led_level_set(3 - t, s, L2 - 2)
        elif self.mode == TRACK_EDIT:
            if self._stage_selected:
                row, column = self._stage_to_grid(self._stage_selected.position)
                b.led_level_set(row, column, L3)


        b.render(self)

    def _stage_to_grid(self, stage_position):
        row = stage_position >> 4
        column = stage_position - (row * 16)
        return (3 - row), column

    def _grid_to_stage(self, row, column):
        return ((3 - row) * 16) + column

    def _duration_to_grid(self, stage):
        hi = stage.duration >> 3
        low = stage.duration - (hi * 8)
        return hi, low

    def _width_to_grid(self, stage):
        hi = stage.width >> 3
        low = stage.width - (hi * 8)
        return hi, low
        
    def _new_buffer(self):
        return monome.LedBuffer(self.width, self.height)

    

if __name__ == '__main__':
    loop = asyncio.get_event_loop()
    asyncio.async(monome.create_serialosc_connection(Transit))
    loop.run_forever()
