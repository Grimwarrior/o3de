#!/usr/bin/env python3
"""
Regenerate the AZSL parser from the .g4 grammars, mirroring Grammar/Generate.ps1 exactly.

Exists because Generate.ps1 needs Java on the Windows PATH. This runs the same jar with the same flags and applies
the same post-processing, so the output is what Generate.ps1 would have produced:

    java -jar antlr4.jar -Dlanguage=Cpp -o <grammarDir> -listener -no-visitor  azslLexer.g4
    java -jar antlr4.jar -Dlanguage=Cpp -o <grammarDir> -listener -no-visitor -lib <grammarDir> azslParser.g4

then, for each generated source: tabs to four spaces, the O3DE copyright header prepended, UTF-8 without BOM. The
.interp and .tokens files ANTLR leaves behind are deleted.

Generates into a temp directory first and reports what changed before touching the checked-in files, because the
eight generated files are ~900KB and a silent difference in tool version or flags is otherwise invisible.

    python3 z_azslc_generate.py --dry-run     # generate and diff, write nothing
    python3 z_azslc_generate.py               # generate and install
"""

import argparse, hashlib, pathlib, shutil, subprocess, sys, tempfile

JAR_SHA256 = "eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76"
GENERATED = [
    "azslLexer.cpp", "azslLexer.h",
    "azslParser.cpp", "azslParser.h",
    "azslParserBaseListener.cpp", "azslParserBaseListener.h",
    "azslParserListener.cpp", "azslParserListener.h",
]
HEADER = "\n".join([
    "// Copyright (c) Contributors to the Open 3D Engine Project.",
    "// For complete copyright and license terms please see the LICENSE at the root of this distribution.",
    "//",
    "// SPDX-License-Identifier: Apache-2.0 OR MIT",
    "//",
    "",
])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gdir = pathlib.Path(args.root) / "Code/Tools/ShaderCompiler/Source/Grammar"
    jar = gdir / "antlr4.jar"
    if not jar.is_file():
        sys.exit(f"ANTLR jar not found at {jar}\n"
                 "Download it on the Windows side:\n"
                 "  Invoke-WebRequest -Uri https://www.antlr.org/download/antlr-4.13.2-complete.jar "
                 "-OutFile .\\Code\\Tools\\ShaderCompiler\\Source\\Grammar\\antlr4.jar")

    digest = hashlib.sha256(jar.read_bytes()).hexdigest()
    if digest != JAR_SHA256:
        sys.exit(f"jar sha256 mismatch\n  got      {digest}\n  expected {JAR_SHA256}\n"
                 "Generate.ps1 pins this hash; a different jar is a different ANTLR.")
    print(f"jar verified ({jar.stat().st_size:,} bytes)")

    out = pathlib.Path(tempfile.mkdtemp(prefix="azslgen_"))
    for grammar, extra in (("azslLexer.g4", []), ("azslParser.g4", ["-lib", str(out)])):
        # The lexer is generated first and the parser is given -lib pointing at it, because azslParser.g4 imports
        # the lexer's token vocabulary. Pointed at the temp dir, not the source dir, so a stale .tokens from an
        # earlier run cannot leak into this one.
        shutil.copy(gdir / grammar, out / grammar)
        # Bare filename, run from inside the output directory. ANTLR bakes the grammar path it is given into a
        # "// Generated from <path> by ANTLR" comment at the top of every file, so passing an absolute path would
        # write this machine's temp directory into eight checked-in source files. Generate.ps1 avoids that by
        # Push-Location into the grammar dir and passing (Split-Path $grammar -Leaf); this matches it.
        cmd = ["java", "-jar", str(jar), "-Dlanguage=Cpp", "-o", str(out),
               "-listener", "-no-visitor"] + extra + [grammar]
        print("  " + " ".join(["java", "-jar", jar.name] + cmd[3:]))
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=str(out))
        if r.returncode != 0:
            print(r.stdout); print(r.stderr, file=sys.stderr)
            sys.exit(f"ANTLR failed on {grammar} (exit {r.returncode})")
        if r.stderr.strip():
            print("  ANTLR messages:"); print("    " + r.stderr.strip().replace("\n", "\n    "))

    changed, same, missing = [], [], []
    for name in GENERATED:
        src = out / name
        if not src.is_file():
            missing.append(name); continue
        # Generate.ps1: tabs to four spaces, header prepended, UTF-8 no BOM.
        text = HEADER + src.read_text(encoding="utf-8").replace("\t", "    ")
        dst = gdir / name
        old = dst.read_text(encoding="utf-8", errors="surrogateescape") if dst.is_file() else ""
        # Compare ignoring line endings: the worktree is CRLF, ANTLR emits LF.
        if old.replace("\r\n", "\n") == text.replace("\r\n", "\n"):
            same.append(name)
        else:
            changed.append((name, len(old.splitlines()), len(text.splitlines())))
        src.write_text(text, encoding="utf-8", newline="")

    if missing:
        sys.exit("ANTLR did not produce: " + ", ".join(missing))

    print()
    for name in same:
        print(f"  unchanged  {name}")
    for name, a, b in changed:
        print(f"  CHANGED    {name}   {a:,} -> {b:,} lines")
    print()

    if args.dry_run:
        print(f"dry run; generated files left in {out}")
        return
    if not changed:
        print("Nothing to install: output is identical to what is checked in.")
        return

    for name in GENERATED:
        # Write LF; the tree's CRLF is a working-copy artifact, and git stores LF.
        (gdir / name).write_bytes((out / name).read_text(encoding="utf-8").encode("utf-8"))
    for stray in list(gdir.glob("*.interp")) + list(gdir.glob("*.tokens")):
        stray.unlink()
    print(f"Installed {len(changed)} regenerated file(s) into {gdir}")
    print("Rebuild AZSLc, then check the emitted HLSL is byte identical before trusting the timings.")


if __name__ == "__main__":
    main()
