#!/usr/bin/env python3
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
"""Builds the Mac package fixtures the tests open.

Nothing here comes from Apple. Every byte is written by this script: a small
HFS+ volume, the UDIF container that makes it a .dmg, a xar archive that is a
.pkg, and an application bundle whose program is a Mach-O header with no code
after it. The bundle cannot run anywhere. It is there so the desktop's
unpacking, naming, icon, and architecture checks have something real to read.

  mac-fixtures.py OUTDIR
  mac-fixtures.py --wrap PROGRAM NAME OUT.dmg   (or OUT.pkg)

  DeskMac.dmg        zlib UDIF, HFS+ volume, one x86_64+arm64 application
  DeskMacApm.dmg     the same volume behind an Apple partition map
  DeskMacBz.dmg      the same volume, bzip2 chunks
  DeskMacTwo.dmg     two volumes, the application in the second
  DeskArm.dmg        an application built for arm64 only
  DeskCjk.dmg        an application with a Chinese name
  DeskBig.dmg        a 24 MB application, to stop an install half way
  DeskMac.pkg        product archive, gzip cpio payload, binary Info.plist
  DeskMacPbzx.pkg    the same with a pbzx payload
  DeskTool.pkg       a package with no application in it
  Evil.dmg           a volume whose entries try to leave the unpack directory
  Evil.pkg           a payload that tries the same with a link and with ../
  DeskMac.app/       the bundle as a plain directory

fsck.hfsplus accepts the volumes, and 7-Zip lists every archive; the test
checks both when those tools are installed.
"""
import base64
import bz2
import gzip
import hashlib
import io
import lzma
import os
import plistlib
import struct
import sys
import zlib

HFS_EPOCH = 2082844800  # seconds from 1904 to 1970
NOW = 1794787200 + HFS_EPOCH  # a fixed date, so the fixtures are reproducible


# ---------------------------------------------------------------- pictures

def png(size, rgb):
    """A square icon: a rounded tile with a lighter band, drawn by hand."""
    rows = bytearray()
    radius = size // 5
    for y in range(size):
        rows.append(0)
        for x in range(size):
            dx = min(x, size - 1 - x)
            dy = min(y, size - 1 - y)
            inside = True
            if dx < radius and dy < radius:
                inside = (radius - dx) ** 2 + (radius - dy) ** 2 <= radius ** 2
            if not inside:
                rows += bytes((0, 0, 0, 0))
                continue
            shade = 1.0 - 0.35 * (y / size)
            band = size * 0.42 < y < size * 0.58 and size * 0.2 < x < size * 0.8
            r, g, b = (255, 255, 255) if band else rgb
            rows += bytes((int(r * shade), int(g * shade), int(b * shade), 255))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


def icns(rgb):
    """An .icns holding PNG pictures, which is what current tools write."""
    body = b""
    for ostype, size in ((b"ic07", 128), (b"ic08", 256)):
        data = png(size, rgb)
        body += ostype + struct.pack(">I", len(data) + 8) + data
    return b"icns" + struct.pack(">I", len(body) + 8) + body


# ------------------------------------------------------------------ Mach-O

CPU_X86_64 = 0x01000007
CPU_ARM64 = 0x0100000C


def macho_thin(cputype):
    """A 64-bit Mach-O header for an executable with no load commands."""
    subtype = 3 if cputype == CPU_X86_64 else 0
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, cputype, subtype, 2, 0, 0, 0x00200085, 0)
    return header + b"\0" * 224


def macho_fat(cputypes):
    """A universal binary: a big-endian table, then one slice per CPU."""
    slices = [macho_thin(c) for c in cputypes]
    out = struct.pack(">II", 0xCAFEBABE, len(slices))
    offset = 4096
    table = b""
    for cputype, data in zip(cputypes, slices):
        subtype = 3 if cputype == CPU_X86_64 else 0
        table += struct.pack(">iiIII", cputype, subtype, offset, len(data), 12)
        offset += 4096
    blob = bytearray(out + table)
    for index, data in enumerate(slices):
        start = 4096 * (index + 1)
        blob += b"\0" * (start - len(blob))
        blob += data
    return bytes(blob)


# -------------------------------------------------------------- the bundle

