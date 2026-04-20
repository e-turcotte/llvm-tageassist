#!/usr/bin/env python3
"""
inject_nop.py — Replace a NOP at a given PC with a byte value from a CSV.

CSV format (no header required, but header is auto-detected):
    pc,byte
    0x401000,0xAB
    ...

The "pc" column is the address of the NOP instruction (hex or decimal).
The "byte" column is the replacement byte value to substitute in place of the NOP.

Usage:
    python inject_nop.py <csv_file> <asm_file> [--output <out_file>] [--inplace]

The script:
  1. Parses the CSV for (pc, byte) pairs.
  2. Scans the assembly file for lines whose address label matches a PC.
  3. Verifies the matched line is a NOP instruction.
  4. Replaces the NOP with:   db 0xXX   ; replaced nop at <pc>
  5. Writes the result to --output (default: <asm_file>.patched.asm) or
     back to the original file when --inplace is given.
"""

import argparse
import csv
import re
import sys
from pathlib import Path


def parse_csv(csv_path: str) -> dict[int, int]:
    """Return {nop_pc: prefix_byte} from the CSV file.

    Accepts:
      • Files with a header row containing 'nop_pc' and 'prefix_byte' columns (case-insensitive).
      • Files with exactly two columns and no header (first = nop_pc, second = prefix_byte).
    Both hex (0x…) and decimal values are accepted for both columns.
    """
    path = Path(csv_path)
    if not path.is_file():
        sys.exit(f"[ERROR] CSV file not found: {csv_path}")

    entries: dict[int, int] = {}

    with path.open(newline="") as fh:
        sample = fh.read(1024)
        fh.seek(0)
        sniffer = csv.Sniffer()
        dialect = sniffer.sniff(sample, delimiters=",\t;")
        has_header = sniffer.has_header(sample)

        reader = csv.DictReader(fh, dialect=dialect) if has_header else csv.reader(fh, dialect=dialect)

        if has_header:
            reader.fieldnames = [f.strip().lower() for f in (reader.fieldnames or [])]
            if "nop_pc" not in reader.fieldnames or "prefix_byte" not in reader.fieldnames:
                sys.exit(
                    f"[ERROR] CSV header must contain 'nop_pc' and 'prefix_byte' columns. "
                    f"Found: {reader.fieldnames}"
                )
            for lineno, row in enumerate(reader, start=2):
                nop_pc_str = row["nop_pc"].strip()
                prefix_byte_str = row["prefix_byte"].strip()
                try:
                    nop_pc = int(nop_pc_str, 0)
                    prefix_byte = int(prefix_byte_str, 0)
                except ValueError:
                    print(f"[WARN] Skipping malformed row {lineno}: nop_pc={nop_pc_str!r}, prefix_byte={prefix_byte_str!r}")
                    continue
                if not (0 <= prefix_byte <= 0xFF):
                    print(f"[WARN] Byte value {prefix_byte:#x} out of range at row {lineno}; skipping.")
                    continue
                entries[nop_pc] = prefix_byte
        else:
            for lineno, row in enumerate(reader, start=1):
                if len(row) < 2:
                    print(f"[WARN] Skipping short row {lineno}: {row}")
                    continue
                nop_pc_str, prefix_byte_str = row[0].strip(), row[1].strip()
                try:
                    nop_pc = int(nop_pc_str, 0)
                    prefix_byte = int(prefix_byte_str, 0)
                except ValueError:
                    print(f"[WARN] Skipping malformed row {lineno}: nop_pc={nop_pc_str!r}, prefix_byte={prefix_byte_str!r}")
                    continue
                if not (0 <= prefix_byte <= 0xFF):
                    print(f"[WARN] Byte value {prefix_byte:#x} out of range at row {lineno}; skipping.")
                    continue
                entries[nop_pc] = prefix_byte

    if not entries:
        sys.exit("[ERROR] No valid (nop_pc, prefix_byte) entries found in CSV.")

    print(f"[INFO] Loaded {len(entries)} entries from {csv_path}")
    return entries


# ---------------------------------------------------------------------------
# Address extraction helpers
# ---------------------------------------------------------------------------

# Matches common address labels:
#   0x401000:    nop
#   401000:      nop
#   loc_401000:  nop
#   .L401000:    nop
_ADDR_LABEL_RE = re.compile(
    r"^\s*(?:(?:0x)?([0-9a-fA-F]{4,16})\s*:|"  # bare hex address label
    r"(?:[.\w]+_)?([0-9a-fA-F]{4,16})\s*:)",   # symbolic label ending in hex digits
)

