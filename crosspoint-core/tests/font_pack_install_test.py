#!/usr/bin/env python3
"""Test staging, safe extraction and preservation of existing font files."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import zipfile

reader=sys.argv[1]
subprocess.run([reader,"--test-font-pack-sha"],check=True)
with tempfile.TemporaryDirectory() as tmp:
    base=Path(tmp)
    state=base/"state"
    state.mkdir()
    font=state/"fonts"
    env=dict(os.environ,BOOKS_STATE_DIR=str(state))
    payload=b"CPFONT\0\0"+b"\0"*128
    ttf=b"\0\1\0\0"+b"\0"*128

    def make_zip(path, bad=None, omit=None):
        with zipfile.ZipFile(path,"w",zipfile.ZIP_DEFLATED) as archive:
            for pt in range(8,15):
                if pt!=omit:
                    archive.writestr(f"serif-{pt}.cpfont",payload)
            archive.writestr("ttf/serif-regular.ttf",ttf)
            archive.writestr("licenses/serif.txt","OFL test fixture")
            if bad:
                archive.writestr(bad,b"bad")
    def run(path, success):
        result=subprocess.run([reader,"--test-font-pack-install",str(path)],env=env)
        assert (result.returncode==0)==success, result.returncode
        assert not list(font.glob(".font-pack-*")), "staging directory leaked"

    zip_path=base/"font.zip"
    make_zip(zip_path,"../../outside")
    run(zip_path,False)
    assert not (base/"outside").exists()
    assert not list(font.glob("*.cpfont"))
    make_zip(zip_path,omit=14)
    run(zip_path,False)
    assert not list(font.glob("*.cpfont"))
    # Preserve an existing different valid file (older installation).
    old=font/"serif-12.cpfont"
    old.write_bytes(payload+b"old")
    make_zip(zip_path)
    run(zip_path,True)
    assert old.read_bytes()==payload+b"old"
    assert len(list(font.glob("serif-*.cpfont")))==7
    assert (font/"ttf/serif-regular.ttf").read_bytes()==ttf
    assert (font/"licenses/serif.txt").is_file()
    run(zip_path,True)
    assert old.read_bytes()==payload+b"old"
    # Invalid existing file isn't overwritten and other files aren't installed.
    old.write_bytes(b"not a font")
    (font/"serif-8.cpfont").unlink()
    run(zip_path,False)
    assert old.read_bytes()==b"not a font"
    assert not (font/"serif-8.cpfont").exists()
    # Remove only the chosen pack, including older/partial installations.
    core=font/"sans-12.cpfont"
    core.write_bytes(payload)
    other=font/"inter-12.cpfont"
    other.write_bytes(payload)
    (state/"typography.conf").write_text("BOOKS-TYPE1 serif 10\n")
    subprocess.run([reader,"--test-font-pack-remove","serif"],env=env,check=True)
    assert not list(font.glob("serif-*.cpfont"))
    assert not (font/"ttf/serif-regular.ttf").exists()
    assert not (font/"licenses/serif.txt").exists()
    assert core.read_bytes()==payload and other.read_bytes()==payload
    assert (state/"typography.conf").read_text()=="BOOKS-TYPE1 sans 10\n"
    assert subprocess.run([reader,"--test-font-pack-remove","sans"],env=env).returncode!=0
    assert core.read_bytes()==payload
print("Font pack installation, removal, integrity primitives and preservation: PASS")