def bundle_files(name, cputypes, binary_plist=False, version="1.2", program=None):
    """The files of NAME.app as {relative path: (bytes, mode)}."""
    if program is None:
        program = macho_fat(cputypes) if len(cputypes) > 1 else macho_thin(cputypes[0])
    info = {
        "CFBundleName": name,
        "CFBundleDisplayName": name,
        "CFBundleExecutable": name,
        "CFBundleIdentifier": "example.olympium." + name.replace(" ", "").lower(),
        "CFBundleShortVersionString": version,
        "CFBundleVersion": "34",
        "CFBundleIconFile": "DeskMac",
        "CFBundlePackageType": "APPL",
        "LSMinimumSystemVersion": "10.13",
    }
    fmt = plistlib.FMT_BINARY if binary_plist else plistlib.FMT_XML
    root = name + ".app/Contents"
    return {
        root + "/Info.plist": (plistlib.dumps(info, fmt=fmt), 0o644),
        root + "/PkgInfo": (b"APPL????", 0o644),
        root + "/MacOS/" + name: (program, 0o755),
        root + "/Resources/DeskMac.icns": (icns((52, 120, 246)), 0o644),
        root + "/Resources/中文说明.txt":
            ("这是一个测试程序。\n".encode("utf-8"), 0o644),
    }


# -------------------------------------------------------------------- HFS+

BLOCK = 4096
NODE = 8192          # catalog node
EXT_NODE = 4096      # extents overflow node


def hfs_fold(name):
    """HFS+ compares names without case. ASCII is all these names need."""
    return [ord(c) + 32 if "A" <= c <= "Z" else ord(c) for c in name]


