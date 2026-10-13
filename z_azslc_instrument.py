#!/usr/bin/env python3
"""
Add phase timers to AZSLc, or take them out again.

Three files are touched:

  Main.cpp                              top-level phases, plus the timer itself
  AzslcIntermediateRepresentation.cpp   the four steps inside MiddleEnd
  AzslcListener.cpp                     every SemaCheckListener callback (--deep only)

Ordinary mode gives one checkpoint per phase boundary. --deep additionally wraps all 40 listener callbacks in a
per-call-site accumulator, which is what you want once the walk is known to be callback-bound rather than
traversal-bound: it reports total milliseconds and call count per callback, so a slow one and a merely frequent
one are distinguishable.

The accumulator keeps a static record per call site and passes an opaque handle, so the per-call cost is one
function call plus one clock read -- no map lookup, no string hashing. It still is not free: at roughly 30ns a
clock read, a callback invoked 100,000 times adds a few milliseconds to its own measurement. Read --deep numbers
as ranking rather than absolutes, and take the phase total from a non-deep run.

Anchors are regexes on the distinctive call, not exact whitespace. Inserted regions are fenced with sentinels and
single lines are tagged, so --revert is exact -- verified by round trip, not by inspection.

    python3 z_azslc_instrument.py --check
    python3 z_azslc_instrument.py [--deep]
    python3 z_azslc_instrument.py --revert
"""

import argparse, pathlib, re, sys

TAG   = "//__AZSLC_PHASE_TIMER__"
BEGIN = "//__AZSLC_PHASE_TIMER_BEGIN__"
END   = "//__AZSLC_PHASE_TIMER_END__"

MAIN     = "Code/Tools/ShaderCompiler/Source/Main.cpp"
IR       = "Code/Tools/ShaderCompiler/Source/AzslcIntermediateRepresentation.cpp"
LISTENER = "Code/Tools/ShaderCompiler/Source/AzslcListener.cpp"

ANCHORS = {
    MAIN: [
        (r"ANTLRInputStream\s+input\s*\(",                        "read input"),
        (r"^\s*(?:auto allTokens = lexer\.getAllTokens|tokens\.fill)\s*\(", "lex"),
        (r"^\s*ConstructLineMap\s*\(",                            "line map"),
        (r"parser\s*\.\s*compilationUnit\s*\(",                   "parse"),
        (r"walker\s*\.\s*walk\s*\(&semanticListener",             "semantic walk"),
        (r"ir\s*\.\s*MiddleEnd\s*\(",                             "middle end"),
        (r"ir\s*\.\s*Validate\s*\(",                              "validate"),
        (r"emitter\s*\.\s*Run\s*\(",                              "emit HLSL"),
        (r'prepareOutputAndCall\("ia"',                           "refl: ia"),
        (r'prepareOutputAndCall\("om"',                           "refl: om"),
        (r'prepareOutputAndCall\("srg"',                          "refl: srg"),
        (r'prepareOutputAndCall\("options"',                      "refl: options"),
        (r'prepareOutputAndCall\("bindingdep"',                   "refl: bindingdep"),
    ],
    IR: [
        (r"m_symbols\s*\.\s*ReorderBySymbolDependency\s*\(",       "middle: reorder syms"),
        (r"^\s*RegisterRootConstantStruct\s*\(",                   "middle: root constants"),
        (r"RunMutationsForPadToAttributes\s*\(",                   "middle: pad-to"),
        (r"^\s*ValidateAlignmentIssueWhenScalarOrFloat2PrecededByMatrix\s*\(", "middle: alignment"),
    ],
}

# Code inserted BEFORE an anchor line. The no-op walk separates the cost of visiting every node and dispatching
# virtuals from the cost of SemaCheckListener's own work. It has no side effects, so running it first leaves the
# real walk unchanged.
# Only added by --profile-parser. ANTLR's own profiler counts, per grammar decision, how often SLL prediction was
# not enough and it had to fall back to full-context ALL(*) -- which is the expensive path and the thing the
# nestedNameSpecifier fix removed ~11,900 of. Counting them again says whether `expression` is now the remaining
# offender or whether the ambiguity moved somewhere unexpected.
#
# setProfile() swaps in ProfilingATNSimulator, so the parse gets slower while this is on. Read the counts, not the
# phase timings, from a profiled run.
PARSER_PROFILE_BEFORE = ['parser.setProfile(true);']
PARSER_PROFILE_AFTER = [
    'azslc_phase_timing::DumpParserProfile(parser);',
]

