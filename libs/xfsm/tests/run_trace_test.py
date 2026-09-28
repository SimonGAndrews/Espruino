#!/usr/bin/env python3
"""Compose, run, and compare an XFSM canonical trace test."""

# This file is part of Espruino, a JavaScript interpreter for Microcontrollers
#
# Copyright (C) 2026 Simon Andrews
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any, Iterable


TRACE_SCHEMA = "xfc.trace"
TRACE_VERSION = 1
TESTS_DIR = Path(__file__).resolve().parent
REPOSITORY = TESTS_DIR.parents[2]


def load_records(
    lines: Iterable[str], source: str, allow_noise: bool = False
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            if allow_noise:
                continue
            raise ValueError(f"{source}:{line_number}: invalid JSON") from error
        if isinstance(value, dict) and value.get("schema") == TRACE_SCHEMA:
            records.append(value)
        elif not allow_noise:
            raise ValueError(f"{source}:{line_number}: not an XFSM trace record")
    if not records:
        raise ValueError(f"{source}: no {TRACE_SCHEMA!r} records found")
    return records


def validate_records(records: list[dict[str, Any]], source: str) -> None:
    case_id = records[0].get("case")
    if not isinstance(case_id, str) or not case_id:
        raise ValueError(f"{source}: first record has no case identity")
    for sequence, record in enumerate(records):
        if record.get("version") != TRACE_VERSION:
            raise ValueError(f"{source}: record {sequence} has wrong version")
        if record.get("case") != case_id:
            raise ValueError(f"{source}: record {sequence} changes case identity")
        if record.get("sequence") != sequence:
            raise ValueError(f"{source}: record {sequence} has non-contiguous sequence")
    if records[0].get("kind") != "case":
        raise ValueError(f"{source}: first record is not a case record")
    if records[-1].get("kind") != "result":
        raise ValueError(f"{source}: last record is not a result record")
    if records[-1].get("pass") is not True:
        raise ValueError(f"{source}: trace result did not pass")


def canonical_lines(records: list[dict[str, Any]]) -> list[str]:
    return [
        json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
        for record in records
    ]


def compose(harness: Path, test: Path) -> str:
    return harness.read_text(encoding="utf-8") + "\n" + test.read_text(
        encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path, help="trace test source fragment")
    parser.add_argument("expected", type=Path, nargs="?", help="accepted NDJSON trace")
    parser.add_argument(
        "--espruino",
        type=Path,
        default=REPOSITORY / "bin" / "espruino",
        help="Espruino host executable",
    )
    parser.add_argument(
        "--harness",
        type=Path,
        default=TESTS_DIR / "trace_harness.js",
        help="JavaScript trace harness",
    )
    parser.add_argument(
        "--compose-output",
        type=Path,
        help="write the composed physical-device artifact instead of running it",
    )
    parser.add_argument("--show-output", action="store_true")
    args = parser.parse_args()

    source = compose(args.harness, args.test)
    if args.compose_output:
        args.compose_output.write_text(source, encoding="utf-8")
        print(args.compose_output)
        return 0
    if args.expected is None:
        parser.error("expected trace is required unless --compose-output is used")

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".js", encoding="utf-8", delete=False
    ) as temporary:
        temporary.write(source)
        temporary_path = Path(temporary.name)
    try:
        completed = subprocess.run(
            [str(args.espruino), "--test", str(temporary_path)],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
    finally:
        temporary_path.unlink(missing_ok=True)

    if args.show_output or completed.returncode:
        sys.stdout.write(completed.stdout)
        sys.stderr.write(completed.stderr)

    try:
        observed = load_records(
            completed.stdout.splitlines(), "observed output", allow_noise=True
        )
        expected = load_records(
            args.expected.read_text(encoding="utf-8").splitlines(),
            str(args.expected),
        )
        validate_records(observed, "observed output")
        validate_records(expected, str(args.expected))
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    observed_lines = canonical_lines(observed)
    expected_lines = canonical_lines(expected)
    if observed_lines != expected_lines:
        sys.stderr.writelines(
            difflib.unified_diff(
                expected_lines,
                observed_lines,
                fromfile=str(args.expected),
                tofile="observed",
            )
        )
        return 1
    if completed.returncode:
        print(
            f"Espruino exited with status {completed.returncode}", file=sys.stderr
        )
        return completed.returncode

    print(f"PASS {observed[0]['case']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
