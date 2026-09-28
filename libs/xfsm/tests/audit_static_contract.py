#!/usr/bin/env python3
"""Reproducible source checks for the XFSM native and host boundary."""

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


XFSM = Path(__file__).resolve().parents[1]
PRODUCTION_C = [
    XFSM / "xfsm_compile.c",
    XFSM / "xfsm_native.c",
    XFSM / "xfsm_runtime.c",
]
PRODUCTION_SOURCE = PRODUCTION_C + list(sorted(XFSM.glob("*.h"))) + [
    XFSM / "jswrap_xfsm.c"
]


def without_comments_and_literals(source: str) -> str:
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(lambda match: "\n" * match.group(0).count("\n"), source)


def function_bodies(source: str) -> dict[str, str]:
    clean = without_comments_and_literals(source)
    header = re.compile(
        r"^[A-Za-z_][^;{}=\n]*\b(xfc[A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{",
        re.MULTILINE,
    )
    bodies: dict[str, str] = {}
    for match in header.finditer(clean):
        depth = 1
        position = match.end()
        while position < len(clean) and depth:
            if clean[position] == "{":
                depth += 1
            elif clean[position] == "}":
                depth -= 1
            position += 1
        if depth:
            raise ValueError(f"unterminated function {match.group(1)}")
        bodies[match.group(1)] = clean[match.end() : position - 1]
    return bodies


def recursive_cycles(functions: dict[str, str]) -> list[list[str]]:
    names = set(functions)
    calls = {
        name: set(re.findall(r"\b(xfc[A-Za-z0-9_]*)\s*\(", body)) & names
        for name, body in functions.items()
    }
    cycles: set[tuple[str, ...]] = set()

    def visit(node: str, path: list[str]) -> None:
        if node in path:
            cycle = path[path.index(node) :]
            rotations = [tuple(cycle[index:] + cycle[:index]) for index in range(len(cycle))]
            cycles.add(min(rotations))
            return
        for target in calls[node]:
            visit(target, path + [node])

    for name in names:
        visit(name, [])
    return [list(cycle) for cycle in sorted(cycles)]


def file_scope_mutable_statics(path: Path) -> list[str]:
    clean = without_comments_and_literals(path.read_text(encoding="utf-8"))
    depth = 0
    statement = ""
    found: list[str] = []
    for character in clean:
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
        if depth == 0:
            statement += character
            if character == ";":
                compact = " ".join(statement.split())
                if compact.startswith("static ") and " const " not in f" {compact} " and "(" not in compact:
                    found.append(compact)
                statement = ""
        elif depth == 1 and statement:
            statement = ""
    return found


def main() -> int:
    failures: list[str] = []
    clean_source = {
        path: without_comments_and_literals(path.read_text(encoding="utf-8"))
        for path in PRODUCTION_SOURCE
    }

    forbidden = {
        "native allocation": r"\b(?:malloc|calloc|realloc|free)\s*\(",
        "packed structure": r"\b(?:packed|#pragma\s+pack)\b",
        "thread/task/IRQ primitive": r"\b(?:pthread_|xTask|vTask|irq|mutex|semaphore)\w*\b",
    }
    for label, pattern in forbidden.items():
        hits = [path.name for path, source in clean_source.items() if re.search(pattern, source)]
        if hits:
            failures.append(f"{label}: {', '.join(hits)}")

    native_includes = re.findall(
        r'^\s*#\s*include\s*[<"]([^>"]+)',
        (XFSM / "xfsm_native.c").read_text(encoding="utf-8") +
        (XFSM / "xfsm_native.h").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    allowed_native_includes = {
        "xfsm_native.h", "stdbool.h", "stddef.h", "stdint.h", "limits.h", "string.h"
    }
    unexpected_includes = sorted(set(native_includes) - allowed_native_includes)
    if unexpected_includes:
        failures.append(f"portable engine includes: {', '.join(unexpected_includes)}")

    functions: dict[str, str] = {}
    for path in PRODUCTION_C:
        functions.update(function_bodies(path.read_text(encoding="utf-8")))
    cycles = recursive_cycles(functions)
    if cycles:
        failures.append("recursive call graph: " + json.dumps(cycles, separators=(",", ":")))

    mutable_statics = {
        path.name: file_scope_mutable_statics(path)
        for path in PRODUCTION_C
        if file_scope_mutable_statics(path)
    }
    if mutable_statics:
        failures.append("mutable production globals: " + json.dumps(mutable_statics, sort_keys=True))

    declarations = []
    declaration_pattern = re.compile(
        r"\b(?:char|u?int(?:8|16|32)_t|JsVar\s*\*|const\s+char\s*\*)\s+"
        r"[A-Za-z_][A-Za-z0-9_]*\s*\[([^\]]+)\]\s*(?:[;=,{])"
    )
    for path in PRODUCTION_SOURCE:
        for dimension in declaration_pattern.findall(path.read_text(encoding="utf-8")):
            declarations.append((path.name, " ".join(dimension.split())))
            if re.search(r"\b[a-z][A-Za-z0-9_]*\b", dimension):
                failures.append(f"variable-length array dimension: {path.name}: {dimension}")

    internal = (XFSM / "xfsm_internal.h").read_text(encoding="utf-8")
    hidden_names = re.findall(
        r'^#define\s+XFC_(?:ROOT_)?[A-Z0-9_]+\s+JS_HIDDEN_CHAR_STR\s+"[^"]+"',
        internal,
        re.MULTILINE,
    )
    if len(hidden_names) != 29:
        failures.append(f"hidden host name count: expected 29, observed {len(hidden_names)}")

    wrapper = (XFSM / "jswrap_xfsm.c").read_text(encoding="utf-8")
    public_json = re.findall(
        r'"name"\s*:\s*"(createMachine|createActor|assign)"', wrapper
    )
    if sorted(public_json) != ["assign", "createActor", "createMachine"]:
        failures.append("public JSON wrapper declarations are incomplete or duplicated")

    runtime = (XFSM / "xfsm_runtime.c").read_text(encoding="utf-8")
    required_host_tokens = [
        "xfcBrandMachine", "xfcBrandAssignment", "xfcSetBrand(actor",
        "xfcSetBrand(snapshot", "xfcSetBrand(subscription", "xfcAttachPrototype",
        "XFC_ACTOR_MACHINE_NAME", "XFC_ACTOR_DATA_NAME",
        "XFC_ACTOR_CONTEXT_NAME", "XFC_ACTOR_SUBSCRIPTIONS_NAME",
    ]
    absent_tokens = [token for token in required_host_tokens if token not in runtime]
    if absent_tokens:
        failures.append("host ownership tokens absent: " + ", ".join(absent_tokens))

    report = {
        "schema": "xfc.static-contract",
        "version": 1,
        "sourceFiles": len(PRODUCTION_SOURCE),
        "functionsChecked": len(functions),
        "fixedArrayDeclarations": len(declarations),
        "hiddenHostNames": len(hidden_names),
        "publicWrappers": sorted(public_json),
        "recursiveCycles": cycles,
        "mutableProductionGlobals": mutable_statics,
        "failures": failures,
    }
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    if failures:
        for failure in failures:
            print(f"FAIL {failure}", file=sys.stderr)
        return 1
    print("PASS XFSM static contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
