#!/usr/bin/env python3
r"""Add or remove the Material Canvas preview latency probes.

The preview's edit-to-viewport time is spread across three threads and two processes, and none of the
timers that answer "where did that go" belong in the shipped code -- they print on every edit and several
of them need extra members to hold a start time. This adds them on demand and takes them out again.

    python z_preview_instrument.py --check              what is currently in the tree
    python z_preview_instrument.py --add                add every probe
    python z_preview_instrument.py --add tick apply     add only those
    python z_preview_instrument.py --revert             take them all out again

Each probe is a set of exact before/after text pairs rather than sentinel comments, so reverting restores
the original bytes and any drift fails loudly instead of half-applying. That is deliberate: see
z_PREVIEW_PERFORMANCE.md section 7.12 for what reverting instrumentation by re-editing nearly shipped.

WHAT THEY MEASURE, and what a healthy reading looked like on 2026-09-06 (one preview shader, transparent
standard lighting, window focused):

    graph-compile     OnCompileGraphStarted -> OnCompileGraphCompleted.              98-112 ms
    compile-phases    that number split five ways. "instructions" is nearly all      tables 3, load 1,
                      of it and is the one worth watching.                           preprocess 1,
                                                                                     instructions 68-78,
                                                                                     export 24-47
    shader-compile    the in-process MCPP -> azslc -> dxc -> dxsc chain, and how     658-792 ms,
                      long after the graph compile it landed.                        finishing 0-20 ms after
    apply-entry       when ApplyMaterial actually started, as against when the       2-5 ms after the
                      result it needs became available.                              shader finished
    apply-deferred    says the apply was skipped because a compile is in flight.     once per edit
    tick              any system tick longer than 50 ms. An Atom tool that is not    silent when focused;
                      the foreground window runs at 4 fps -- see trap 7.15 -- and    a steady 254 ms when
                      that shows up as a fixed cost in everything above.             not

Read the results out of user/log/MaterialCanvas.log.
"""

import argparse
import io
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

VIEWPORT_CPP = os.path.join(
    ROOT, "Gems", "Atom", "Tools", "MaterialCanvas", "Code", "Source", "Window", "MaterialCanvasViewportContent.cpp")
VIEWPORT_H = os.path.join(
    ROOT, "Gems", "Atom", "Tools", "MaterialCanvas", "Code", "Source", "Window", "MaterialCanvasViewportContent.h")
COMPILER_CPP = os.path.join(
    ROOT, "Gems", "Atom", "Tools", "MaterialCanvas", "Code", "Source", "Document", "MaterialGraphCompiler.cpp")


# Each entry is (path, clean, instrumented). Order within a probe does not matter; every pair is applied
# independently and every one has to match exactly once.
PROBES = {}

PROBES["graph-compile"] = [
    (VIEWPORT_H,
     r"""        AZStd::chrono::steady_clock::time_point m_compileCompletedAt = AZStd::chrono::steady_clock::now();
""",
     r"""        AZStd::chrono::steady_clock::time_point m_compileCompletedAt = AZStd::chrono::steady_clock::now();

        //! When the graph compile that produced the current material started.
        //!
        //! Everything else here is measured from the compile *finishing*, which quietly leaves out however long the graph
        //! compiler itself took -- and a stopwatch held against the screen does not. Timing from the start is the only way to
        //! see the whole edit-to-viewport latency rather than the part after the interesting bit.
        AZStd::chrono::steady_clock::time_point m_compileStartedAt = AZStd::chrono::steady_clock::now();
"""),
    (VIEWPORT_CPP,
     r"""            // discard its own result rather than apply it to this edit.
            ++m_compileGeneration;
        }
""",
     r"""            // discard its own result rather than apply it to this edit.
            ++m_compileGeneration;
            m_compileStartedAt = AZStd::chrono::steady_clock::now();
        }
"""),
    (VIEWPORT_CPP,
     r"""        m_compileCompletedAt = AZStd::chrono::steady_clock::now();

        if (m_lastOpenedDocumentId != documentId)
""",
     r"""        m_compileCompletedAt = AZStd::chrono::steady_clock::now();

        AZ_TracePrintf(
            "MaterialCanvas", "Graph compile took %.0f ms before the viewport saw anything.\n",
            AZStd::chrono::duration<double, AZStd::milli>(m_compileCompletedAt - m_compileStartedAt).count());

        if (m_lastOpenedDocumentId != documentId)
"""),
]

PROBES["apply-entry"] = [
    (VIEWPORT_CPP,
     r"""        m_appliedDocumentId = documentId;
        m_appliedMaterialAssetId = GetGeneratedAssetId(documentId, ".material");
""",
     r"""        AZ_TracePrintf(
            "MaterialCanvas", "ApplyMaterial entered %.0f ms after the compile finished.\n", MillisecondsSinceCompile());

        m_appliedDocumentId = documentId;
        m_appliedMaterialAssetId = GetGeneratedAssetId(documentId, ".material");
"""),
]

