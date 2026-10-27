"""Read an Xbox disc image (XDVDFS: an XISO, or a redump with the video partition first).

  python tools/xiso.py list    <image.iso>
  python tools/xiso.py info    <image.iso>              XBE header, certificate, sections and XDK libraries
  python tools/xiso.py extract <image.iso> <out dir> [path ...]   whole image, or only the paths given (/DEFAULT.XBE)
"""
import hashlib
import os
import struct
import sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
# where the game partition starts: an XISO at 0, redumps of the three disc layouts further in
PARTITION_OFFSETS = (0, 0x18300000, 0x2080000, 0xFD90000)
XBE_ENTRY_KEY_RETAIL = 0xA8FC57AB
XBE_ENTRY_KEY_DEBUG = 0x94859D4B


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        for off in PARTITION_OFFSETS:
            self.f.seek(off + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                self.base = off
                break
        else:
            sys.exit(f"{path}: no XDVDFS volume found")
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))

    def read(self, sector, size):
        self.f.seek(self.base + sector * SECTOR)
        return self.f.read(size)

    def files(self):
        """(path, sector, size, is_dir), parents before children, in name order."""
        return list(self._walk(self.root_sector, self.root_size, ""))

    def _walk(self, sector, size, parent):
        if size == 0:
            return
        table = self.read(sector, size)
        # each directory is a binary tree of entries; offsets in dwords, 0xFFFF pads a sector's end
        stack, seen, entries = [0], set(), []
        while stack:
            o = stack.pop()
            if o in seen or o * 4 + 14 > len(table):
                continue
            seen.add(o)
            left, right, start, length, attr, name_len = struct.unpack_from("<HHIIBB", table, o * 4)
            if left == 0xFFFF:
                continue
            name = table[o * 4 + 14:o * 4 + 14 + name_len].decode("latin-1")
            entries.append((name, start, length, bool(attr & 0x10)))
            stack += [x for x in (left, right) if x]
        for name, start, length, is_dir in sorted(entries, key=lambda e: e[0].upper()):
            path = f"{parent}/{name}"
            yield path, start, length, is_dir
            if is_dir:
                yield from self._walk(start, length, path)


def xbe_info(x):
    base = struct.unpack_from("<I", x, 0x104)[0]

    def at(va, n=None):
        return x[va - base:] if n is None else x[va - base:va - base + n]

    def cstr(va):
        return at(va).split(b"\0")[0].decode("latin-1")

    cert = at(struct.unpack_from("<I", x, 0x118)[0])
    n_sections, sections = struct.unpack_from("<II", x, 0x11C)
    entry = struct.unpack_from("<I", x, 0x128)[0]
    n_libs, libs = struct.unpack_from("<II", x, 0x160)
    title_id, = struct.unpack_from("<I", cert, 8)
    print(f"title      {cert[0xC:0xC + 80].decode('utf-16le').rstrip(chr(0))}")
    print(f"title id   {title_id:08X}  region {struct.unpack_from('<I', cert, 0xA0)[0]:#x}"
          f"  version {struct.unpack_from('<I', cert, 0xAC)[0]:#x}")
    print(f"sha1       {hashlib.sha1(x).hexdigest()}  ({len(x)} bytes)")
    print(f"base       {base:#010x}  entry {entry ^ XBE_ENTRY_KEY_RETAIL:#010x} (retail)"
          f" / {entry ^ XBE_ENTRY_KEY_DEBUG:#010x} (debug)")
    print("sections")
    for i in range(n_sections):
        flags, va, vsize, raw, rsize, name = struct.unpack_from("<IIIIII", at(sections + i * 56, 24))
        print(f"  {cstr(name):<10} va {va:#010x}  size {vsize:#09x}  flags {flags:#06x}")
    print("libraries")
    for i in range(n_libs):
        lib = at(libs + i * 16, 16)
        major, minor, build, flags = struct.unpack_from("<HHHH", lib, 8)
        print(f"  {lib[:8].rstrip(b'\0').decode():<8} {major}.{minor}.{build}  flags {flags:#06x}")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "info", "extract"):
        sys.exit(__doc__)
    image = Image(sys.argv[2])
    files = image.files()
    if sys.argv[1] == "list":
        for path, _, size, is_dir in files:
            print(f"{'<dir>' if is_dir else size:>12}  {path}")
        print(f"{sum(not d for *_, d in files)} files")
    elif sys.argv[1] == "info":
        xbe = next((f for f in files if f[0].upper() == "/DEFAULT.XBE"), None)
        if not xbe:
            sys.exit("no /default.xbe on the image")
        xbe_info(image.read(xbe[1], xbe[2]))
    else:
        if len(sys.argv) < 4:
            sys.exit(__doc__)
        out = sys.argv[3]
        wanted = {p.upper() for p in sys.argv[4:]}
        for path, sector, size, is_dir in files:
            if wanted and path.upper() not in wanted:
                continue
            dest = os.path.join(out, *path.strip("/").split("/"))
            if is_dir:
                os.makedirs(dest, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as o:
                left = size
                image.f.seek(image.base + sector * SECTOR)
                while left:
                    chunk = image.f.read(min(left, 1 << 20))
                    o.write(chunk)
                    left -= len(chunk)
            print(f"{size:>12}  {path}")


if __name__ == "__main__":
    main()