def utf16(name):
    data = name.encode("utf-16-be")
    return struct.pack(">H", len(data) // 2) + data


class Hfs:
    """Collects folders, files and links, then writes one HFS+ volume."""

    def __init__(self, volume_name, total_blocks=1024):
        self.name = volume_name
        self.total = total_blocks
        self.next_id = 16
        # cnid -> dict(parent, name, kind, data, mode, children)
        self.nodes = {2: dict(parent=1, name=volume_name, kind="dir", mode=0o755)}
        self.by_path = {"": 2}
        self.extra = []  # raw (parent, name, kind, data, mode) records for hostile volumes

    def _add(self, parent, name, kind, data=b"", mode=0o644):
        cnid = self.next_id
        self.next_id += 1
        self.nodes[cnid] = dict(parent=parent, name=name, kind=kind, data=data, mode=mode)
        return cnid

    def mkdirs(self, path):
        if path in self.by_path:
            return self.by_path[path]
        head, _, tail = path.rpartition("/")
        parent = self.mkdirs(head)
        cnid = self._add(parent, tail, "dir", mode=0o755)
        self.by_path[path] = cnid
        return cnid

    def add_file(self, path, data, mode=0o644):
        head, _, tail = path.rpartition("/")
        return self._add(self.mkdirs(head), tail, "file", data, mode)

    def add_link(self, path, target):
        head, _, tail = path.rpartition("/")
        return self._add(self.mkdirs(head), tail, "link", target.encode("utf-8"), 0o777)

    def add_under(self, parent_cnid, name, data):
        """A file whose parent is whatever CNID is given, even a link."""
        return self._add(parent_cnid, name, "file", data, 0o644)

    def build(self):
        total = self.total
        bitmap_blocks = (total // 8 + BLOCK - 1) // BLOCK
        ext_blocks = 4 * EXT_NODE // BLOCK
        cat_nodes = 8
        cat_blocks = cat_nodes * NODE // BLOCK
        cursor = 1
        bitmap_at, cursor = cursor, cursor + bitmap_blocks
        ext_at, cursor = cursor, cursor + ext_blocks
        cat_at, cursor = cursor, cursor + cat_blocks
        image = bytearray(total * BLOCK)

        # File contents first, so the catalog can say where they are.
        extents = {}
        for cnid in sorted(self.nodes):
            node = self.nodes[cnid]
            if node["kind"] == "dir" or not node["data"]:
                continue
            blocks = (len(node["data"]) + BLOCK - 1) // BLOCK
            image[cursor * BLOCK:cursor * BLOCK + len(node["data"])] = node["data"]
            extents[cnid] = (cursor, blocks)
            cursor += blocks
        used_until = cursor
        if used_until >= total - 1:
            raise SystemExit("the volume is too small for its files")

        def fork(length, start, blocks):
            data = struct.pack(">QII", length, 0, blocks)
            data += struct.pack(">II", start, blocks) if blocks else struct.pack(">II", 0, 0)
            return data + b"\0" * 56

        def perms(mode, kind):
            file_type = {"dir": 0o040000, "file": 0o100000, "link": 0o120000}[kind]
            return struct.pack(">IIBBHI", 0, 0, 0, 0, file_type | mode, 0)

        dates = struct.pack(">IIIII", NOW, NOW, NOW, NOW, 0)
        children = {}
        for cnid, node in self.nodes.items():
            children.setdefault(node["parent"], []).append(cnid)

        records = []
        for cnid, node in self.nodes.items():
            key = struct.pack(">I", node["parent"]) + utf16(node["name"])
            if node["kind"] == "dir":
                body = struct.pack(">HHII", 1, 0, len(children.get(cnid, [])), cnid)
                body += dates + perms(node["mode"], "dir") + b"\0" * 32 + struct.pack(">II", 0, 0)
                thread_type = 3
            else:
                start, blocks = extents.get(cnid, (0, 0))
                finder = b"slnkrhap" + b"\0" * 8 if node["kind"] == "link" else b"\0" * 16
                body = struct.pack(">HHII", 2, 2, 0, cnid)
                body += dates + perms(node["mode"], node["kind"]) + finder + b"\0" * 16
                body += struct.pack(">II", 0, 0)
                body += fork(len(node["data"]), start, blocks) + fork(0, 0, 0)
                thread_type = 4
            records.append(((node["parent"], hfs_fold(node["name"])), key, body))
            thread_key = struct.pack(">I", cnid) + utf16("")
            thread = struct.pack(">HHI", thread_type, 0, node["parent"]) + utf16(node["name"])
            records.append(((cnid, []), thread_key, thread))
        records.sort(key=lambda r: r[0])

        # One leaf node holds every record of these small volumes.
        leaf = bytearray(NODE)
        leaf[0:14] = struct.pack(">IIbBHH", 0, 0, -1, 1, len(records), 0)
        at = 14
        offsets = []
        for _, key, body in records:
            record = struct.pack(">H", len(key)) + key + body
            if len(record) % 2:
                record += b"\0"
            offsets.append(at)
            leaf[at:at + len(record)] = record
            at += len(record)
        offsets.append(at)
        if at + 2 * len(offsets) > NODE:
            raise SystemExit("the catalog does not fit one node")
        for index, value in enumerate(offsets):
            struct.pack_into(">H", leaf, NODE - 2 * (index + 1), value)

        def header_node(node_size, depth, root, leaf_records, first, last, max_key,
                        total_nodes, free_nodes, compare, attributes, used_bits):
            node = bytearray(node_size)
            node[0:14] = struct.pack(">IIbBHH", 0, 0, 1, 0, 3, 0)
            node[14:120] = struct.pack(">HIIIIHHIIHIBBI", depth, root, leaf_records, first, last,
                                       node_size, max_key, total_nodes, free_nodes, 0,
                                       node_size, 0, compare, attributes) + b"\0" * 64
            node[248] = used_bits
            for index, value in enumerate((14, 120, 248, node_size - 8)):
                struct.pack_into(">H", node, node_size - 2 * (index + 1), value)
            return node

        catalog = header_node(NODE, 1, 1, len(records), 1, 1, 516, cat_nodes, cat_nodes - 2,
                              0xCF, 6, 0xC0) + leaf + bytes((cat_nodes - 2) * NODE)
        extents_tree = header_node(EXT_NODE, 0, 0, 0, 0, 0, 10, 4, 3, 0, 2, 0x80) + bytes(3 * EXT_NODE)
        image[cat_at * BLOCK:cat_at * BLOCK + len(catalog)] = catalog
        image[ext_at * BLOCK:ext_at * BLOCK + len(extents_tree)] = extents_tree

        bitmap = bytearray(bitmap_blocks * BLOCK)
        for block in list(range(used_until)) + [total - 1]:
            bitmap[block // 8] |= 0x80 >> (block % 8)
        image[bitmap_at * BLOCK:bitmap_at * BLOCK + len(bitmap)] = bitmap

        files = sum(1 for n in self.nodes.values() if n["kind"] != "dir")
        folders = sum(1 for n in self.nodes.values() if n["kind"] == "dir") - 1
        header = struct.pack(">2sHIII", b"H+", 4, 0x100, 0x31302E30, 0)
        header += struct.pack(">IIII", NOW, NOW, 0, NOW)
        header += struct.pack(">IIIIIIIIII", files, folders, BLOCK, total, total - used_until - 1,
                              used_until, 65536, 65536, self.next_id, 1)
        header += struct.pack(">Q", 1)
        header += b"\0" * 24 + hashlib.sha1(self.name.encode()).digest()[:8]
        header += fork(bitmap_blocks * BLOCK, bitmap_at, bitmap_blocks)
        header += fork(ext_blocks * BLOCK, ext_at, ext_blocks)
        header += fork(cat_blocks * BLOCK, cat_at, cat_blocks)
        header += fork(0, 0, 0) + fork(0, 0, 0)
        assert len(header) == 512, len(header)
        image[1024:1536] = header
        image[len(image) - 1024:len(image) - 512] = header
        return bytes(image)


def hfs_volume(volume_name, files, links=()):
    volume = Hfs(volume_name)
    for path, (data, mode) in files.items():
        volume.add_file(path, data, mode)
    for path, target in links:
        volume.add_link(path, target)
    return volume.build()


# -------------------------------------------------------------------- UDIF

def crc_field(data):
    return struct.pack(">II", 2, 32) + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF) + b"\0" * 124


def udif(partitions, method="zlib"):
    """partitions: list of (name, bytes). Each becomes one blkx entry."""
    fork = bytearray()
    entries = []
    sector = 0
    checks = b""
    for index, (name, data) in enumerate(partitions):
        sectors = len(data) // 512
        chunks = b""
        count = 0
        step = 2048 * 512
        for start in range(0, len(data), step):
            piece = data[start:start + step]
            piece_sectors = len(piece) // 512
            if piece.count(0) == len(piece):
                # A run of zeros is stored as nothing. Type 0 is "zero fill",
                # which counts toward the checksum; type 2, "ignore", does not
                # in every reader, and 7-Zip then reports a checksum error.
                kind, packed = 0x00000000, b""
            elif method == "bz2":
                kind, packed = 0x80000006, bz2.compress(piece)
            else:
                kind, packed = 0x80000005, zlib.compress(piece, 9)
            chunks += struct.pack(">IIQQQQ", kind, 0, start // 512, piece_sectors, len(fork), len(packed))
            fork += packed
            count += 1
        chunks += struct.pack(">IIQQQQ", 0xFFFFFFFF, 0, sectors, 0, len(fork), 0)
        count += 1
        crc = zlib.crc32(data) & 0xFFFFFFFF
        checks += struct.pack(">I", crc)
        mish = struct.pack(">4sIQQQII", b"mish", 1, sector, sectors, 0, 2048 + 8, index)
        mish += b"\0" * 24 + crc_field(data) + struct.pack(">I", count) + chunks
        entries.append((name, index, mish))
        sector += sectors
    items = ""
    for name, index, mish in entries:
        items += ("\t\t\t<dict>\n\t\t\t\t<key>Attributes</key>\n\t\t\t\t<string>0x0050</string>\n"
                  "\t\t\t\t<key>CFName</key>\n\t\t\t\t<string>%s</string>\n"
                  "\t\t\t\t<key>Data</key>\n\t\t\t\t<data>\n\t\t\t\t%s\n\t\t\t\t</data>\n"
                  "\t\t\t\t<key>ID</key>\n\t\t\t\t<string>%d</string>\n"
                  "\t\t\t\t<key>Name</key>\n\t\t\t\t<string>%s</string>\n\t\t\t</dict>\n"
                  % (name, base64.b64encode(mish).decode(), index, name))
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
           '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
           '<plist version="1.0">\n<dict>\n\t<key>resource-fork</key>\n\t<dict>\n'
           '\t\t<key>blkx</key>\n\t\t<array>\n%s\t\t</array>\n\t</dict>\n</dict>\n</plist>\n'
           % items).encode()
    koly = struct.pack(">4sIIIQQQQQII", b"koly", 4, 512, 1, 0, 0, len(fork), 0, 0, 1, 1)
    koly += hashlib.md5(bytes(fork)).digest()
    koly += crc_field(bytes(fork))
    koly += struct.pack(">QQ", len(fork), len(xml)) + b"\0" * 120
    koly += struct.pack(">II", 2, 32) + struct.pack(">I", zlib.crc32(checks) & 0xFFFFFFFF) + b"\0" * 124
    koly += struct.pack(">IQ", 1, sector) + b"\0" * 12
    assert len(koly) == 512, len(koly)
    return bytes(fork) + xml + koly


def apple_partition_map(volume):
    """A driver map, a partition map, and the volume: the classic layout."""
    map_sectors = 63
    volume_sectors = len(volume) // 512
    total = 1 + map_sectors + volume_sectors
    ddm = struct.pack(">2sHI", b"ER", 512, total) + b"\0" * 504

    def entry(count, start, sectors, name, kind):
        data = struct.pack(">2sHIII", b"PM", 0, count, start, sectors)
        data += name.encode().ljust(32, b"\0") + kind.encode().ljust(32, b"\0")
        data += struct.pack(">II", 0, sectors) + struct.pack(">I", 0x40000033)
        return data.ljust(512, b"\0")

    table = entry(2, 1, map_sectors, "Apple", "Apple_partition_map")
    table += entry(2, 1 + map_sectors, volume_sectors, "disk image", "Apple_HFS")
    table = table.ljust(map_sectors * 512, b"\0")
    return [("Driver Descriptor Map (DDM : 0)", ddm),
            ("Apple (Apple_partition_map : 1)", table),
            ("disk image (Apple_HFS : 2)", volume)]


# ------------------------------------------------------------- cpio and xar

def cpio_odc(entries):
    """entries: list of (name, bytes or None for a directory, mode, link target)."""
    out = bytearray()
    inode = 1
    for name, data, mode, link in entries:
        if link is not None:
            data, kind = link.encode("utf-8"), 0o120000
        elif data is None:
            data, kind = b"", 0o040000
        else:
            kind = 0o100000
        encoded = name.encode("utf-8") + b"\0"
        out += b"070707" + b"%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" % (
            1, inode, kind | mode, 0, 0, 1, 0, NOW - HFS_EPOCH, len(encoded), len(data))
        out += encoded + data
        inode += 1
    trailer = b"TRAILER!!!\0"
    out += b"070707" + b"%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" % (
        0, 0, 0, 0, 0, 1, 0, 0, len(trailer), 0)
    out += trailer
    return bytes(out)


def pbzx(data, chunk=1 << 16):
    """Apple's chunked xz container, used for newer package payloads."""
    out = b"pbzx" + struct.pack(">Q", chunk)
    for start in range(0, len(data), chunk):
        piece = data[start:start + chunk]
        packed = lzma.compress(piece, format=lzma.FORMAT_XZ)
        if len(packed) >= len(piece):
            packed = piece  # stored as it is when xz does not help
        out += struct.pack(">QQ", len(piece), len(packed)) + packed
    return out


def payload_entries(files, prefix="./Applications/"):
    seen = set()
    entries = [(".", None, 0o755, None)]
    for path in sorted(files):
        parts = (prefix + path).split("/")
        for depth in range(2, len(parts)):
            folder = "/".join(parts[:depth])
            if folder not in seen and folder != ".":
                seen.add(folder)
                entries.append((folder, None, 0o755, None))
        data, mode = files[path]
        entries.append((prefix + path, data, mode, None))
    return entries


def xar(tree):
    """tree: list of (name, bytes) or (name, [children]). Returns a .pkg."""
    heap = bytearray(b"\0" * 20)
    counter = [0]

    def emit(items, indent):
        text = ""
        for name, value in items:
            counter[0] += 1
            pad = " " * indent
            if isinstance(value, list):
                text += '%s<file id="%d">\n%s <name>%s</name>\n%s <type>directory</type>\n' % (
                    pad, counter[0], pad, name, pad)
                text += emit(value, indent + 1) + "%s</file>\n" % pad
                continue
            packed = zlib.compress(value, 9)
            offset = len(heap)
            heap.extend(packed)
            text += ('%s<file id="%d">\n%s <name>%s</name>\n%s <type>file</type>\n'
                     '%s <data>\n%s  <length>%d</length>\n%s  <offset>%d</offset>\n%s  <size>%d</size>\n'
                     '%s  <encoding style="application/x-gzip"/>\n'
                     '%s  <archived-checksum style="sha1">%s</archived-checksum>\n'
                     '%s  <extracted-checksum style="sha1">%s</extracted-checksum>\n'
                     '%s </data>\n%s</file>\n' % (
                         pad, counter[0], pad, name, pad, pad, pad, len(packed), pad, offset, pad,
                         len(value), pad, pad, hashlib.sha1(packed).hexdigest(), pad,
                         hashlib.sha1(value).hexdigest(), pad, pad))
        return text

    toc = ('<?xml version="1.0" encoding="UTF-8"?>\n<xar>\n <toc>\n'
           '  <checksum style="sha1">\n   <offset>0</offset>\n   <size>20</size>\n  </checksum>\n'
           + emit(tree, 2) + " </toc>\n</xar>\n").encode()
    packed_toc = zlib.compress(toc, 9)
    heap[0:20] = hashlib.sha1(packed_toc).digest()
    return struct.pack(">IHHQQI", 0x78617221, 28, 1, len(packed_toc), len(toc), 1) + packed_toc + bytes(heap)


def package(title, identifier, files, compress, scripts_marker):
    entries = payload_entries(files)
    archive = cpio_odc(entries)
    # Small chunks, so the fixture has several and some are stored as they are.
    payload = pbzx(archive, 4096) if compress == "pbzx" else gzip.compress(archive, mtime=0)
    bundles = ""
    for path in files:
        if path.endswith(".app/Contents/Info.plist"):
            bundles += ' <bundle path="./Applications/%s" id="%s" CFBundleShortVersionString="1.2"/>\n' % (
                path[:-len("/Contents/Info.plist")], identifier)
    info = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<pkg-info format-version="2" identifier="%s" version="1.2" install-location="/" auth="root">\n'
            ' <payload numberOfFiles="%d" installKBytes="%d"/>\n%s'
            ' <scripts>\n  <postinstall file="./postinstall"/>\n </scripts>\n</pkg-info>\n'
            % (identifier, len(entries), sum(len(d or b"") for _, d, _, _ in entries) // 1024 + 1, bundles)).encode()
    # A script that would leave a mark if anything ever ran it.
    script = ("#!/bin/sh\necho ran > '%s'\n" % scripts_marker).encode()
    scripts = gzip.compress(cpio_odc([(".", None, 0o755, None), ("./postinstall", script, 0o755, None)]), mtime=0)
    distribution = ('<?xml version="1.0" encoding="utf-8"?>\n<installer-gui-script minSpecVersion="1">\n'
                    ' <title>%s</title>\n <pkg-ref id="%s"/>\n'
                    ' <choices-outline>\n  <line choice="default"/>\n </choices-outline>\n'
                    ' <choice id="default"><pkg-ref id="%s"/></choice>\n'
                    ' <pkg-ref id="%s" version="1.2" installKBytes="12">#%s.pkg</pkg-ref>\n'
                    '</installer-gui-script>\n' % (title, identifier, identifier, identifier, title.replace(" ", ""))).encode()
    component = title.replace(" ", "") + ".pkg"
    return xar([("Distribution", distribution),
                (component, [("PackageInfo", info), ("Payload", payload), ("Scripts", scripts)])])


# -------------------------------------------------------------------- main

def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


def wrap(program_path, name, out_path):
    """A disk image around a program that already exists.

    Used where a Mac runtime is installed, to carry one of that runtime's own
    command-line programs through the whole path: disk image, Install,
    Launchpad entry, launch. The program is copied as it is.
    """
    with open(program_path, "rb") as handle:
        program = handle.read()
    files = bundle_files(name, [CPU_X86_64], program=program)
    if out_path.endswith(".pkg"):
        identifier = "example.olympium." + name.replace(" ", "").lower()
        write(out_path, package(name, identifier, files, "pbzx", os.devnull))
    else:
        blocks = max(1024, (sum(len(d) for d, _ in files.values()) // BLOCK) * 3 // 2 + 256)
        volume = Hfs(name, total_blocks=blocks)
        for path, (data, mode) in files.items():
            volume.add_file(path, data, mode)
        write(out_path, udif([("whole disk (Apple_HFS : 0)", volume.build())]))
    print("wrapped", program_path, "as", out_path)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--wrap":
        wrap(sys.argv[2], sys.argv[3], sys.argv[4])
        return
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = os.path.abspath(sys.argv[1])
    os.makedirs(out, exist_ok=True)
    marker = os.path.join(out, "postinstall-ran")

    app = bundle_files("Desk Mac", [CPU_X86_64, CPU_ARM64])
    extras = {"README.txt": (b"Drag Desk Mac to Applications.\n", 0o644)}
    links = [("Applications", "/Applications")]
    volume = hfs_volume("Desk Mac", {**app, **extras}, links)
    write(os.path.join(out, "DeskMac.hfs"), volume)
    write(os.path.join(out, "DeskMac.dmg"), udif([("whole disk (Apple_HFS : 0)", volume)]))
    write(os.path.join(out, "DeskMacApm.dmg"), udif(apple_partition_map(volume)))
    write(os.path.join(out, "DeskMacBz.dmg"), udif([("whole disk (Apple_HFS : 0)", volume)], "bz2"))

    # Two volumes in one image. The archiver lists them as two files and
    # opens neither by itself, so the desktop has to look inside.
    second = hfs_volume("Second", {"note.txt": (b"second volume\n", 0o644)})
    write(os.path.join(out, "DeskMacTwo.dmg"),
          udif([("first (Apple_HFS : 0)", second), ("second (Apple_HFS : 1)", volume)]))

    # A larger one, so there is time to stop an install half way. The bulk
    # is noise that does not compress, made from a fixed seed.
    noise = hashlib.shake_256(b"olympium desk big").digest(24 * 1024 * 1024)
    big = dict(bundle_files("Desk Big", [CPU_X86_64, CPU_ARM64]))
    big["Desk Big.app/Contents/Resources/bulk.bin"] = (noise, 0o644)
    big_volume = Hfs("Desk Big", total_blocks=8192)
    for path, (data, mode) in big.items():
        big_volume.add_file(path, data, mode)
    write(os.path.join(out, "DeskBig.dmg"), udif([("whole disk (Apple_HFS : 0)", big_volume.build())]))

    # A Chinese name for the volume, the application, and its program.
    cjk = bundle_files("\u8ba1\u7b97\u5668", [CPU_X86_64, CPU_ARM64])
    write(os.path.join(out, "DeskCjk.dmg"),
          udif([("whole disk (Apple_HFS : 0)", hfs_volume("\u8ba1\u7b97\u5668", cjk))]))

    arm = bundle_files("Desk Arm", [CPU_ARM64])
    write(os.path.join(out, "DeskArm.dmg"),
          udif([("whole disk (Apple_HFS : 0)", hfs_volume("Desk Arm", arm))]))

    packaged = bundle_files("Desk Mac", [CPU_X86_64, CPU_ARM64], binary_plist=True)
    write(os.path.join(out, "DeskMac.pkg"),
          package("Desk Mac", "example.olympium.deskmac", packaged, "gzip", marker))
    write(os.path.join(out, "DeskMacPbzx.pkg"),
          package("Desk Mac", "example.olympium.deskmac", packaged, "pbzx", marker))
    write(os.path.join(out, "DeskTool.pkg"),
          package("Desk Tool", "example.olympium.desktool",
                  {"desk-tool": (b"#!/bin/sh\necho tool\n", 0o755)}, "gzip", marker))

    # A hostile volume: a link that points outside, with a file filed under it.
    hostile = Hfs("Evil")
    for path, (data, mode) in bundle_files("Evil", [CPU_X86_64]).items():
        hostile.add_file(path, data, mode)
    escape = os.path.join(out, "escape")
    link = hostile.add_link("way-out", escape)
    hostile.add_under(link, "planted.txt", b"written through a link\n")
    hostile.add_link("Evil.app/Contents/Resources/secret", "/etc/hostname")
    write(os.path.join(out, "Evil.dmg"), udif([("whole disk (Apple_HFS : 0)", hostile.build())]))

    evil_files = bundle_files("Evil", [CPU_X86_64])
    entries = payload_entries(evil_files)
    entries.append(("./way-out", None, 0o777, escape))
    entries.append(("./way-out/planted.txt", b"written through a link\n", 0o644, None))
    entries.append(("../../../../../../../../" + escape.lstrip("/") + "/dotdot.txt", b"dot dot\n", 0o644, None))
    entries.append((escape + "/absolute.txt", b"absolute\n", 0o644, None))
    info = ('<?xml version="1.0" encoding="utf-8"?>\n<pkg-info format-version="2" '
            'identifier="example.olympium.evil" version="1.0" install-location="/"/>\n').encode()
    write(os.path.join(out, "Evil.pkg"),
          xar([("PackageInfo", info), ("Payload", gzip.compress(cpio_odc(entries), mtime=0))]))

    root = os.path.join(out, "plain")
    for path, (data, mode) in app.items():
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        write(full, data)
        os.chmod(full, mode)
    print("fixtures in", out)


if __name__ == "__main__":
    main()
