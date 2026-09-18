#!/usr/bin/env python3
import sys, xml.etree.ElementTree as ET
from collections import Counter

if len(sys.argv) < 2:
    print("usage: peek_tables.py traces/counters_tables.xml"); sys.exit(1)

src = sys.argv[1]
tree = ET.parse(src)
root = tree.getroot()

def rows():
    for t in root.findall(".//table"):
        cols = [ (c.get("name") or "").strip() for c in t.findall("./columns/column") ]
        if not cols: continue
        for r in t.findall("./rows/row"):
            cells = [ (c.text or "").strip() for c in r.findall("./cell") ]
            if not cells: continue
            yield cols, cells

names = Counter()
examples = []
for cols, cells in rows():
    # take first column as name-ish, last as value-ish (just to list)
    nm = cells[0] if cells else ""
    names[nm] += 1
    if len(examples) < 30:
        examples.append((cols, cells))

print("== Column name sets observed (first 5) ==")
seen_sets = []
for cols, _ in examples[:5]:
    print("  -", cols)

print("\n== First ~30 example rows ==")
for cols, cells in examples:
    print("  ROW:")
    for c, v in zip(cols, cells):
        print(f"    {c!r}: {v!r}")
    print()

print("== Most common first-cell names (top 40) ==")
for nm, cnt in names.most_common(40):
    print(f"  {nm!r}: {cnt}")