PROBES["apply-deferred"] = [
    (VIEWPORT_CPP,
     r"""        if (m_shaderCompileInFlight)
        {
            return;
        }
""",
     r"""        if (m_shaderCompileInFlight)
        {
            AZ_TracePrintf(
                "MaterialCanvas", "Apply deferred: the shader for this edit is still compiling.\n");
            return;
        }
"""),
]

PROBES["shader-compile"] = [
    (VIEWPORT_CPP,
     r"""                auto compiled = CompileInMemoryShaders(requests);
""",
     r"""                const auto compileStart = AZStd::chrono::steady_clock::now();
                auto compiled = CompileInMemoryShaders(requests);
"""),
    (VIEWPORT_CPP,
     r"""                {
                    AZStd::scoped_lock lock(m_compiledShadersMutex);
""",
     r"""                const double compileMs =
                    AZStd::chrono::duration<double, AZStd::milli>(AZStd::chrono::steady_clock::now() - compileStart).count();

                AZ_TracePrintf(
                    "MaterialCanvas", "In-memory shader compile took %.0f ms for %zu shader(s), finishing %.0f ms after the compile.\n",
                    compileMs, compiled.size(), MillisecondsSinceCompile());

                {
                    AZStd::scoped_lock lock(m_compiledShadersMutex);
"""),
]

PROBES["tick"] = [
    (VIEWPORT_H,
     r"""        AZ::Data::AssetId GetGeneratedAssetId(const AZ::Uuid& documentId, AZStd::string_view extension) const;
""",
     r"""        //! When the previous system tick ran, so a tick that took an unreasonably long time can say so.
        //!
        //! The apply is scheduled on a tick, so the main loop's frame time is a floor on how quickly anything this compiles can reach
        //! the screen. A shader that was ready 253 ms before the apply ran did not lose that time to work: ApplyMaterial itself takes
        //! 7-18 ms. It lost it to waiting for a turn.
        AZStd::chrono::steady_clock::time_point m_lastSystemTickAt = AZStd::chrono::steady_clock::now();

        AZ::Data::AssetId GetGeneratedAssetId(const AZ::Uuid& documentId, AZStd::string_view extension) const;
"""),
    (VIEWPORT_CPP,
     r"""        const auto now = AZStd::chrono::steady_clock::now();

        // Cheap enough to run as soon as it is asked for:""",
     r"""        const auto now = AZStd::chrono::steady_clock::now();

        if (const double sinceLastTick = AZStd::chrono::duration<double, AZStd::milli>(now - m_lastSystemTickAt).count();
            sinceLastTick > 50.0)
        {
            AZ_TracePrintf("MaterialCanvas", "Slow tick: %.0f ms since the last one.\n", sinceLastTick);
        }
        m_lastSystemTickAt = now;

        // Cheap enough to run as soon as it is asked for:"""),
]

PROBES["compile-phases"] = [
    (COMPILER_CPP,
     r"""        m_writtenGeneratedFiles.clear();
        m_onlyMaterialPropertyValuesChanged = true;
""",
     r"""        m_writtenGeneratedFiles.clear();

        // Phase timings for the compile itself, as opposed to the wait that used to dominate it. Everything below happens before the
        // viewport can start on the shader, so it is a floor on how quickly an edit can reach the screen.
        double slotAndDependencyMs = 0.0;
        double loadTemplatesMs = 0.0;
        double preprocessMs = 0.0;
        double instructionsMs = 0.0;
        double exportMs = 0.0;
        const auto phaseStart = AZStd::chrono::steady_clock::now();
        auto timePhase = [](double& accumulator, auto&& phase)
        {
            const auto start = AZStd::chrono::steady_clock::now();
            auto result = phase();
            accumulator += AZStd::chrono::duration<double, AZStd::milli>(AZStd::chrono::steady_clock::now() - start).count();
            return result;
        };
        auto timeVoidPhase = [](double& accumulator, auto&& phase)
        {
            const auto start = AZStd::chrono::steady_clock::now();
            phase();
            accumulator += AZStd::chrono::duration<double, AZStd::milli>(AZStd::chrono::steady_clock::now() - start).count();
        };
        m_onlyMaterialPropertyValuesChanged = true;
"""),
    (COMPILER_CPP,
     r"""        BuildSlotValueTable();
        BuildDependencyTables();
""",
     r"""        timeVoidPhase(slotAndDependencyMs, [this]() { BuildSlotValueTable(); BuildDependencyTables(); });
"""),
    (COMPILER_CPP,
     r"""            if (!LoadTemplatesForCurrentNode())
""",
     r"""            if (!timePhase(loadTemplatesMs, [this]() { return LoadTemplatesForCurrentNode(); }))
"""),
    (COMPILER_CPP,
     r"""            PreprocessTemplatesForCurrentNode();
""",
     r"""            timeVoidPhase(preprocessMs, [this]() { PreprocessTemplatesForCurrentNode(); });
"""),
    (COMPILER_CPP,
     r"""            BuildInstructionsForCurrentNode(currentNode);
""",
     r"""            timeVoidPhase(instructionsMs, [this, &currentNode]() { BuildInstructionsForCurrentNode(currentNode); });
"""),
    (COMPILER_CPP,
     r"""                if (!ExportOutputSetForCurrentNode(currentNode, outputSet))
""",
     r"""                if (!timePhase(exportMs, [this, &currentNode, outputSet]() { return ExportOutputSetForCurrentNode(currentNode, outputSet); }))
"""),
    (COMPILER_CPP,
     r"""
        // Only wait on the Asset Processor when this compile produced something the viewport cannot resolve on its own.""",
     r"""
        AZ_TracePrintf(
            "MaterialGraphCompiler",
            "Compile phases: %.0f ms total -- tables %.0f, load templates %.0f, preprocess %.0f, instructions %.0f, export %.0f.\n",
            AZStd::chrono::duration<double, AZStd::milli>(AZStd::chrono::steady_clock::now() - phaseStart).count(),
            slotAndDependencyMs,
            loadTemplatesMs,
            preprocessMs,
            instructionsMs,
            exportMs);

        // Only wait on the Asset Processor when this compile produced something the viewport cannot resolve on its own."""),
]


