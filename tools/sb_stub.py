"""A Sound Blaster for WALK.SL in tools/dosemu.py (docs/findings.md, "WALK.SL step 25"): its voice driver (segment 0x190e, a wrapper of
CT-VOICE's functions) is stubbed so that the card is found, and each sound the game starts, cuts or waits for is logged by pass.

The card's busy word (0x24e in the data segment, CT-VOICE's status word: not 0 while a sound plays) follows a model the port's
walk_dump uses too, since the oracle's passes have no real time: a sound started in pass P plays through pass P + D - 1, D being its
length at the port's 35 passes a second (sfx_passes in src/sfx.c), and is over at the top of pass P + D. A stop ends it, and so does a
wait for it (the three loops at 0x1609e, 0x1649b and 0x18adb that wait until the word is 0, which would never end in pass time).

Events: (pass, 'play', FILE, OFFSET) with FILE 'SND_EF.1' or '_1_.LOC' and OFFSET where the VOC sound-data block starts in it;
(pass, 'cut') for a stop while a sound plays (not the one 0x1c68e makes before it plays); (pass, 'wait') for a wait while one plays.
The pass is the one the port's walk_dump counts (emu.pass_count - 1); -1 before the first loop top.
"""
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_IP, UC_X86_REG_SP,
                               UC_X86_REG_SS)

DRV = 0x290E0
DETECT, SPEAKER, STOP, TERMINATE, PLAY = 0x11, 0x49, 0x6D, 0x76, 0x9C
WAITS = (0x1609E, 0x1649B, 0x18ADB)
# every other read of the busy word (cmp word ptr [0x24e], 0): the loops' clean-ups after a sound, the kill's (0x12425) and 0x24215
READS = (0x11D49, 0x12425, 0x160C3, 0x164C0, 0x18D36, 0x19179, 0x199FA, 0x19AE1, 0x1A654, 0x1AA43, 0x1B519, 0x1B847, 0x1C07C,
         0x1CA95, 0x1D42B, 0x207C5, 0x217B9, 0x2257D, 0x23400, 0x24215, 0x24DE0, 0x250D7, 0x258C0)
PLAY_STOP = 0x1C6BD   # the return address of 0x1c68e's own stop before it plays
STATUS = 0x24E
PASS_HZ = 35          # TK_WALK_PASS_HZ


def block_passes(block):
    """D, the passes a VOC sound-data block (type 1) lasts: its samples (voc_block_frames in src/voc.c) at its rate, at 35 passes a
    second, rounded up; at least 1."""
    n = block[1] | block[2] << 8 | block[3] << 16
    tc, pack = block[4], block[5]
    data = max(n - 2, 0)
    samples = data if pack == 0 else (pack + 1) * max(data - 1, 0)   # after an ADPCM block's starting level
    return max(1, (samples * PASS_HZ * (256 - tc) + 999_999) // 1_000_000)


class SoundBlaster:
    def __init__(self, emu, data_dir):
        self.emu = emu
        self.files = {name: Path(data_dir, name).read_bytes() for name in ('SND_EF.1', '_1_.LOC', 'RPG.LCP')}
        self.events = []
        self.callers = []
        self.until = None     # the pass at whose top the sound playing is over; None when none plays
        self.ds = None
        for off in (DETECT, SPEAKER, STOP, TERMINATE, PLAY):
            emu.uc.hook_add(UC_HOOK_CODE, self.drv, None, DRV + off, DRV + off)
        for a in WAITS:
            emu.uc.hook_add(UC_HOOK_CODE, self.wait, None, a, a)
        for a in READS:
            emu.uc.hook_add(UC_HOOK_CODE, self.read, None, a, a)

    def pass_now(self):
        return self.emu.pass_count - 1

    def set_status(self, uc, v):
        if self.ds is not None:
            uc.mem_write(self.ds * 16 + STATUS, struct.pack('<H', v))

    def busy(self):
        return self.until is not None and self.pass_now() < self.until

    def end(self, uc):
        self.until = None
        self.set_status(uc, 0)

    def where(self, lin):
        uc = self.emu.uc
        head = bytes(uc.mem_read(lin, 6))
        n = head[1] | head[2] << 8 | head[3] << 16
        block = head + bytes(uc.mem_read(lin + 6, min(n - 2, 64)))
        hits = [(name, i) for name, d in self.files.items() for i in [d.find(block)] if i >= 0]
        if len(hits) != 1:
            return ('?', lin), head
        return hits[0], head

    def read(self, uc, address, size, user):
        if self.until is not None and self.pass_now() >= self.until:
            self.end(uc)

    def wait(self, uc, address, size, user):
        if self.busy():
            self.events.append((self.pass_now(), 'wait'))
        self.end(uc)

    def drv(self, uc, address, size, user):
        emu = self.emu
        off = address - DRV
        ss, sp = emu.r(UC_X86_REG_SS), emu.r(UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH', bytes(uc.mem_read(ss * 16 + sp, 4)))
        if self.ds is None:
            self.ds = emu.r(UC_X86_REG_DS)
        if off == PLAY:
            o, s = struct.unpack('<HH', bytes(uc.mem_read(ss * 16 + sp + 4, 4)))
            (name, at), head = self.where(s * 16 + o)
            self.events.append((self.pass_now(), 'play', name, at))
            bp = emu.r(UC_X86_REG_BP)
            rip, rcs = struct.unpack('<HH', bytes(uc.mem_read(ss * 16 + ((bp + 2) & 0xFFFF), 4)))
            self.callers.append(rcs * 16 + rip - 5)   # the call of 0x1c68e (for reading the log; not compared)
            self.until = self.pass_now() + block_passes(head)
            self.set_status(uc, 0xFFFF)
        elif off == STOP:
            if self.busy() and cs * 16 + ip != PLAY_STOP:
                self.events.append((self.pass_now(), 'cut'))
                self.callers.append(cs * 16 + ip - 5)
            self.end(uc)
        # every function returns 0 (the card is found, the calls succeed)
        uc.reg_write(UC_X86_REG_AX, 0)
        uc.reg_write(UC_X86_REG_SP, (sp + 4) & 0xFFFF)
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_IP, ip)