# Matches a NOP with optional trailing comment:  nop  or  nop ; ...
_NOP_INSTR_RE = re.compile(r"^\s*90\s*nop\b\s*(;.*)?$", re.IGNORECASE)


def extract_address(line: str) -> int | None:
    """Return the integer address encoded in an assembly label, or None."""
    m = _ADDR_LABEL_RE.match(line)
    if not m:
        return None
    hex_str = m.group(1) or m.group(2)
    try:
        return int(hex_str, 16)
    except ValueError:
        return None


def is_nop(line: str) -> bool:
    """Return True if the instruction on this line is a bare NOP."""
    # Strip the address/label prefix so we only examine the mnemonic portion.
    instr_part = re.sub(r"^[^:]+:\s*", "", line, count=1)
    return bool(_NOP_INSTR_RE.match(instr_part))


def replace_nop_with_byte(line: str, pc: int, byte_val: int) -> str:
    """Replace the NOP mnemonic in *line* with a db directive for *byte_val*.

    The label prefix (if any) and line ending are preserved.
    Result:   <label_prefix>  db 0xXX   ; replaced nop at 0x<pc>
    """
    # Capture everything up to and including the label colon + whitespace.
    label_match = re.match(r"^(\s*[^:]+:\s*)", line)
    prefix = label_match.group(1) if label_match else ""
    return f"{prefix}{byte_val:02X}  ; replaced nop at {pc:#010x}\n"


# ---------------------------------------------------------------------------
# Core replacement logic
# ---------------------------------------------------------------------------

def replace_nops(asm_lines: list[str], entries: dict[int, int]) -> tuple[list[str], list[int]]:
    """Return (patched_lines, list_of_matched_pcs).

    For each line whose address appears in *entries* AND whose mnemonic is a
    NOP, the NOP is replaced with a db directive carrying the CSV byte value.
    """
    output: list[str] = []
    matched_pcs: list[int] = []
    remaining = dict(entries)  # consumed as matches are found

    for line in asm_lines:
        addr = extract_address(line)
        if addr is not None and addr in remaining:
            if is_nop(line):
                byte_val = remaining.pop(addr)
                new_line = replace_nop_with_byte(line, addr, byte_val)
                output.append(new_line)
                matched_pcs.append(addr)
                print(
                    f"[REPLACE] PC {addr:#010x} → nop replaced with db {byte_val:#04x}\n"
                    f"          original : {line.rstrip()}\n"
                    f"          replaced : {new_line.rstrip()}"
                )
            else:
                print(
                    f"[SKIP]    PC {addr:#010x} found but instruction is not a NOP: {line.rstrip()}"
                )
                output.append(line)
        else:
            output.append(line)

    # Warn about entries that were never matched
    for pc in remaining:
        print(f"[WARN]    PC {pc:#010x} not found (or not a NOP) in the assembly file.")

    return output, matched_pcs


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Replace NOP instructions at given PCs with byte values from a CSV.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("csv_file", help="CSV file with 'pc' and 'byte' columns.")
    p.add_argument("asm_file", help="x86 assembly source file to patch.")
    p.add_argument(
        "--output", "-o",
        default=None,
        help="Output file path. Default: <asm_file>.patched.asm",
    )
    p.add_argument(
        "--inplace", "-i",
        action="store_true",
        help="Overwrite the original assembly file instead of writing a new one.",
    )
    return p


def main() -> None:
    args = build_parser().parse_args()

    asm_path = Path(args.asm_file)
    if not asm_path.is_file():
        sys.exit(f"[ERROR] Assembly file not found: {args.asm_file}")

    if args.inplace:
        out_path = asm_path
    elif args.output:
        out_path = Path(args.output)
    else:
        out_path = asm_path.with_suffix("").with_name(asm_path.stem + ".patched" + asm_path.suffix)

    entries = parse_csv(args.csv_file)

    with asm_path.open() as fh:
        asm_lines = fh.readlines()

    print(f"[INFO] Processing {len(asm_lines)} lines from {asm_path}")

    patched, matched = replace_nops(asm_lines, entries)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w") as fh:
        fh.writelines(patched)

    print(
        f"\n[DONE] {len(matched)}/{len(entries)} NOPs replaced. "
        f"Output written to: {out_path}"
    )


if __name__ == "__main__":
    main()
