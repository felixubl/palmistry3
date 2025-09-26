#!/usr/bin/env bash
set -euo pipefail

# Performance Benchmark for Poker Hand Evaluator
# Focus: Actual measurements only

echo "========================================="
echo "   Poker Evaluator Performance Benchmark"
echo "========================================="
echo ""

# Configuration
N_SMALL=1000000
N_MEDIUM=10000000
N_LARGE=100000000
TRACES_DIR="benchmark_results"
BIN="./target/release/examples/throughput"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="$TRACES_DIR/results_$TIMESTAMP.txt"

# Create results directory
mkdir -p "$TRACES_DIR"

# Function to capture output
log() {
    echo "$@" | tee -a "$RESULTS_FILE"
}

log "Benchmark started at $(date)"
log "System: $(uname -m) on macOS $(sw_vers -productVersion)"
log ""

# Build
log "=== Building ==="
cargo build --release --examples 2>&1 | tail -2 | tee -a "$RESULTS_FILE"
log ""

# Binary size
log "=== Binary Info ==="
SIZE=$(ls -lh "$BIN" | awk '{print $5}')
log "Binary size: $SIZE"
log ""

# Function to run benchmark and extract metrics
run_benchmark() {
    local N=$1
    local DESC=$2
    
    log "--- $DESC: $(printf "%'d" $N) hands ---"
    
    # Run with /usr/bin/time for hardware counters
    TIME_FILE="$TRACES_DIR/time_${N}.txt"
    PROG_FILE="$TRACES_DIR/prog_${N}.txt"
    
    /usr/bin/time -l env N="$N" "$BIN" >"$PROG_FILE" 2>"$TIME_FILE"
    
    # Parse and display results
    python3 - <<PYTHON "$PROG_FILE" "$TIME_FILE" "$RESULTS_FILE" "$N"
import sys, re

prog_file = sys.argv[1]
time_file = sys.argv[2]
results_file = sys.argv[3]
n_hands = int(sys.argv[4])

# Read files
with open(prog_file) as f:
    prog = f.read()
with open(time_file) as f:
    timing = f.read()

# Parse program output for throughput
m = re.search(r'processed\s+([\d_]+)\s+hands\s+in\s+([0-9.]+)\s+s.*?([\d_]+)\s+hands/s', prog)
if m:
    eval_time = float(m.group(2))
    throughput = int(m.group(3).replace('_', ''))
else:
    eval_time = throughput = 0

# Parse hardware counters from time output
def extract(pattern, text, is_float=False):
    m = re.search(pattern, text, re.M)
    if m:
        val = m.group(1).replace(',', '')
        return float(val) if is_float else int(val)
    return 0

instructions = extract(r'^\s*([0-9,]+)\s+instructions retired', timing)
cycles = extract(r'^\s*([0-9,]+)\s+cycles elapsed', timing)
rss_bytes = extract(r'^\s*([0-9,]+)\s+maximum resident set size', timing)
page_faults = extract(r'^\s*([0-9,]+)\s+page reclaims', timing)

# Calculate metrics
ipc = instructions / cycles if cycles > 0 else 0
instr_per_hand = instructions / n_hands if n_hands > 0 else 0
cycles_per_hand = cycles / n_hands if n_hands > 0 else 0
ns_per_hand = (eval_time * 1e9) / n_hands if n_hands > 0 else 0
rss_mb = rss_bytes / (1024 * 1024)
throughput_m = throughput / 1e6

# Display results
results = f"""Throughput:        {throughput_m:.2f} M hands/sec
Time/hand:         {ns_per_hand:.1f} ns
Instructions/hand: {instr_per_hand:.0f}
Cycles/hand:       {cycles_per_hand:.0f}
IPC:               {ipc:.2f}
RSS:               {rss_mb:.1f} MB
Page faults:       {page_faults:,}
"""

print(results)
with open(results_file, 'a') as f:
    f.write(results + "\n")
PYTHON
}

# Run benchmarks
log "=== Performance Measurements ==="
log ""

run_benchmark $N_SMALL "Small"
run_benchmark $N_MEDIUM "Medium"
run_benchmark $N_LARGE "Large"

# System info
log "=== System Info ==="
python3 - <<'PYTHON' | tee -a "$RESULTS_FILE"
import subprocess
import re

try:
    sysctl_output = subprocess.check_output(['sysctl', '-a'], text=True, stderr=subprocess.DEVNULL)
    
    # Extract CPU and cache info
    brand = re.search(r'machdep.cpu.brand_string:\s*(.+)', sysctl_output)
    l1d = re.search(r'hw.perflevel\d+.l1dcachesize:\s*(\d+)', sysctl_output)
    l1i = re.search(r'hw.perflevel\d+.l1icachesize:\s*(\d+)', sysctl_output)
    l2 = re.search(r'hw.perflevel\d+.l2cachesize:\s*(\d+)', sysctl_output)
    
    if brand:
        print(f"CPU: {brand.group(1)}")
    
    print("\nCache sizes:")
    if l1i:
        print(f"  L1I: {int(l1i.group(1))/1024:.0f} KB")
    if l1d:
        print(f"  L1D: {int(l1d.group(1))/1024:.0f} KB")
    if l2:
        print(f"  L2:  {int(l2.group(1))/1024/1024:.1f} MB")
        
