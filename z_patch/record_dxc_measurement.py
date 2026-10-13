import io, os, sys, glob

ROOT = os.environ["HOME"] + "/mnt/o3de/Gems/Atom/Tools/MaterialCanvas/Assets/MaterialCanvas/Pipelines/PreviewOnly/ShaderTemplates"


def read(p):
    with io.open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(p, s):
    with io.open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def eol_of(s):
    return "\r\n" if "\r\n" in s else "\n"


# ---------------------------------------------------------------- templates
old_tpl_lines = [
    "    // -O1 rather than -O0: DXC's -O0 can defeat resource index resolution and produce DXIL that fails validation on shaders like",
    "    // these. If -O1 is not enough, -O0 is the next thing to try, but verify the preview still renders before keeping it.",
]
new_tpl_lines = [
    "    // -O1 rather than -O0, and -O0 is not the next thing to try: it is slower. Measured on the preview transparent pixel shader,",
    "    // -O0 costs 240 ms against -O1's 209 ms, because it emits 65 KB of DXIL where -O1 emits 37 KB and the extra module costs more",
    "    // to write, validate and sign than the optimiser passes saved. See \"Investigated and rejected\" in README.md.",
]

changed = 0
for path in sorted(glob.glob(os.path.join(ROOT, "*.shader.template"))):
    data = read(path)
    eol = eol_of(data)
    old = eol.join(old_tpl_lines)
    new = eol.join(new_tpl_lines)
    n = data.count(old)
    if n == 0:
        print("skip (no match): %s" % os.path.basename(path))
        continue
    if n != 1:
        print("FAIL %s: %d matches" % (os.path.basename(path), n))
        sys.exit(1)
    write(path, data.replace(old, new, 1))
    changed += 1
    print("updated %s" % os.path.basename(path))

print("templates updated: %d" % changed)

# ---------------------------------------------------------------- README
readme = os.path.join(ROOT, "README.md")
data = read(readme)
eol = eol_of(data)

anchor = "### Investigated and rejected" + eol + eol + \
    "Two further cuts look obvious from the attribution table and are not worth making. Both were measured; this section exists so" + eol + \
    "the next person does not spend the afternoon rediscovering them."

replacement = "### Investigated and rejected" + eol + eol + \
    "Four cuts look obvious and are not worth making. All were measured; this section exists so the next person does not spend the" + eol + \
    "afternoon rediscovering them."

if data.count(anchor) != 1:
    print("FAIL: README anchor not found exactly once (%d)" % data.count(anchor))
    sys.exit(1)
data = data.replace(anchor, replacement, 1)

# Insert the two new entries before the "azslc cost tracks input size" paragraph.
tail_anchor = "azslc cost tracks input size and does so slightly worse than linearly:"
if data.count(tail_anchor) != 1:
    print("FAIL: README tail anchor not found exactly once")
    sys.exit(1)

new_entries = eol.join([
    "**Lowering DXC's optimisation level to `-O0`** is slower than `-O1`, not faster. On the preview transparent pixel shader",
    "(4,282 lines of generated HLSL) the pixel stage measures 209 ms at `-O1` and 240 ms at `-O0`, both with process spawn",
    "subtracted. The object code explains it: `-O1` emits 37,252 bytes of DXIL and `-O0` emits 65,276. DXC still runs its front end",
    "and DXIL emission at `-O0`, and writing, validating and signing 75% more module costs more than the optimiser passes it skipped.",
    "The templates therefore keep `-O1`, which is already well below DXC's `-O3` default and is where the saving actually is.",
    "",
    "**Dropping `-Fh`** saves 6 ms of 209, about 3%. DXC writes a full text disassembly of the object code to that file and",
    "`ShaderPlatformInterface.cpp` reads it back for one thing: counting dynamic branches, reported as a job byproduct. It is pure",
    "diagnostic and nothing in the built shader depends on it, so removing it is tempting. It is not worth losing: the dynamic branch",
    "count is what showed the production pixel shader carrying 674 branches against the preview's 72, which is the single most useful",
    "number for deciding whether a graph or the pipeline is responsible for a slow compile. If it is ever removed, make it conditional",
    "rather than deleted.",
    "",
    "Process spawn is cheap and is not worth attacking: a one-line shader compiles end to end in 22 ms, so the five compiler",
    "invocations in a preview shader job cost about 100 ms of process creation between them. An earlier harness in this project",
    "reported 1,017 ms for the same one-line shader; that number was an artefact of the harness and should be ignored.",
    "",
    "",
])

data = data.replace(tail_anchor, new_entries + tail_anchor, 1)
write(readme, data)
print("README.md updated")
