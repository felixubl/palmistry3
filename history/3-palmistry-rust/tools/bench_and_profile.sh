#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   N=10000000 DURATION=5 ./tools/bench_and_profile.sh
#
# Outputs:
#   - Eval-only throughput (from examples/throughput)
#   - Process-wide runtime, IPC, Peak RSS (/usr/bin/time -l)
#   - L1D/L2/System-cache + Branch counters via xctrace (headless)
#   - vm_stat/top snapshots
#   - All artifacts in ./traces/

N="${N:-5000000}"
DURATION="${DURATION:-5}"
TRACES_DIR="traces"
BIN="./target/release/examples/throughput"

mkdir -p "$TRACES_DIR"

echo "== Build =="
RUSTFLAGS="-C target-cpu=native -C codegen-units=1 -C panic=abort" \
cargo build --release --examples

echo
echo "== Run throughput example (eval-only timing) =="
TIME_OUT="$TRACES_DIR/time.txt"
PROG_OUT="$TRACES_DIR/prog.txt"
/usr/bin/time -l env N="$N" "$BIN" >"$PROG_OUT" 2>"$TIME_OUT"

python3 - <<'PY' "$PROG_OUT" "$TIME_OUT"
import sys, re
prog = open(sys.argv[1]).read()
tim  = open(sys.argv[2]).read()

pm = re.search(r'processed\s+([\d_]+)\s+hands\s+in\s+([0-9.]+)\s+s.*?([\d_]+)\s+hands/s', prog)
hands_eval  = int(pm.group(1).replace('_','')) if pm else 0
secs_eval   = float(pm.group(2)) if pm else 0.0
hps_eval    = int(pm.group(3).replace('_','')) if pm else 0

def fnum(pat, flags=0):
    m = re.search(pat, tim, flags); 
    return float(m.group(1).replace(',','')) if m else 0.0
def fint(pat, flags=0):
    m = re.search(pat, tim, flags); 
    return int(m.group(1).replace(',','')) if m else 0

real = fnum(r'([0-9.]+)\s+real\b')
user = fnum(r'([0-9.]+)\s+user\b')
sys  = fnum(r'([0-9.]+)\s+sys\b')
elapsed = real if real > 0 else (user + sys)

instr  = fnum(r'^\s*([0-9,]+)\s+instructions retired\s*$', re.M|re.I)
cycles = fnum(r'^\s*([0-9,]+)\s+cycles elapsed\s*$',       re.M|re.I)

# Fixed RSS parsing for macOS
rss_bytes = 0
# Try multiple patterns for RSS
patterns = [
    r'^\s*([0-9,]+)\s+maximum resident set size\s*$',  # macOS format (bytes)
    r'^\s*([0-9,]+)\s+peak memory footprint\s*$',      # Alternative format
    r'maximum resident set size\s+([0-9,]+)',          # Fallback pattern
]
for pat in patterns:
    m = re.search(pat, tim, re.M|re.I)
    if m:
        rss_bytes = int(m.group(1).replace(',',''))
        break

# macOS reports RSS in bytes, not KB
rss_mb = rss_bytes / (1024*1024) if rss_bytes else 0

ipc = (instr / cycles) if cycles else 0.0

print("\n== Summary (eval-only from program) ==")
if pm:
    print(f"Eval hands       : {hands_eval:,.0f}")
    print(f"Eval time        : {secs_eval:.3f} s")
    print(f"Eval throughput  : {hps_eval/1e6:.2f} M hands/s")
else:
    print("Could not parse eval-only line from program output.")

print("\n== Process-wide stats (/usr/bin/time -l) ==")
print(f"Wall time (total): {elapsed:.3f} s   (user {user:.3f}s, sys {sys:.3f}s)")
print(f"Instructions     : {instr:,.0f}")
print(f"Cycles           : {cycles:,.0f}   (IPC = {ipc:.2f})")
print(f"Peak RSS         : {rss_mb:.2f} MiB")
PY

echo
echo "== System snapshots (optional) =="
echo "Sampling vm_stat and top for ~${DURATION}s..."
VM_OUT="$TRACES_DIR/vm_stat.txt"
TOP_OUT="$TRACES_DIR/top.txt"
( vm_stat -c "${DURATION}" 1 > "$VM_OUT" 2>/dev/null ) & 
( top -l "${DURATION}" -s 1 -stats pid,cpu,mem,vsize,threads > "$TOP_OUT" 2>/dev/null ) & 
sleep "${DURATION}"
echo "Saved: $VM_OUT, $TOP_OUT"

# ---- Headless CPU counters via xctrace (requires full Xcode) ----
if command -v xcrun >/dev/null 2>&1 && xcrun --find xctrace >/dev/null 2>&1; then
  echo
  echo "== Record CPU/cache counters with xctrace (${DURATION}s) =="

  # Pick a template that exists
  TEMPLATES="$(xcrun xctrace list templates 2>/dev/null | sed -e 's/^[[:space:]]*//')"
  TEMPLATE=""
  for t in "CPU Counters" "Counters" "Performance" "Time Profiler"; do
    if echo "$TEMPLATES" | grep -q "^$t$"; then 
      TEMPLATE="$t"
      break
    fi
  done
  
  if [ -z "$TEMPLATE" ]; then
    echo "Warning: No suitable xctrace template found"
    echo "Available templates:"
    echo "$TEMPLATES"
  else
    TRACE="$TRACES_DIR/counters-$(date +%Y%m%d-%H%M%S).trace"
    TOC_XML="$TRACES_DIR/counters_toc.xml"
    TABLES_XML="$TRACES_DIR/counters_tables.xml"
    COUNTERS_JSON="$TRACES_DIR/counters.json"

    echo "Using template: ${TEMPLATE}"
    
    # Run xctrace with the binary
    N_ENV="$N" xcrun xctrace record \
      --template "${TEMPLATE}" \
      --time-limit "${DURATION}s" \
      --output "$TRACE" \
      --launch -- "$BIN" 2>/dev/null || {
        echo "xctrace failed, trying alternative approach..."
        # Alternative: attach to running process
        "$BIN" &
        PID=$!
        sleep 0.5
        xcrun xctrace record \
          --template "${TEMPLATE}" \
          --time-limit "$((DURATION-1))s" \
          --output "$TRACE" \
          --attach "$PID" 2>/dev/null || true
        wait "$PID"
    }

    if [ -f "$TRACE" ]; then
      echo "Exporting TOC..."
      xcrun xctrace export --input "$TRACE" --toc --output "$TOC_XML" 2>/dev/null || true

      echo "Exporting ALL tables (XML)..."
      # Try multiple XPath expressions
      for xpath in \
        '/trace-toc[1]/run[1]/data[1]/table' \
        '//table[@schema="cpu-counters"]' \
        '//table[@schema="counters"]' \
        '//table'; do
        xcrun xctrace export --input "$TRACE" \
          --xpath "$xpath" \
          --output "$TABLES_XML" 2>/dev/null && break
      done

      echo "Parsing counters..."
      python3 "$TRACES_DIR/../tools/summary.py" "$COUNTERS_JSON"
      
      echo "Trace saved: $TRACE"
    else
      echo "Failed to create trace file"
    fi
  fi
else
  echo
  echo "xctrace not available; skipping hardware counters."
fi

echo
echo "Artifacts in ./traces:"
ls -1 "$TRACES_DIR" 2>/dev/null || true