def read(path):
    raw = io.open(path, "rb").read()
    return raw.decode("utf-8"), raw.count(b"\r\n") > 0


def write(path, text, crlf):
    io.open(path, "wb").write(text.replace("\r\n", "\n").replace("\n", "\r\n" if crlf else "\n").encode("utf-8"))


def normalize(text):
    return text.replace("\r\n", "\n")


def state_of(probe):
    """present / absent / partial / drifted, plus a per-pair breakdown."""
    present = 0
    absent = 0
    broken = []
    for path, clean, instrumented in PROBES[probe]:
        if not os.path.exists(path):
            broken.append("%s: file not found" % os.path.basename(path))
            continue
        text = normalize(read(path)[0])
        # Instrumented first: clean is often a substring of instrumented.
        n_instrumented = text.count(normalize(instrumented))
        if n_instrumented == 1:
            present += 1
            continue
        n_clean = text.count(normalize(clean))
        if n_clean == 1:
            absent += 1
        else:
            broken.append("%s: matched clean %d, instrumented %d" % (os.path.basename(path), n_clean, n_instrumented))

    if broken:
        return "drifted", broken
    if present and absent:
        return "partial", []
    return ("present" if present else "absent"), []


def apply(probe, add):
    state, broken = state_of(probe)
    if state == "drifted":
        print("  %-15s DRIFTED -- not touching it" % probe)
        for line in broken:
            print("      %s" % line)
        return False
    if state == "present" and add:
        print("  %-15s already present" % probe)
        return True
    if state == "absent" and not add:
        print("  %-15s already absent" % probe)
        return True

    for path, clean, instrumented in PROBES[probe]:
        text, crlf = read(path)
        text = normalize(text)
        old, new = (clean, instrumented) if add else (instrumented, clean)
        old, new = normalize(old), normalize(new)
        if text.count(old) != 1:
            # A partial probe: this pair is already on the target side.
            continue
        write(path, text.replace(old, new), crlf)

    state, _ = state_of(probe)
    ok = state == ("present" if add else "absent")
    print("  %-15s %s" % (probe, "added" if add else "removed") if ok else "  %-15s FAILED (now %s)" % (probe, state))
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--add", action="store_true", help="insert the probes")
    group.add_argument("--revert", action="store_true", help="remove the probes")
    group.add_argument("--check", action="store_true", help="report what is in the tree")
    parser.add_argument("probes", nargs="*", choices=sorted(PROBES) + [[]], help="probes to act on (default: all)")
    args = parser.parse_args()

    names = args.probes or sorted(PROBES)

    if args.check:
        for name in names:
            state, broken = state_of(name)
            print("  %-15s %s" % (name, state))
            for line in broken:
                print("      %s" % line)
        return 0

    ok = True
    for name in names:
        ok = apply(name, args.add) and ok

    if not ok:
        print("\nSomething did not apply cleanly. Restore from git rather than re-editing by hand:")
        print("  git checkout -- Gems/Atom/Tools/MaterialCanvas/Code/Source")
        return 1

    print("\nRebuild MaterialCanvas, then read user/log/MaterialCanvas.log.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
