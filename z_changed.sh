#!/bin/bash
# External-modification audit.
#   MTIME  - when the file was last written; an outlier was touched by something other than the session.
#   EOL    - CRLF where git tracks LF means a Windows tool rewrote the whole file. That kind of rewrite is what
#            silently reverted a line in MaterialCanvasMainWindow.h before, so it is the signal that matters most.
#   DIFF   - "every line replaced" is the same rewrite seen from git's side, and it hides real edits inside it.
cd "$(dirname "$0")" || exit 1

FILES='
Code/Tools/CMakeLists.txt
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphCompiler.h
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocument.h
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocumentRequestBus.h
Gems/Atom/Tools/AtomToolsFramework/Code/Source/Graph/GraphDocument.cpp
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/InMemoryShaderCompiler.h
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/InMemoryShaderCompiler.cpp
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.h
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.cpp
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasMainWindow.h
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasMainWindow.cpp
Gems/Atom/Tools/MaterialCanvas/Code/CMakeLists.txt
Gems/Atom/Tools/MaterialCanvas/Code/materialcanvas_files.cmake
Gems/Atom/Tools/MaterialCanvas/Code/materialcanvas_editor_files.cmake
Gems/Atom/Asset/Shader/Code/CMakeLists.txt
Gems/Atom/RPI/Code/Source/RPI.Builders/Material/MaterialBuilderUtils.h
Gems/Atom/RPI/Code/Source/RPI.Public/Material/MaterialSystem.cpp
engine.json
'

printf '%-19s %-4s %-6s %-9s %s\n' "MTIME" "EOL" "GIT" "DIFF" "FILE"
printf '%s\n' "----------------------------------------------------------------------------------------------------------"

while read -r f; do
    [ -z "$f" ] && continue
    if [ ! -f "$f" ]; then printf '%-19s %-4s %-6s %-9s %s\n' "-" "-" "NOFILE" "-" "$f"; continue; fi

    mtime=$(date -r "$f" '+%Y-%m-%d %H:%M:%S')
    if grep -qU $'\r' "$f" 2>/dev/null; then eol="CRLF"; else eol="LF"; fi

    st=$(git status --porcelain -- "$f" 2>/dev/null | cut -c1-2 | tr -d ' ')
    [ -z "$st" ] && st="clean"

    nums=$(git diff --numstat -- "$f" 2>/dev/null | head -1)
    if [ -z "$nums" ]; then
        diffcol="-"
    else
        add=$(echo "$nums" | cut -f1); del=$(echo "$nums" | cut -f2)
        total=$(wc -l < "$f" | tr -d ' ')
        if [ "$del" -gt 0 ] && [ "$add" -ge $((total - 2)) ] && [ "$del" -ge $((total - 2)) ]; then
            diffcol="REWRITE"
        else
            diffcol="+$add/-$del"
        fi
    fi
    printf '%-19s %-4s %-6s %-9s %s\n' "$mtime" "$eol" "$st" "$diffcol" "$f"
done <<< "$FILES"

echo
echo "REWRITE = every line replaced by identical text: a whole-file rewrite rather than an edit."
