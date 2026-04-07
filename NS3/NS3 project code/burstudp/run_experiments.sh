#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: could not find ./ns3 under $NS3_DIR" >&2
    exit 1
fi

SIM="scratch/bursty-udp-topology"
BUILD_TARGET="bursty-udp-topology"

RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR}"
RAW_DIR="${RAW_DIR:-$RESULTS_DIR/raw}"
CLEAR_OLD="${CLEAR_OLD:-1}"
GENERATE_PLOTS="${GENERATE_PLOTS:-0}"

VARIANTS_CSV="${VARIANTS_CSV:-Reno,Sack,FR,GFR,JA-GFR}"

CONNECTION_VALUES_CSV="${CONNECTION_VALUES_CSV:-1,5,10,20,30}"
BANDWIDTH_VALUES_CSV="${BANDWIDTH_VALUES_CSV:-1,45,150}"
RTT_VALUES_CSV="${RTT_VALUES_CSV:-0.01,0.05,0.1,0.5,1.0}"

BASE_NUM_CONNECTIONS="${BASE_NUM_CONNECTIONS:-10}"
BASE_BOTTLENECK_BW="${BASE_BOTTLENECK_BW:-45}"
BASE_RTT="${BASE_RTT:-0.5}"
SIM_TIME="${SIM_TIME:-200}"

FIG2_CSV="$RESULTS_DIR/fig2_connections.csv"
FIG3_CSV="$RESULTS_DIR/fig3_bandwidth.csv"
FIG4_CSV="$RESULTS_DIR/fig4_rtt.csv"

mkdir -p "$RESULTS_DIR" "$RAW_DIR"

if [[ "$CLEAR_OLD" == "1" ]]; then
    rm -f "$FIG2_CSV" "$FIG3_CSV" "$FIG4_CSV"
    rm -f "$RAW_DIR"/*.log
fi

: > "$FIG2_CSV"
: > "$FIG3_CSV"
: > "$FIG4_CSV"

IFS=',' read -r -a VARIANTS <<< "$VARIANTS_CSV"

run_sweep() {
    local experiment="$1"
    local tcp_variant="$2"
    local output_csv="$3"
    local extra_args="$4"

    local safe_variant="${tcp_variant//[^A-Za-z0-9_-]/_}"
    local raw_log="$RAW_DIR/${experiment}_${safe_variant}.log"
    local run_spec="$SIM --experiment=$experiment --tcpVariant=$tcp_variant --simTime=$SIM_TIME --outputFile=$output_csv $extra_args"

    echo "=== experiment=$experiment variant=$tcp_variant ==="

    (
        cd "$NS3_DIR"
        ./ns3 run "$run_spec"
    ) | tee "$raw_log"
}

echo "Building $BUILD_TARGET ..."
(cd "$NS3_DIR" && ./ns3 build "$BUILD_TARGET")

for variant in "${VARIANTS[@]}"; do
    run_sweep \
        "connections" \
        "$variant" \
        "$FIG2_CSV" \
        "--bottleneckBw=$BASE_BOTTLENECK_BW --Rtt=$BASE_RTT --connectionList=$CONNECTION_VALUES_CSV"

    run_sweep \
        "bandwidth" \
        "$variant" \
        "$FIG3_CSV" \
        "--numConnections=$BASE_NUM_CONNECTIONS --Rtt=$BASE_RTT --bandwidthList=$BANDWIDTH_VALUES_CSV"

    run_sweep \
        "rtt" \
        "$variant" \
        "$FIG4_CSV" \
        "--numConnections=$BASE_NUM_CONNECTIONS --bottleneckBw=$BASE_BOTTLENECK_BW --rttList=$RTT_VALUES_CSV"
done

if [[ "$GENERATE_PLOTS" == "1" ]]; then
    echo ""
    echo "Generating plots ..."
    (
        cd "$NS3_DIR"
        python3 scratch/burstyudp/plot_results.py --base "$RESULTS_DIR"
    )
fi

echo ""
echo "Generated CSV files:"
echo "  $FIG2_CSV"
echo "  $FIG3_CSV"
echo "  $FIG4_CSV"
