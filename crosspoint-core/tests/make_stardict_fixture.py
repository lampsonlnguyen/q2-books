#!/usr/bin/env python3
"""StarDict fixtures for the DI-01/DI-02 tests (tests/stardict_test.cpp).

  plain/    version 2.4.2, .dict, sametypesequence=m, .syn synonyms,
            Vietnamese headwords, 3000 entries
  zipped/   version 3.0.0, idxoffsetbits=64, dictzip .dict.dz (4 KiB chunks,
            definitions that cross chunks), typed fields (h then m), no
            sametypesequence, 1200 entries
Words are sorted as StarDict sorts them (ASCII case-insensitive, then bytes).
"""
import functools
import os
import struct
import sys
import zlib


def stardict_cmp(a: bytes, b: bytes) -> int:
    fa, fb = a.lower(), b.lower()  # bytes.lower() folds ASCII only, as g_ascii_strcasecmp
    if fa != fb:
        return -1 if fa < fb else 1
    return (a > b) - (a < b)


def write_ifo(path, **fields):
    with open(path, "w", encoding="utf-8") as f:
        f.write("StarDict's dict ifo file\n")
        for k, v in fields.items():
            f.write(f"{k}={v}\n")


def dictzip(data: bytes, chunk=4096) -> bytes:
    chunks = []
    for i in range(0, len(data), chunk):  # noqa: one compressor per chunk
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        piece = c.compress(data[i:i + chunk])
        piece += c.flush(zlib.Z_FULL_FLUSH if i + chunk < len(data) else zlib.Z_FINISH)
        chunks.append(piece)
    # Each chunk is its own raw deflate stream here; the reader inflates each
    # on its own, which is what dictzip's full-flush chunks allow.
    ra = struct.pack("<HHH", 1, chunk, len(chunks)) + b"".join(struct.pack("<H", len(c)) for c in chunks)
    extra = b"RA" + struct.pack("<H", len(ra)) + ra
    header = b"\x1f\x8b\x08\x04" + b"\x00\x00\x00\x00" + b"\x02\x03" + struct.pack("<H", len(extra)) + extra
    trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)
    return header + b"".join(chunks) + trailer


def build(out, name, entries, *, version, bits, sametype, zipped, synonyms=()):
    os.makedirs(out, exist_ok=True)
    base = os.path.join(out, name)
    entries = sorted(entries, key=functools.cmp_to_key(lambda x, y: stardict_cmp(x[0], y[0])))
    data, idx, offset = [], [], 0
    fmt = ">QI" if bits == 64 else ">II"
    for word, body in entries:
        idx.append(word + b"\0" + struct.pack(fmt, offset, len(body)))
        data.append(body)
        offset += len(body)
    data, idx = b"".join(data), b"".join(idx)
    with open(base + ".idx", "wb") as f:
        f.write(idx)
    if zipped:
        with open(base + ".dict.dz", "wb") as f:
            f.write(dictzip(data))
    else:
        with open(base + ".dict", "wb") as f:
            f.write(data)
    fields = dict(version=version, bookname=name.capitalize() + " test dictionary",
                  wordcount=len(entries), idxfilesize=len(idx))
    if bits == 64:
        fields["idxoffsetbits"] = 64
    if sametype:
        fields["sametypesequence"] = sametype
    if synonyms:
        order = {w: i for i, (w, _) in enumerate(entries)}
        syn = sorted(((s, order[t]) for s, t in synonyms),
                     key=functools.cmp_to_key(lambda x, y: stardict_cmp(x[0], y[0])))
        with open(base + ".syn", "wb") as f:
            for s, i in syn:
                f.write(s + b"\0" + struct.pack(">I", i))
        fields["synwordcount"] = len(syn)
    write_ifo(base + ".ifo", **fields)


def main(out):
    plain = [(f"word{i:04d}".encode(), f"Definition number {i} of the plain dictionary.".encode())
             for i in range(2990)]
    plain += [("apple".encode(), b"A round fruit."),
              ("Apple".encode(), b"A technology company."),
              ("sách".encode(), "Quyển sách (book).".encode()),
              ("Đà Lạt".encode(), "Thành phố ở Lâm Đồng.".encode()),
              ("zebra".encode(), b"A striped animal."),
              ("aardvark".encode(), b"The first word."),
              ("zzz".encode(), b"The last word."),
              ("colour".encode(), b"British spelling of color."),
              ("đẹp".encode(), "Beautiful.".encode()),
              ("中文".encode(), "Chinese language.".encode())]
    build(os.path.join(out, "plain"), "plain", plain, version="2.4.2", bits=32,
          sametype="m", zipped=False,
          synonyms=[(b"color", b"colour"), (b"pomme", b"apple"), (b"book", "sách".encode())])
    long_text = ("Long definition that crosses dictzip chunks. " * 300).encode()
    zipped = [(f"entry{i:04d}".encode(), b"h<b>Bold</b> &amp; text " + str(i).encode() + b"\0m plain " + str(i).encode() + b"\0")
              for i in range(1199)]
    zipped += [(b"longword", b"m" + long_text + b"\0")]
    build(os.path.join(out, "zipped"), "zipped", zipped, version="3.0.0", bits=64,
          sametype=None, zipped=True)


if __name__ == "__main__":
    main(sys.argv[1])