INSERTS = {
    MAIN: [
        (r"walker\s*\.\s*walk\s*\(&semanticListener", [
            "azslParserBaseListener noopListenerForTiming;",
            "walker.walk(&noopListenerForTiming, tree);",
            'azslc_phase_timing::Mark("walk: dispatch only");',
        ]),
    ],
}

# --deep: wrap the body of every function matching this. The mark goes after the '{' on the line following the
# signature, and the captured group names the record.
BODY_WRAP = {
    LISTENER: (r"^\s*void SemaCheckListener::([A-Za-z_][A-Za-z0-9_]*)\s*\(", 'AZSLC_ACCUM("{name}");'),
}

PREAMBLE = {
    MAIN: '''{begin}
// Phase timing. One global, so its destructor prints after main() returns and catches the output writes and
// teardown that happen on the way out. Steady clock: this measures elapsed work, and a wall clock that stepped
// backwards mid-run would silently produce negative phases.
#include <chrono>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
namespace azslc_phase_timing
{{
    struct Accum {{ const char* m_name; double m_ms; long long m_calls; }};
    // Deliberately leaked, never destroyed. A function-local static vector here would be constructed on the first
    // NewAccum call -- during the parse walk -- and therefore destroyed BEFORE g_phaseTimer, which was constructed
    // at startup. ~PhaseTimer would then read a destroyed vector and print nothing, silently.
    static std::vector<Accum*>& Accums() {{ static std::vector<Accum*>* v = new std::vector<Accum*>(); return *v; }}

    // Opaque handle so the accumulator's layout lives in exactly one translation unit.
    void* NewAccum(const char* name)
    {{
        Accum* a = new Accum{{name, 0.0, 0}};
        Accums().push_back(a);
        return a;
    }}
    void AddAccum(void* handle, double ms)
    {{
        Accum* a = static_cast<Accum*>(handle);
        a->m_ms += ms;
        ++a->m_calls;
    }}

    struct PhaseTimer
    {{
        using Clock = std::chrono::steady_clock;
        Clock::time_point m_start = Clock::now();
        Clock::time_point m_last  = m_start;
        std::vector<std::pair<std::string, double>> m_marks;

        void Mark(const char* name)
        {{
            const auto now = Clock::now();
            m_marks.emplace_back(name, std::chrono::duration<double, std::milli>(now - m_last).count());
            m_last = now;
        }}

        ~PhaseTimer()
        {{
            if (m_marks.empty()) {{ return; }}   // --help, --version, a syntax-only run, or a throw before phase one
            const auto now = Clock::now();
            const double tail  = std::chrono::duration<double, std::milli>(now - m_last).count();
            const double total = std::chrono::duration<double, std::milli>(now - m_start).count();
            std::fprintf(stderr, "\\n=== AZSLc phase breakdown ===\\n");
            for (const auto& mark : m_marks)
            {{
                std::fprintf(stderr, "  %-22s %9.1f ms  %5.1f%%\\n",
                             mark.first.c_str(), mark.second, total > 0.0 ? 100.0 * mark.second / total : 0.0);
            }}
            std::fprintf(stderr, "  %-22s %9.1f ms  %5.1f%%\\n",
                         "reflection + exit", tail, total > 0.0 ? 100.0 * tail / total : 0.0);
            std::fprintf(stderr, "  %-22s %9.1f ms\\n", "TOTAL", total);
            std::fprintf(stderr, "=============================\\n");

            auto& accums = Accums();
            if (!accums.empty())
            {{
                std::sort(accums.begin(), accums.end(),
                          [](const Accum* l, const Accum* r) {{ return l->m_ms > r->m_ms; }});
                double sum = 0.0;
                for (const Accum* a : accums) {{ sum += a->m_ms; }}
                std::fprintf(stderr, "\\n=== SemaCheckListener callbacks ===\\n");
                std::fprintf(stderr, "  %-42s %9s %10s %9s\\n", "callback", "ms", "calls", "us/call");
                for (const Accum* a : accums)
                {{
                    if (a->m_calls == 0) {{ continue; }}
                    std::fprintf(stderr, "  %-42s %9.1f %10lld %9.2f\\n",
                                 a->m_name, a->m_ms, a->m_calls,
                                 a->m_calls ? 1000.0 * a->m_ms / (double)a->m_calls : 0.0);
                }}
                std::fprintf(stderr, "  %-42s %9.1f\\n", "SUM", sum);
                std::fprintf(stderr, "==================================\\n");
            }}
        }}
    }};
    PhaseTimer g_phaseTimer;

    // External linkage on purpose: the other instrumented translation units call these.
    void Mark(const char* name) {{ g_phaseTimer.Mark(name); }}

    // ANTLR's decision profiler. LL_Fallback is the count that matters: how many times SLL prediction hit a
    // conflict and the parser had to re-run the decision with the full call stack. Ambiguities are decisions where
    // more than one alternative matched and ANTLR resolved by alternative order -- what nestedNameSpecifier was
    // doing for every identifier in the file.
    void DumpParserProfile(antlr4::Parser& parser)
    {{
        auto info = parser.getParseInfo().getDecisionInfo();
        const auto& parserAtn = parser.getATN();
        const auto& ruleNames = parser.getRuleNames();

        std::vector<size_t> order;
        for (size_t i = 0; i < info.size(); ++i)
        {{
            if (info[i].LL_Fallback > 0 || info[i].ambiguities.size() > 0) {{ order.push_back(i); }}
        }}
        std::sort(order.begin(), order.end(),
                  [&info](size_t l, size_t r) {{ return info[l].LL_Fallback > info[r].LL_Fallback; }});

        long long totalFallback = 0, totalInvocations = 0, totalAmbig = 0;
        double totalMs = 0.0;
        for (const auto& d : info)
        {{
            totalFallback += d.LL_Fallback;
            totalInvocations += d.invocations;
            totalAmbig += (long long)d.ambiguities.size();
            totalMs += (double)d.timeInPrediction / 1e6;
        }}

        std::fprintf(stderr, "\\n=== ANTLR decision profile ===\\n");
        std::fprintf(stderr, "  %-5s %-26s %11s %11s %9s %9s\\n",
                     "dec", "rule", "LL_fallback", "invocations", "ambig", "pred ms");
        for (size_t i : order)
        {{
            const auto& d = info[i];
            const char* rule = "?";
            if (d.decision < parserAtn.decisionToState.size())
            {{
                const size_t ruleIndex = parserAtn.decisionToState[d.decision]->ruleIndex;
                if (ruleIndex < ruleNames.size()) {{ rule = ruleNames[ruleIndex].c_str(); }}
            }}
            std::fprintf(stderr, "  %-5zu %-26s %11lld %11lld %9zu %9.1f\\n",
                         d.decision, rule, d.LL_Fallback, d.invocations,
                         d.ambiguities.size(), (double)d.timeInPrediction / 1e6);
        }}
        std::fprintf(stderr, "  %-32s %11lld %11lld %9lld %9.1f\\n",
                     "TOTAL (all decisions)", totalFallback, totalInvocations, totalAmbig, totalMs);

        // Which alternatives actually conflicted. ambigAlts is a bitset of the alternatives that all matched, so
        // the same set recurring thousands of times names one structural ambiguity in the grammar rather than
        // many. The snippet is the source that triggered it, which is usually enough to recognise the shape.
        int shown = 0;
        for (size_t i : order)
        {{
            const auto& d = info[i];
            if (d.ambiguities.empty() || shown >= 3) {{ continue; }}
            ++shown;
            const char* rule = "?";
            if (d.decision < parserAtn.decisionToState.size())
            {{
                const size_t ruleIndex = parserAtn.decisionToState[d.decision]->ruleIndex;
                if (ruleIndex < ruleNames.size()) {{ rule = ruleNames[ruleIndex].c_str(); }}
            }}
            std::map<std::string, std::pair<long long, std::string>> byAlts;
            for (const auto& amb : d.ambiguities)
            {{
                auto& entry = byAlts[amb.ambigAlts.toString()];
                ++entry.first;
                if (entry.second.empty() && amb.input != nullptr && amb.stopIndex >= amb.startIndex)
                {{
                    std::string snippet = const_cast<antlr4::TokenStream*>(amb.input)->getText(
                        antlr4::misc::Interval(amb.startIndex, amb.stopIndex));
                    for (auto& ch : snippet) {{ if (ch == '\\n' || ch == '\\r' || ch == '\\t') {{ ch = ' '; }} }}
                    if (snippet.size() > 56) {{ snippet = snippet.substr(0, 56) + "..."; }}
                    entry.second = snippet;
                }}
            }}
            std::vector<std::pair<std::string, std::pair<long long, std::string>>> sorted(byAlts.begin(), byAlts.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& l, const auto& r) {{ return l.second.first > r.second.first; }});
            std::fprintf(stderr, "\\n  ambiguities in '%s' (decision %zu), by conflicting alternatives:\\n",
                         rule, d.decision);
            int rows = 0;
            for (const auto& e : sorted)
            {{
                if (rows++ >= 4) {{ break; }}
                std::fprintf(stderr, "    alts %-12s x%-6lld  %s\\n",
                             e.first.c_str(), e.second.first, e.second.second.c_str());
            }}
        }}
        std::fprintf(stderr, "==============================\\n");
    }}
}}
{end}''',

    IR: '''{begin}
// Defined in Main.cpp. Declared rather than included so this instrumentation adds no header to the build.
namespace azslc_phase_timing {{ void Mark(const char* name); }}
{end}''',

    LISTENER: '''{begin}
// Per-call-site accumulation for the listener callbacks. The record is a function-local static, so the lookup is
// resolved once at first call and every later call is a pointer dereference -- a map keyed on the callback name
// would have cost more than several of the callbacks being measured.
#include <chrono>
namespace azslc_phase_timing
{{
    void* NewAccum(const char* name);
    void AddAccum(void* handle, double ms);

    struct ScopedAccum
    {{
        void* m_handle;
        std::chrono::steady_clock::time_point m_t0;
        explicit ScopedAccum(void* handle) : m_handle(handle), m_t0(std::chrono::steady_clock::now()) {{}}
        ~ScopedAccum()
        {{
            AddAccum(m_handle, std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_t0).count());
        }}
    }};
}}
#define AZSLC_ACCUM(NAME) \\
    static void* s_azslcAccum = azslc_phase_timing::NewAccum(NAME); \\
    azslc_phase_timing::ScopedAccum azslcAccumGuard{{s_azslcAccum}}
{end}''',
}