except Exception as e:
    print(f"Could not read system info: {e}")
PYTHON

log ""

# Summary analysis
log "=== Performance Summary ==="
python3 - <<'PYTHON' "$TRACES_DIR" | tee -a "$RESULTS_FILE"
import sys
import re
from pathlib import Path

traces_dir = Path(sys.argv[1])

# Collect metrics from all runs
metrics = []
for n in [1000000, 10000000, 100000000]:
    time_file = traces_dir / f"time_{n}.txt"
    prog_file = traces_dir / f"prog_{n}.txt"
    
    if time_file.exists() and prog_file.exists():
        with open(prog_file) as f:
            prog = f.read()
        with open(time_file) as f:
            timing = f.read()
        
        # Extract throughput
        m = re.search(r'([\d_]+)\s+hands/s', prog)
        throughput = int(m.group(1).replace('_', '')) / 1e6 if m else 0
        
        # Extract IPC
        instr = cycles = 0
        m = re.search(r'(\d+)\s+instructions retired', timing)
        if m:
            instr = int(m.group(1).replace(',', ''))
        m = re.search(r'(\d+)\s+cycles elapsed', timing)
        if m:
            cycles = int(m.group(1).replace(',', ''))
        
        ipc = instr/cycles if cycles > 0 else 0
        
        metrics.append({
            'hands': n,
            'throughput': throughput,
            'ipc': ipc
        })

if metrics:
    print("\nThroughput across scales:")
    for m in metrics:
        print(f"  {m['hands']/1e6:>5.0f}M hands: {m['throughput']:>6.2f} M/sec (IPC: {m['ipc']:.2f})")
    
    # Check consistency
    throughputs = [m['throughput'] for m in metrics]
    avg_throughput = sum(throughputs) / len(throughputs)
    variance = max(throughputs) - min(throughputs)
    variance_pct = (variance / avg_throughput) * 100
    
    print(f"\nAverage: {avg_throughput:.2f} M hands/sec")
    print(f"Variance: {variance:.2f} M/sec ({variance_pct:.1f}%)")
    
    # IPC analysis
    avg_ipc = sum(m['ipc'] for m in metrics) / len(metrics)
    print(f"\nAverage IPC: {avg_ipc:.2f}")
    
    if avg_ipc >= 4.5:
        print("→ IPC ≥ 4.5 indicates L1 cache residency")
    elif avg_ipc >= 3.5:
        print("→ IPC 3.5-4.5 indicates mostly L1 with some L2")
    elif avg_ipc >= 2.5:
        print("→ IPC 2.5-3.5 indicates L2 cache access")
    else:
        print("→ IPC < 2.5 indicates L3/memory access")
PYTHON

# Create JSON summary
python3 - <<'PYTHON' "$TRACES_DIR" "$TIMESTAMP"
import sys
import json
import re
from pathlib import Path

traces_dir = Path(sys.argv[1])
timestamp = sys.argv[2]

results = {
    "timestamp": timestamp,
    "benchmarks": []
}

for n in [1000000, 10000000, 100000000]:
    prog_file = traces_dir / f"prog_{n}.txt"
    time_file = traces_dir / f"time_{n}.txt"
    
    if prog_file.exists() and time_file.exists():
        prog = prog_file.read_text()
        timing = time_file.read_text()
        
        # Extract all metrics
        bench = {"hands": n}
        
        m = re.search(r'([\d_]+)\s+hands/s', prog)
        if m:
            bench["throughput"] = int(m.group(1).replace('_', ''))
        
        m = re.search(r'(\d+)\s+instructions retired', timing)
        if m:
            bench["instructions"] = int(m.group(1).replace(',', ''))
            
        m = re.search(r'(\d+)\s+cycles elapsed', timing)
        if m:
            bench["cycles"] = int(m.group(1).replace(',', ''))
            
        m = re.search(r'(\d+)\s+maximum resident set size', timing)
        if m:
            bench["rss_bytes"] = int(m.group(1).replace(',', ''))
        
        if "instructions" in bench and "cycles" in bench:
            bench["ipc"] = bench["instructions"] / bench["cycles"]
            
        results["benchmarks"].append(bench)

# Save
json_file = traces_dir / f"results_{timestamp}.json"
with open(json_file, 'w') as f:
    json.dump(results, f, indent=2)

print(f"\nDetailed results saved to: {json_file}")
PYTHON

log ""
log "========================================="
log "         Benchmark Complete"
log "========================================="
log ""
log "All results in: $TRACES_DIR/"