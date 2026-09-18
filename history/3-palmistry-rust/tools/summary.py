#!/usr/bin/env python3
import json, sys, re, pathlib, xml.etree.ElementTree as ET

# Read /usr/bin/time output for IPC/RSS fallback
def time_summary(traces_dir):
    tfile = traces_dir / "time.txt"
    if not tfile.exists(): return {}
    t = tfile.read_text()
    def pick(rx):
        m = re.search(rx, t, re.M)
        return float(m.group(1).replace(',','')) if m else 0.0
    return {
        "instr_time": pick(r"^\s*([0-9,]+)\s+instructions retired"),
        "cycles_time": pick(r"^\s*([0-9,]+)\s+cycles elapsed"),
        "rss_bytes": pick(r"^\s*([0-9,]+)\s+maximum resident set size")  # macOS reports in bytes
    }

# Parse XML tables from xctrace export
def parse_xctrace_xml(xml_path):
    if not xml_path.exists():
        return []
    
    try:
        tree = ET.parse(xml_path)
        root = tree.getroot()
    except Exception as e:
        print(f"[debug] XML parse error: {e}")
        return []
    
    rows = []
    
    # Try to find counter tables
    for table in root.findall(".//table"):
        schema = table.get("schema", "")
        
        # Get column names
        columns = []
        for col in table.findall("./columns/column"):
            columns.append(col.get("name", "").strip())
        
        # Parse rows
        for row in table.findall("./rows/row"):
            cells = []
            for cell in row.findall("./cell"):
                cells.append((cell.text or "").strip())
            
            # Try to extract name/value pairs
            if len(cells) >= 2:
                # Common patterns: (name, value) or (counter, count) or (event, value)
                name = cells[0]
                value_str = cells[-1]  # Often the value is the last column
                
                # Try to parse numeric value
                value_str = re.sub(r'[,\u00A0\s]', '', value_str)
                value_str = re.sub(r'[^0-9eE+\-\.].*$', '', value_str)
                try:
                    value = float(value_str)
                    if value > 0:
                        rows.append({"name": name, "value": value})
                except:
                    pass
    
    return rows

# Load JSON rows or parse XML
def load_rows(json_path):
    # First try JSON
    if json_path.exists():
        try:
            data = json.loads(json_path.read_text())
            if isinstance(data, dict) and "rows" in data:
                return data["rows"]
            if isinstance(data, list):
                return data
        except:
            pass
    
    # Fall back to XML parsing
    xml_path = json_path.parent / "counters_tables.xml"
    return parse_xctrace_xml(xml_path)

def norm(s): 
    return (s or "").strip().lower()

# Counter name patterns (expanded for better matching)
LABELS = {
    "instructions":  [r"instruction", r"instr.*retired", r"^instr$"],
    "cycles":        [r"cycle", r"cycles.*elapsed", r"^cyc$"],
    "l1d_access":    [r"l1.*d.*access", r"l1d.*access", r"l1 data.*access", r"l1-dcache.*access"],
    "l1d_miss":      [r"l1.*d.*miss", r"l1d.*miss", r"l1 data.*miss", r"l1-dcache.*miss"],
    "l2_access":     [r"l2.*access", r"l2 cache.*access", r"l2-cache.*access"],
    "l2_miss":       [r"l2.*miss", r"l2 cache.*miss", r"l2-cache.*miss"],
    "slc_access":    [r"system.*cache.*access", r"sys.*cache.*access", r"slc.*access", r"last.*level.*access"],
    "slc_miss":      [r"system.*cache.*miss", r"sys.*cache.*miss", r"slc.*miss", r"last.*level.*miss"],
    "branches":      [r"branch(?!.*mis)", r"^br$", r"branch.*retired"],
    "br_mispredict": [r"branch.*mis", r"mispredict", r"br.*mis"]
}

def match_bucket(name):
    n = norm(name)
    for key, patterns in LABELS.items():
        for pat in patterns:
            if re.search(pat, n):
                return key
    return None

def main():
    # Parse arguments
    json_path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "traces/counters.json")
    traces_dir = json_path.parent if json_path.parent.name else pathlib.Path("traces")

    rows = load_rows(json_path)
    
    # Aggregate counters
    sums = {k: 0.0 for k in LABELS}
    seen_names = []
    unmatched = []
    
    for r in rows:
        name = r.get("name") or ""
        val = r.get("value")
        if isinstance(val, (int, float)) and val > 0:
            b = match_bucket(name)
            if b:
                sums[b] += float(val)
                seen_names.append(f"{name} -> {b}")
            else:
                unmatched.append(f"{name}: {val}")
    
    # Get fallback from time.txt
    ts = time_summary(traces_dir)
    
    # Use xctrace values if available, otherwise fall back to time
    instr = sums["instructions"] or ts.get("instr_time", 0.0)
    cyc   = sums["cycles"]       or ts.get("cycles_time", 0.0)
    ipc   = (instr / cyc) if cyc else 0.0
    
    def pct(m, a): 
        return 100.0 * (m / a) if a else 0.0
    
    print(f"\n== Hardware Counters Summary ==")
    print(f"Instructions  : {instr:,.0f}")
    print(f"Cycles        : {cyc:,.0f}   (IPC = {ipc:.2f})")
    
    l1a, l1m = sums["l1d_access"], sums["l1d_miss"]
    l2a, l2m = sums["l2_access"],  sums["l2_miss"]
    sca, scm = sums["slc_access"], sums["slc_miss"]
    
    if l1a > 0:
        print(f"L1D Access    : {l1a:,.0f}   Miss: {l1m:,.0f}   Miss% = {pct(l1m, l1a):.2f}%")
    if l2a > 0:
        print(f"L2  Access    : {l2a:,.0f}   Miss: {l2m:,.0f}   Miss% = {pct(l2m, l2a):.2f}%")
    if sca > 0:
        print(f"SLC Access    : {sca:,.0f}   Miss: {scm:,.0f}   Miss% = {pct(scm, sca):.2f}%")
    
    br, brm = sums["branches"], sums["br_mispredict"]
    if br > 0:
        mpki = (1000.0 * brm / instr) if instr else 0.0
        print(f"Branches      : {br:,.0f}    Mispredict: {brm:,.0f}   MPKI = {mpki:.2f}")
    
    rss_bytes = ts.get("rss_bytes", 0.0)
    rss_mb = rss_bytes / (1024*1024) if rss_bytes else 0.0
    print(f"Peak RSS      : {rss_mb:.2f} MiB")
    
    # Debug info
    if seen_names:
        print(f"\n[debug] Matched {len(seen_names)} counter values")
        for n in seen_names[:10]:
            print(f"  {n}")
    
    if unmatched:
        print(f"\n[debug] Unmatched counters: {len(unmatched)}")
        for n in unmatched[:10]:
            print(f"  {n}")
    
    # Save raw data for debugging
    raw_path = traces_dir / "counters_debug.json"
    with open(raw_path, 'w') as f:
        json.dump({
            "rows": rows,
            "sums": sums,
            "time_stats": ts,
            "matched": seen_names,
            "unmatched": unmatched
        }, f, indent=2)
    print(f"\n[debug] Saved raw data to {raw_path}")

if __name__ == "__main__":
    main()