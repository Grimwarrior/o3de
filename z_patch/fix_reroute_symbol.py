import sys, io, os
PATH = os.environ["HOME"] + "/mnt/o3de/Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp"
with io.open(PATH, "r", encoding="utf-8", newline="") as f:
    data = f.read()
eol = "\r\n" if "\r\n" in data else "\n"
def L(lines): return eol.join(lines)

old = L([
'                const AZStd::any sourceSlotValue = GetValueFromSlotOrConnection(sourceSlot);',
'                const AZStd::string sourceSlotSymbolName = GetAzslValueFromSlot(sourceSlot);',
])

new = L([
'                const AZStd::any sourceSlotValue = GetValueFromSlotOrConnection(sourceSlot);',
'',
'                // A reroute declares no variable of its own, so it has no symbol to name and the expression has to be fetched from',
'                // whatever feeds it. Every other node does declare one, and naming it is the entire point of this substitution.',
'                //',
'                // Asking GetAzslValueFromSlot for all of them, reroute or not, is wrong twice over. It answers with the slot\'s literal',
'                // value when nothing is connected behind it, and a literal is written as a brace initializer, which is only valid where a',
'                // declaration expects one: substituted into the widening conversions below it produced "float4({0, 0}, 0, 1)", and behind',
'                // the casts further down "(float2){0, 0, 0, 1}", neither of which AZSL will parse. It also loses the value, because an',
'                // input node\'s output slot carries its type default rather than the value the node contributes -- which is why the broken',
'                // expression read {0, 0} for a float2 input holding {0.05, 0.3}.',
'                const AZStd::string sourceSlotSymbolName =',
'                    IsRerouteNode(sourceSlot->GetParentNode()) ? GetAzslValueFromSlot(sourceSlot) : GetSymbolNameFromSlot(sourceSlot);',
])

n = data.count(old)
if n != 1:
    print("FAIL: found %d occurrences (expected 1)" % n); sys.exit(1)
data = data.replace(old, new, 1)
with io.open(PATH, "w", encoding="utf-8", newline="") as f:
    f.write(data)
print("applied (eol=%s)" % repr(eol))
