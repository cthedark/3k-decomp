#!/usr/bin/env python3
"""Minimal DOS-program emulator used as a reference ("oracle") for the C port.

Runs an unpacked 16-bit DOS MZ program with the Unicorn CPU emulator and answers the
few DOS, BIOS and hardware requests the game makes. It is NOT a general DOS emulator:
anything it does not understand is logged, and the run continues with a neutral answer.

What it provides
  - MZ loading with relocations (load segment 0x1000, like the Ghidra listing), a PSP,
    an environment block, and the C runtime start-up expectations.
  - INT 21h: version, vectors, memory blocks, files (case-insensitive lookup in a COPY of
    the data directory), exit, exec (stops the run and reports the program name).
  - INT 10h mode set, INT 33h (reports "no mouse"), VGA DAC writes, keyboard port 0x60,
    BIOS tick counter at 0040:006C, retrace status at port 0x3DA.
  - Snapshots: the 320x200 screen at A000:0000 plus the DAC palette, saved as PPM and raw.

Data safety: the program may write to data files (MARK rewrites ANI.LCP). Files are served
from a private copy, never from original_bin/.

Usage
  work/venv/bin/python tools/dosemu.py PROGRAM.exe [--data DIR] [--ticks N]
        [--snap TICK:FILE.ppm ...] [--key TICK:SCANCODE[:release_after] ...]
        [--instr-per-tick N] [--trace-int]
Requires: unicorn (pip install unicorn==2.1.2; building it needs cmake and pkg-config).
"""
import argparse
import os
import shutil
import struct
import sys
from pathlib import Path

from unicorn import (UC_ARCH_X86, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16, Uc, UcError)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                               UC_X86_REG_CS, UC_X86_REG_CX, UC_X86_REG_DS, UC_X86_REG_DX,
                               UC_X86_REG_EFLAGS, UC_X86_REG_ES, UC_X86_REG_IP, UC_X86_REG_SI,
                               UC_X86_REG_SP, UC_X86_REG_SS, UC_X86_REG_DI, UC_X86_REG_BP)

ROOT = Path(__file__).resolve().parent.parent
LOAD_SEG = 0x1000
PSP_SEG = LOAD_SEG - 0x10
ENV_SEG = 0x0800
HEAP_START = 0x2000
HEAP_END = 0x9F00   # conventional memory ends at 640 KB = segment 0xA000
VGA = 0xA0000