MARK_CALL = 'azslc_phase_timing::Mark("{label}"); ' + TAG


def load(path):
    raw = path.read_bytes().decode("utf-8", errors="surrogateescape")
    return raw, ("\r\n" if "\r\n" in raw else "\n")


def strip_lines(lines):
    out, inside = [], False
    for ln in lines:
        if BEGIN in ln:
            inside = True
            continue
        if inside:
            if END in ln:
                inside = False
            continue
        if TAG in ln:
            continue
        out.append(ln)
    if inside:
        raise RuntimeError("opening sentinel without a closing one; refusing to write a truncated file")
    return out


def apply_to(path, anchors, preamble, inserts=(), body_wrap=None, profile_parser=False):
    raw, eol = load(path)
    lines = raw.split(eol)

    missing = [lbl for rx, lbl in anchors if not any(re.search(rx, l) for l in lines)]
    for rx, _ in inserts:
        if not any(re.search(rx, l) for l in lines):
            missing.append("insert:" + rx)
    if body_wrap and not any(re.search(body_wrap[0], l) for l in lines):
        missing.append("body-wrap:" + body_wrap[0])
    if missing:
        return None, "anchors not found (AZSLc version drift?): " + ", ".join(missing)

    last_include = max(i for i, l in enumerate(lines[:120]) if l.startswith("#include"))
    out, wrapped = [], 0
    i = 0
    while i < len(lines):
        ln = lines[i]

        for rx, code in inserts:
            if re.search(rx, ln) and TAG not in ln:
                indent = ln[: len(ln) - len(ln.lstrip())]
                out.extend(indent + c + " " + TAG for c in code)
                break

        if profile_parser and re.search(r"parser\s*\.\s*compilationUnit\s*\(", ln) and TAG not in ln:
            indent = ln[: len(ln) - len(ln.lstrip())]
            out.extend(indent + c + " " + TAG for c in PARSER_PROFILE_BEFORE)

        out.append(ln)

        if profile_parser and re.search(r"parser\s*\.\s*compilationUnit\s*\(", ln) and TAG not in ln:
            indent = ln[: len(ln) - len(ln.lstrip())]
            out.extend(indent + c + " " + TAG for c in PARSER_PROFILE_AFTER)

        if i == last_include:
            out.extend(preamble.format(begin=BEGIN, end=END).strip("\n").split("\n"))
            i += 1
            continue

        # A body wrap goes after the '{' on the line following the signature, not after the signature itself.
        if body_wrap:
            m = re.search(body_wrap[0], ln)
            # The brace line may carry a trailing comment (exitFunctionParam does), so compare after stripping one.
            nxt = lines[i + 1].split("//")[0].strip() if i + 1 < len(lines) else ""
            if m and TAG not in ln and nxt == "{":
                out.append(lines[i + 1])
                indent = lines[i + 1][: len(lines[i + 1]) - len(lines[i + 1].lstrip())] + "    "
                out.append(indent + body_wrap[1].format(name=m.group(1)) + " " + TAG)
                wrapped += 1
                i += 2
                continue

        for rx, label in anchors:
            if re.search(rx, ln) and TAG not in ln:
                indent = ln[: len(ln) - len(ln.lstrip())]
                out.append(indent + MARK_CALL.format(label=label))
                break
        i += 1

    return eol.join(out).encode("utf-8", errors="surrogateescape"), (wrapped if body_wrap else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--revert", action="store_true")
    ap.add_argument("--deep", action="store_true", help="also profile every SemaCheckListener callback")
    ap.add_argument("--profile-parser", action="store_true",
                    help="turn on ANTLR's own decision profiler and dump per-decision LL fallbacks")
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    rels = [MAIN, IR] + ([LISTENER] if args.deep else [])
    # revert and check must consider every file, whether or not --deep was passed
    all_rels = [MAIN, IR, LISTENER]
    paths = {r: root / r for r in all_rels}
    for r, p in paths.items():
        if not p.is_file():
            sys.exit(f"not found: {p}\nPass --root pointing at the engine root.")

    state = {r: any(TAG in l or BEGIN in l for l in load(p)[0].split("\n")) for r, p in paths.items()}

    if args.check:
        for r in all_rels:
            raw, eol = load(paths[r])
            lines = raw.split(eol)
            print(f"{r}\n   state: {'INSTRUMENTED' if state[r] else 'clean'}   "
                  f"endings: {'CRLF' if eol == chr(13)+chr(10) else 'LF'}")
            for rx, label in ANCHORS.get(r, []):
                hits = [i + 1 for i, l in enumerate(lines) if re.search(rx, l) and TAG not in l]
                print(f"     {label:<24} "
                      f"{('line ' + str(hits[0])) if len(hits) == 1 else (str(len(hits)) + ' matches' if hits else 'NOT FOUND')}")
            if r in BODY_WRAP:
                n = sum(1 for l in lines if re.search(BODY_WRAP[r][0], l) and TAG not in l)
                print(f"     {'callbacks to wrap':<24} {n}")
            print()
        return

    if args.revert:
        if not any(state.values()):
            print("Not instrumented; nothing to do.")
            return
        for r in all_rels:
            raw, eol = load(paths[r])
            paths[r].write_bytes(eol.join(strip_lines(raw.split(eol))).encode("utf-8", errors="surrogateescape"))
            print(f"Reverted {r}")
        return

    if any(state.values()):
        sys.exit("Already instrumented. Run --revert first.")

    staged = {}
    for r in rels:
        data, info = apply_to(paths[r], ANCHORS.get(r, []), PREAMBLE[r],
                              INSERTS.get(r, ()), BODY_WRAP.get(r),
                              args.profile_parser and r == MAIN)
        if data is None:
            sys.exit(f"Aborting, {r}: {info}")
        staged[paths[r]] = data
        if info:
            print(f"  {r}: wrapped {info} callbacks")
    for p, data in staged.items():
        p.write_bytes(data)
    modes = ("deep" if args.deep else "") + (", parser profile" if args.profile_parser else "")
    print(f"Instrumented {len(staged)} files{' (' + modes.strip(', ') + ')' if modes.strip(', ') else ''}. "
          "Rebuild AZSLc; tables go to stderr.")


if __name__ == "__main__":
    main()