class Emu:
    def __init__(self, exe, datadir, instr_per_tick=250_000, trace_int=False):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)
        self.datadir = Path(datadir)
        self.instr_per_tick = instr_per_tick
        self.trace_int = trace_int
        self.log_files = False
        self.ticks = 0
        self.mode = 3
        self.pal = bytearray(768)
        self.dac_index = 0
        self.dac_sub = 0
        self.key = 0
        self.mouse = None            # None = no mouse driver; else [x, y, buttons] in pixels
        self.vectors = {}
        self.next_para = HEAP_START
        self.blocks = []
        self.files = {}
        self.next_handle = 5
        self.stop_reason = None
        self.unknown = {}
        self.stdout = bytearray()
        self.load(exe)
        self.uc.hook_add(UC_HOOK_INTR, self.on_int)
        self.uc.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)
        # default interrupt target: an IRET at F000:FF53; every vector points there at start
        self.uc.mem_write(0xF0000 + 0xFF53, b"\xcf")
        self.uc.mem_write(0, struct.pack("<HH", 0xFF53, 0xF000) * 256)

    # ---------------------------------------------------------------- loading
    def load(self, exe):
        d = Path(exe).read_bytes()
        (sig, lastpage, pages, nreloc, hdrpar, minalloc, maxalloc, ss, sp, csum, ip, cs,
         reloff, ovl) = struct.unpack("<2s13H", d[:28])
        assert sig == b"MZ"
        hdr = hdrpar * 16
        size = (pages - 1) * 512 + (lastpage or 512)
        image = bytearray(d[hdr:size])
        for i in range(nreloc):
            off, seg = struct.unpack("<HH", d[reloff + 4 * i:reloff + 4 * i + 4])
            lin = seg * 16 + off
            val = struct.unpack("<H", image[lin:lin + 2])[0]
            image[lin:lin + 2] = struct.pack("<H", (val + LOAD_SEG) & 0xFFFF)
        self.uc.mem_write(LOAD_SEG * 16, bytes(image))
        self.image_paras = (len(image) + 15) // 16
        # The heap starts after the image and the extra memory the header asks for.
        self.next_para = LOAD_SEG + self.image_paras + minalloc + 0x10
        # PSP
        psp = bytearray(256)
        psp[0:2] = b"\xcd\x20"
        struct.pack_into("<H", psp, 2, HEAP_END)      # top of memory segment
        struct.pack_into("<H", psp, 0x2C, ENV_SEG)    # environment segment
        psp[0x80] = 0
        psp[0x81] = 0x0D
        self.uc.mem_write(PSP_SEG * 16, bytes(psp))
        env = b"COMSPEC=C:\\COMMAND.COM\0\0" + struct.pack("<H", 1) + b"C:\\" + Path(exe).name.upper().encode() + b"\0"
        self.uc.mem_write(ENV_SEG * 16, env)
        u = self.uc
        u.reg_write(UC_X86_REG_DS, PSP_SEG)
        u.reg_write(UC_X86_REG_ES, PSP_SEG)
        u.reg_write(UC_X86_REG_SS, (ss + LOAD_SEG) & 0xFFFF)
        u.reg_write(UC_X86_REG_SP, sp)
        u.reg_write(UC_X86_REG_CS, (cs + LOAD_SEG) & 0xFFFF)
        u.reg_write(UC_X86_REG_IP, ip)
        # BIOS data area: tick counter, keyboard buffer pointers
        self.set_ticks(0)
        u.mem_write(0x41A, struct.pack("<HH", 0x1E, 0x1E))

    def set_ticks(self, t):
        self.uc.mem_write(0x46C, struct.pack("<I", t & 0xFFFFFFFF))

    # --------------------------------------------------------------- helpers
    def r(self, reg):
        return self.uc.reg_read(reg)

    def w(self, reg, v):
        self.uc.reg_write(reg, v & 0xFFFF)

    def set_cf(self, flag):
        f = self.r(UC_X86_REG_EFLAGS)
        f = (f | 1) if flag else (f & ~1)
        self.uc.reg_write(UC_X86_REG_EFLAGS, f)

    def cstring(self, seg, off):
        out = bytearray()
        a = seg * 16 + off
        while len(out) < 128:
            b = self.uc.mem_read(a, 1)[0]
            if b == 0:
                break
            out.append(b)
            a += 1
        return out.decode("ascii", "replace")

    def host_path(self, name):
        n = name.replace("\\", "/").split("/")[-1]
        if not self.datadir.is_dir():
            return None
        for p in self.datadir.iterdir():
            if p.name.lower() == n.lower():
                return p
        return self.datadir / n.upper()

    def note_unknown(self, what):
        self.unknown[what] = self.unknown.get(what, 0) + 1

    # ----------------------------------------------------------- interrupts
    def on_int(self, uc, intno, _):
        ax = self.r(UC_X86_REG_AX)
        ah = ax >> 8
        if self.trace_int:
            print(f"  int {intno:02x} ax={ax:04x} bx={self.r(UC_X86_REG_BX):04x} cx={self.r(UC_X86_REG_CX):04x}"
                  f" dx={self.r(UC_X86_REG_DX):04x}", file=sys.stderr)
        if intno == 0x21:
            self.dos(ax, ah)
        elif intno == 0x10:
            if ah == 0:
                self.mode = ax & 0xFF
            else:
                self.note_unknown(f"int10 ah={ah:02x}")
        elif intno == 0x33:
            if self.mouse is None:
                self.w(UC_X86_REG_AX, 0)       # no mouse driver
            elif ax == 0:                      # reset: a driver with two buttons is there
                self.w(UC_X86_REG_AX, 0xFFFF)
                self.w(UC_X86_REG_BX, 2)
            elif ax == 3:                      # position and buttons, in pixels (the program takes them as such)
                self.w(UC_X86_REG_BX, self.mouse[2])
                self.w(UC_X86_REG_CX, self.mouse[0])
                self.w(UC_X86_REG_DX, self.mouse[1])
            # other functions (show/hide, ranges, ratios, motion counters): accepted and ignored
        elif intno == 0x16:
            self.w(UC_X86_REG_AX, 0)
            self.note_unknown(f"int16 ah={ah:02x}")
        else:
            self.note_unknown(f"int{intno:02x}")

    def alloc(self, paras):
        if self.next_para + paras > HEAP_END:
            return None
        seg = self.next_para
        self.next_para += paras
        self.blocks.append([seg, paras])
        return seg

    def dos(self, ax, ah):
        u = self.uc
        al = ax & 0xFF
        bx = self.r(UC_X86_REG_BX)
        cx = self.r(UC_X86_REG_CX)
        dx = self.r(UC_X86_REG_DX)
        ds = self.r(UC_X86_REG_DS)
        self.set_cf(False)
        if ah == 0x30:
            self.w(UC_X86_REG_AX, 0x0005)
            self.w(UC_X86_REG_BX, 0)
            self.w(UC_X86_REG_CX, 0)
        elif ah == 0x25:
            u.mem_write(al * 4, struct.pack("<HH", dx, ds))
        elif ah == 0x35:
            off, seg = struct.unpack("<HH", bytes(u.mem_read(al * 4, 4)))
            self.w(UC_X86_REG_ES, seg)
            self.w(UC_X86_REG_BX, off)
        elif ah == 0x48:
            seg = self.alloc(bx)
            if seg is None:
                self.set_cf(True)
                self.w(UC_X86_REG_AX, 8)
                self.w(UC_X86_REG_BX, HEAP_END - self.next_para)
            else:
                self.w(UC_X86_REG_AX, seg)
        elif ah == 0x49:
            es = self.r(UC_X86_REG_ES)
            if self.blocks and self.blocks[-1][0] == es:
                seg, paras = self.blocks.pop()
                self.next_para = seg
        elif ah == 0x4A:
            # Resize a block. Only the most recently allocated block can grow (nothing
            # sits above it); shrinking always works. Otherwise fail like DOS does:
            # CF set, AX=8 (insufficient memory), BX = largest size possible.
            es = self.r(UC_X86_REG_ES)
            blk = next((b for b in self.blocks if b[0] == es), None)
            if blk is not None:
                seg, paras = blk
                if bx <= paras:
                    blk[1] = bx
                    if blk is self.blocks[-1]:
                        self.next_para = seg + bx
                elif blk is self.blocks[-1] and seg + bx <= HEAP_END:
                    blk[1] = bx
                    self.next_para = seg + bx
                else:
                    self.set_cf(True)
                    self.w(UC_X86_REG_AX, 8)
                    self.w(UC_X86_REG_BX, paras)
        elif ah == 0x3D:
            name = self.cstring(ds, dx)
            p = self.host_path(name)
            if p is None or not p.exists():
                self.set_cf(True)
                self.w(UC_X86_REG_AX, 2)
                self.note_unknown(f"open-missing {name}")
            else:
                h = self.next_handle
                self.next_handle += 1
                self.files[h] = [open(p, "r+b" if (al & 3) else "rb"), name]
                self.w(UC_X86_REG_AX, h)
                if self.log_files:
                    print(f"tick {self.ticks}: open {name}")
        elif ah == 0x3C:
            name = self.cstring(ds, dx)
            p = self.host_path(name)
            h = self.next_handle
            self.next_handle += 1
            self.files[h] = [open(p, "w+b"), name]
            self.w(UC_X86_REG_AX, h)
        elif ah == 0x3E:
            f = self.files.pop(bx, None)
            if f:
                f[0].close()
        elif ah == 0x3F:
            f = self.files.get(bx)
            if not f:
                self.set_cf(True)
                self.w(UC_X86_REG_AX, 6)
            else:
                data = f[0].read(cx)
                u.mem_write(ds * 16 + dx, data)
                self.w(UC_X86_REG_AX, len(data))
        elif ah == 0x40:
            data = bytes(u.mem_read(ds * 16 + dx, cx))
            if bx in (1, 2):
                self.stdout += data
            else:
                f = self.files.get(bx)
                if f:
                    f[0].write(data)
                    f[0].flush()
            self.w(UC_X86_REG_AX, cx)
        elif ah == 0x42:
            f = self.files.get(bx)
            off = (cx << 16) | dx
            if off >= 0x80000000:
                off -= 0x100000000
            if f:
                f[0].seek(off, {0: 0, 1: 1, 2: 2}[al])
                pos = f[0].tell()
                self.w(UC_X86_REG_AX, pos & 0xFFFF)
                self.w(UC_X86_REG_DX, pos >> 16)
        elif ah == 0x44:
            if al == 0:
                self.w(UC_X86_REG_DX, 0x80D3 if bx <= 2 else 0x0002)
            else:
                self.note_unknown(f"ioctl al={al:02x}")
        elif ah == 0x43:
            self.w(UC_X86_REG_CX, 0x20)
        elif ah in (0x1A, 0x67):
            pass
        elif ah == 0x29:
            self.w(UC_X86_REG_AX, 0)
        elif ah == 0x52:
            u.mem_write(0x700, bytes(64))
            self.w(UC_X86_REG_ES, 0x70)
            self.w(UC_X86_REG_BX, 0)
        elif ah == 0x09:
            s = bytearray()
            a = ds * 16 + dx
            while len(s) < 200 and u.mem_read(a, 1)[0] != 0x24:
                s.append(u.mem_read(a, 1)[0])
                a += 1
            self.stdout += s
        elif ah == 0x4B:
            name = self.cstring(ds, dx)
            self.stop_reason = f"exec {name} (type {al})"
            u.emu_stop()
        elif ah == 0x4C:
            self.stop_reason = f"exit code {al}"
            u.emu_stop()
        else:
            self.note_unknown(f"int21 ah={ah:02x}")

    # ----------------------------------------------------------------- ports
    def on_in(self, uc, port, size, _):
        if port == 0x3DA:
            return 0x01
        if port == 0x60:
            return self.key
        if port in (0x3C9,):
            return 0
        self.note_unknown(f"in {port:04x}")
        return 0

    def on_out(self, uc, port, size, value, _):
        if port == 0x3C8:
            self.dac_index = value & 0xFF
            self.dac_sub = 0
        elif port == 0x3C9:
            self.pal[self.dac_index * 3 + self.dac_sub] = value & 0x3F
            self.dac_sub += 1
            if self.dac_sub == 3:
                self.dac_sub = 0
                self.dac_index = (self.dac_index + 1) & 0xFF
        elif port in (0x3D8, 0x61, 0x20, 0x3C0, 0x3C4, 0x3C5, 0x3CE, 0x3CF, 0x3D4, 0x3D5):
            pass
        else:
            self.note_unknown(f"out {port:04x}")

    # ------------------------------------------------------------- execution
    def snapshot(self):
        return bytes(self.uc.mem_read(VGA, 64000)), bytes(self.pal)

    def read_vector(self, n):
        off, seg = struct.unpack("<HH", bytes(self.uc.mem_read(n * 4, 4)))
        return seg, off

    def deliver_irq8(self):
        """Call the program's INT 8 handler like the CPU would: push FLAGS, CS, IP; clear IF."""
        v = self.read_vector(8)
        if not v or v[0] == 0xF000 or v == (0, 0):
            return
        u = self.uc
        ss, sp = self.r(UC_X86_REG_SS), self.r(UC_X86_REG_SP)
        flags = self.r(UC_X86_REG_EFLAGS) & 0xFFFF
        cs, ip = self.r(UC_X86_REG_CS), self.r(UC_X86_REG_IP)
        sp = (sp - 6) & 0xFFFF
        u.mem_write(ss * 16 + sp, struct.pack("<HHH", ip, cs, flags))
        self.w(UC_X86_REG_SP, sp)
        self.uc.reg_write(UC_X86_REG_EFLAGS, self.r(UC_X86_REG_EFLAGS) & ~0x200)
        self.w(UC_X86_REG_CS, v[0])
        self.w(UC_X86_REG_IP, v[1])

    def watch_pass(self, addr, tick_div, snaps, keys, sync_var=None, mice=None):
        """Make a loop deterministic. `addr` is the loop-top instruction (linear address).

        Each arrival counts one more *completed pass*. From the first arrival on, the BIOS
        tick counter is set to base + completed_passes // tick_div, so the program's
        tick-gated work depends on the pass count and not on emulation speed.
        `snaps` maps completed-pass number -> output path (raw screen+palette and a PPM);
        `keys` maps completed-pass number -> value for keyboard port 0x60.
        `sync_var`, if given, is a BP-relative offset of a 16-bit local that holds the last
        tick the program saw; at the first arrival it is set to the tick base so the first
        tick change happens at a pass number we control.
        Stops after the last snapshot."""
        from unicorn import UC_HOOK_CODE
        self.pass_count = 0
        self.tick_base = None
        self.freeze_ticks = False
        last = max(snaps) if snaps else None

        def cb(uc, address, size, user):
            n = self.pass_count          # passes completed before this arrival
            self.pass_count += 1
            if self.tick_base is None:
                self.tick_base = struct.unpack("<I", bytes(uc.mem_read(0x46C, 4)))[0]
                self.freeze_ticks = True
                if sync_var is not None:
                    a = self.r(UC_X86_REG_SS) * 16 + ((self.r(UC_X86_REG_BP) - sync_var) & 0xFFFF)
                    uc.mem_write(a, struct.pack("<H", self.tick_base & 0xFFFF))
            self.set_ticks(self.tick_base + n // tick_div)
            if n in keys:
                self.key = keys[n]
            if mice and n in mice:
                self.mouse = list(mice[n])
            if n in snaps:
                pix, pal = self.snapshot()
                path = snaps[n]
                Path(path).write_bytes(pix + pal)
                write_ppm(path + ".ppm", pix, pal)
                print(f"pass {n}: wrote {path}")
            if last is not None and n >= last:
                self.stop_reason = f"snapshots done at pass {n}"
                uc.emu_stop()

        self.uc.hook_add(UC_HOOK_CODE, cb, None, addr, addr)

    def watch_hits(self, specs):
        """Snapshot at the N-th (0-based) arrival at an address, without touching the clock.

        `specs` is a list of (address, n, path). Unlike watch_pass this leaves the BIOS tick
        counter alone, so code that waits for the clock (such as the subtitle waits of
        STORY2.SL) still finishes. Use it for loops whose passes do not depend on time.
        Stops when every snapshot has been written."""
        from unicorn import UC_HOOK_CODE
        todo = {}
        for addr, n, path in specs:
            todo.setdefault(addr, {})[n] = path
        counts = {a: 0 for a in todo}
        remaining = [len(specs)]

        def mk(addr):
            def cb(uc, address, size, user):
                n = counts[addr]
                counts[addr] += 1
                path = todo[addr].get(n)
                if path is not None:
                    pix, pal = self.snapshot()
                    Path(path).write_bytes(pix + pal)
                    write_ppm(path + ".ppm", pix, pal)
                    print(f"hit {addr:#x} #{n}: wrote {path}")
                    remaining[0] -= 1
                    if remaining[0] == 0:
                        self.stop_reason = "snapshots done"
                        uc.emu_stop()
            return cb

        for addr in todo:
            self.uc.hook_add(UC_HOOK_CODE, mk(addr), None, addr, addr)

    def watch_peeks(self, specs):
        """Print `len` bytes at linear address `lin` the first time execution reaches `addr`.
        `specs` is a list of (addr, lin, len). Used to read program state (for example the
        random generator's seed) that a C-side comparison has to start from."""
        from unicorn import UC_HOOK_CODE

        def mk(addr, lin, n):
            seen = [False]

            def cb(uc, address, size, user):
                if not seen[0]:
                    seen[0] = True
                    data = bytes(uc.mem_read(lin, n))
                    print(f"peek at {addr:#x}: [{lin:#x}..+{n}] = {data.hex()}")
            return cb

        for addr, lin, n in specs:
            self.uc.hook_add(UC_HOOK_CODE, mk(addr, lin, n), None, addr, addr)

    def watch_hit_pass(self, addrs, limit=40, stop_at_first=False):
        """With --loop-top: print, for each arrival at one of `addrs`, the number of loop passes
        completed so far (as watch_pass counts them). For finding in which pass a branch is taken."""
        from unicorn import UC_HOOK_CODE
        seen = [0]

        def cb(uc, address, size, user):
            if seen[0] < limit:
                seen[0] += 1
                print(f"reached {address:#x} after {getattr(self, 'pass_count', 0) - 1} completed pass(es)", flush=True)
                if stop_at_first:
                    self.stop_reason = f"stopped at the first arrival at {address:#x}"
                    uc.emu_stop()

        for a in addrs:
            self.uc.hook_add(UC_HOOK_CODE, cb, None, a, a)

    def watch_dump_local(self, specs):
        """At the first arrival at `addr`, read the far pointer stored in the local variable at
        [bp + bpoff] (offset word, then segment word, as the program's C code keeps them), and write
        `n` bytes from that address to `path`. `specs` is a list of (addr, bpoff, n, path). Used to
        look at the program's working buffers (decoded pictures, composed screens)."""
        from unicorn import UC_HOOK_CODE

        def mk(addr, bpoff, n, path):
            seen = [False]

            def cb(uc, address, size, user):
                if seen[0]:
                    return
                seen[0] = True
                bp = uc.reg_read(UC_X86_REG_BP)
                ss = uc.reg_read(UC_X86_REG_SS)
                at = ss * 16 + ((bp + bpoff) & 0xFFFF)
                off = int.from_bytes(bytes(uc.mem_read(at, 2)), "little")
                seg = int.from_bytes(bytes(uc.mem_read(at + 2, 2)), "little")
                lin = seg * 16 + off
                Path(path).write_bytes(bytes(uc.mem_read(lin, n)))
                print(f"dump at {addr:#x}: [bp{bpoff:+#x}] = {seg:04x}:{off:04x} (linear {lin:#x}), {n} bytes -> {path}")
            return cb

        for addr, bpoff, n, path in specs:
            self.uc.hook_add(UC_HOOK_CODE, mk(addr, bpoff, n, path), None, addr, addr)

    def watch_calls(self, addrs, nwords, limit, outpath):
        """Log each arrival at the given function entry addresses (far calls: the return address
        takes 4 bytes, so the arguments start at ss:sp+4) with NWORDS argument words, in order, to
        OUTPATH (one line per call: running number, tick, address, return cs:ip, arguments).
        Stops logging after LIMIT calls. For reading what a program draws, and in what order."""
        from unicorn import UC_HOOK_CODE
        f = open(outpath, "w")
        count = [0]

        def cb(uc, address, size, user):
            if count[0] >= limit:
                return
            sp = uc.reg_read(UC_X86_REG_SP)
            ss = uc.reg_read(UC_X86_REG_SS)
            raw = bytes(uc.mem_read(ss * 16 + sp, 4 + 2 * nwords))
            ret_ip = int.from_bytes(raw[0:2], "little")
            ret_cs = int.from_bytes(raw[2:4], "little")
            args = [int.from_bytes(raw[4 + 2 * i:6 + 2 * i], "little") for i in range(nwords)]
            f.write("%5d t%-4d %#x ret %04x:%04x (%#x) %s\n" % (
                count[0], self.ticks, address, ret_cs, ret_ip, ret_cs * 16 + ret_ip,
                " ".join("%04x" % a for a in args)))
            count[0] += 1
            if count[0] == limit:
                f.write("-- limit reached\n")
            f.flush()

        for a in addrs:
            self.uc.hook_add(UC_HOOK_CODE, cb, None, a, a)

    def run(self, max_ticks, hooks):
        u = self.uc
        total = 0
        while self.stop_reason is None and self.ticks <= max_ticks:
            for fn in hooks.get(self.ticks, []):
                fn(self)
            if self.ticks > 0 and (self.r(UC_X86_REG_EFLAGS) & 0x200):
                self.deliver_irq8()
            cs = self.r(UC_X86_REG_CS)
            ip = self.r(UC_X86_REG_IP)
            try:
                u.emu_start(cs * 16 + ip, 0xFFFFF, count=self.instr_per_tick)
            except UcError as e:
                self.stop_reason = f"CPU error: {e} at {self.r(UC_X86_REG_CS):04x}:{self.r(UC_X86_REG_IP):04x}"
                break
            total += self.instr_per_tick
            self.ticks += 1
            if not getattr(self, "freeze_ticks", False):
                self.set_ticks(self.ticks)
        if self.stop_reason is None:
            self.stop_reason = f"reached tick {self.ticks}"


def write_ppm(path, pix, pal):
    lut = [v * 255 // 63 for v in range(64)]
    out = bytearray(b"P6\n320 200\n255\n")
    for p in pix:
        out += bytes((lut[pal[p * 3]], lut[pal[p * 3 + 1]], lut[pal[p * 3 + 2]]))
    Path(path).write_bytes(bytes(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("--data", default=str(ROOT / "original_bin"), help="source data directory (copied, never modified)")
    ap.add_argument("--work", default=str(ROOT / "work" / "emu_data"), help="private copy used by the emulated program")
    ap.add_argument("--ticks", type=int, default=100)
    ap.add_argument("--instr-per-tick", type=int, default=250_000)
    ap.add_argument("--snap", action="append", default=[], metavar="TICK:FILE.ppm")
    ap.add_argument("--key", action="append", default=[], metavar="TICK:SCANCODE[:RELEASE_TICK]")
    ap.add_argument("--mouse", action="append", default=[], metavar="TICK:X:Y:BUTTONS",
                    help="a mouse driver is present; from TICK the position is (X, Y) pixels and BUTTONS "
                         "(bit 0 left, bit 1 right) the buttons held. Several allowed; the first sets the "
                         "state from the start if its TICK is 0")
    ap.add_argument("--trace-int", action="store_true")
    ap.add_argument("--log-files", action="store_true", help="print each successful file open with the tick")
    ap.add_argument("--loop-top", metavar="ADDR", help="linear address of a loop's first instruction; enables pass counting")
    ap.add_argument("--tick-div", type=int, default=3, help="BIOS ticks advance once per this many completed passes")
    ap.add_argument("--snap-pass", action="append", default=[], metavar="N:FILE.raw",
                    help="save screen+palette after N completed passes of the --loop-top loop")
    ap.add_argument("--snap-hit", action="append", default=[], metavar="ADDR:N:FILE.raw",
                    help="save screen+palette at the N-th (0-based) arrival at linear address ADDR "
                         "(hex); the clock is not touched")
    ap.add_argument("--dump-local", action="append", default=[], metavar="ADDR:BPOFF:LEN:FILE",
                    help="at the first arrival at ADDR (hex), write LEN bytes from the far pointer "
                         "stored at [bp+BPOFF] (BPOFF like -0x2a) to FILE")
    ap.add_argument("--hit-pass-stop", action="store_true",
                    help="with --hit-pass: end the run at the first arrival (e.g. when the program leaves its loop)")
    ap.add_argument("--hit-pass", metavar="ADDR[,ADDR...]",
                    help="with --loop-top: print the number of completed passes at each arrival at these addresses (hex)")
    ap.add_argument("--trace-calls", metavar="ADDR[,ADDR...]:NWORDS:LIMIT:FILE",
                    help="log calls to these function entries (hex linear addresses) with NWORDS stack "
                         "argument words, at most LIMIT calls, to FILE")
    ap.add_argument("--peek", action="append", default=[], metavar="ADDR:LINEAR:LEN",
                    help="print LEN bytes at linear address LINEAR (hex) the first time execution "
                         "reaches ADDR (hex)")
    ap.add_argument("--mouse-pass", action="append", default=[], metavar="N:X:Y:BUTTONS",
                    help="with --loop-top: a mouse driver is present; from the arrival at the loop top "
                         "after N completed passes the position is (X, Y) pixels and BUTTONS the buttons held")
    ap.add_argument("--key-pass", action="append", default=[], metavar="N:CODE",
                    help="set keyboard port 0x60 to CODE after N completed passes")
    ap.add_argument("--sync-var", metavar="BPOFF",
                    help="BP-relative offset of the loop's 'last tick' local (hex); set to the tick base at pass 0")
    a = ap.parse_args()

    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    for p in Path(a.data).iterdir():
        if p.is_file():
            shutil.copy2(p, work / p.name)       # overwrite the private copy; original_bin is only read

    emu = Emu(a.exe, work, a.instr_per_tick, a.trace_int)
    emu.log_files = a.log_files
    hooks = {}

    def add(t, fn):
        hooks.setdefault(t, []).append(fn)

    for s in a.snap:
        t, f = s.split(":", 1)

        def mk(f=f):
            def fn(e):
                pix, pal = e.snapshot()
                write_ppm(f, pix, pal)
                Path(f + ".raw").write_bytes(pix + pal)
                print(f"tick {e.ticks}: wrote {f} (mode {e.mode:#x})")
            return fn
        add(int(t), mk())
    if a.mouse:
        emu.mouse = [0, 0, 0]
        for k in a.mouse:
            t, mx, my, mb = (int(x, 0) for x in k.split(":"))
            add(t, lambda e, mx=mx, my=my, mb=mb: setattr(e, "mouse", [mx, my, mb]))
    for k in a.key:
        parts = k.split(":")
        t, sc = int(parts[0]), int(parts[1], 0)
        add(t, lambda e, sc=sc: setattr(e, "key", sc))
        if len(parts) > 2:
            add(int(parts[2]), lambda e: setattr(e, "key", 0))

    if a.hit_pass:
        emu.watch_hit_pass([int(x, 16) for x in a.hit_pass.split(",")], stop_at_first=a.hit_pass_stop)
    if a.trace_calls:
        al, nw, lim, path = a.trace_calls.split(":", 3)
        emu.watch_calls([int(x, 16) for x in al.split(",")], int(nw), int(lim), path)
    if a.dump_local:
        specs = []
        for x in a.dump_local:
            addr, bpoff, n, path = x.split(":", 3)
            specs.append((int(addr, 16), int(bpoff, 0), int(n, 0), path))
        emu.watch_dump_local(specs)
    if a.peek:
        emu.watch_peeks([(int(x.split(":")[0], 16), int(x.split(":")[1], 16), int(x.split(":")[2], 0)) for x in a.peek])

    if a.snap_hit:
        specs = []
        for s in a.snap_hit:
            addr, n, path = s.split(":", 2)
            specs.append((int(addr, 16), int(n), path))
        emu.watch_hits(specs)

    if a.loop_top:
        snaps = {}
        for s in a.snap_pass:
            n, path = s.split(":", 1)
            snaps[int(n)] = path
        keys = {}
        for k in a.key_pass:
            n, code = k.split(":", 1)
            keys[int(n)] = int(code, 0)
        mice = {}
        for k in a.mouse_pass:
            n, mx, my, mb = (int(x, 0) for x in k.split(":"))
            mice[n] = (mx, my, mb)
            if emu.mouse is None:
                emu.mouse = [0, 0, 0]
        emu.watch_pass(int(a.loop_top, 16), a.tick_div, snaps, keys,
                       int(a.sync_var, 16) if a.sync_var else None, mice)

    emu.run(a.ticks, hooks)
    r = emu.r
    if a.loop_top:
        print(f"loop passes started: {emu.pass_count}")
    print(f"stopped: {emu.stop_reason}  (ticks {emu.ticks}, video mode {emu.mode:#x})")
    print(f"cpu at stop: cs:ip={r(UC_X86_REG_CS):04x}:{r(UC_X86_REG_IP):04x} (linear {r(UC_X86_REG_CS) * 16 + r(UC_X86_REG_IP):#x})"
          f" ax={r(UC_X86_REG_AX):04x} bx={r(UC_X86_REG_BX):04x} cx={r(UC_X86_REG_CX):04x} dx={r(UC_X86_REG_DX):04x}"
          f" si={r(UC_X86_REG_SI):04x} di={r(UC_X86_REG_DI):04x} ds={r(UC_X86_REG_DS):04x} es={r(UC_X86_REG_ES):04x}")
    if emu.stdout:
        print("program output:", emu.stdout.decode("ascii", "replace"))
    if emu.unknown:
        print("unhandled requests (count):")
        for k, v in sorted(emu.unknown.items()):
            print(f"   {k}: {v}")


if __name__ == "__main__":
    main()
