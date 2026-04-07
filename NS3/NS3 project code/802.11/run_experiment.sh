#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: could not find ./ns3 under $NS3_DIR" >&2
    exit 1
fi

SIM="scratch/wireless80211"
BUILD_TARGET="wireless80211"

RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR/results_wireless80211}"
RAW_DIR="$RESULTS_DIR/raw"

TCP_VARIANT="${TCP_VARIANT:-TcpFr}"
SIM_TIME="${SIM_TIME:-40}"
CLEAR_OLD="${CLEAR_OLD:-1}"

NODE_VALUES_CSV="${NODE_VALUES_CSV:-20,40,60,80}"
FLOW_VALUES_CSV="${FLOW_VALUES_CSV:-10,20,30,40,50}"
PPS_VALUES_CSV="${PPS_VALUES_CSV:-100,200,300,400,500}"
SPEED_VALUES_CSV="${SPEED_VALUES_CSV:-5,10,15,20,25}"

BASE_NODES="${BASE_NODES:-40}"
BASE_FLOWS="${BASE_FLOWS:-30}"
BASE_PPS="${BASE_PPS:-300}"
BASE_SPEED="${BASE_SPEED:-20}"

mkdir -p "$RESULTS_DIR" "$RAW_DIR"

if [[ "$CLEAR_OLD" == "1" ]]; then
    rm -f "$RESULTS_DIR"/nodes.csv \
          "$RESULTS_DIR"/flows.csv \
          "$RESULTS_DIR"/pps.csv \
          "$RESULTS_DIR"/speed.csv
    rm -f "$RAW_DIR"/*.txt
fi

IFS=',' read -r -a NODE_VALUES <<< "$NODE_VALUES_CSV"
IFS=',' read -r -a FLOW_VALUES <<< "$FLOW_VALUES_CSV"
IFS=',' read -r -a PPS_VALUES <<< "$PPS_VALUES_CSV"
IFS=',' read -r -a SPEED_VALUES <<< "$SPEED_VALUES_CSV"

SAFE_VARIANT="${TCP_VARIANT//[^A-Za-z0-9_-]/_}"

CSV_HEADER="vary_parameter,vary_value,tcp_variant,n_nodes,n_flows,packets_per_second,node_speed_mps,throughput_mbps,mean_delay_ms,pdr_pct,drop_ratio_pct,total_energy_j"

NODES_CSV="$RESULTS_DIR/nodes.csv"
FLOWS_CSV="$RESULTS_DIR/flows.csv"
PPS_CSV="$RESULTS_DIR/pps.csv"
SPEED_CSV="$RESULTS_DIR/speed.csv"

printf '%s\n' "$CSV_HEADER" > "$NODES_CSV"
printf '%s\n' "$CSV_HEADER" > "$FLOWS_CSV"
printf '%s\n' "$CSV_HEADER" > "$PPS_CSV"
printf '%s\n' "$CSV_HEADER" > "$SPEED_CSV"

extract_from_summary_line() {
    local key="$1"
    local summary_line="$2"
    printf '%s\n' "$summary_line" | tr ' ' '\n' | awk -F= -v target="$key" '$1 == target {print $2; exit}'
}

run_case() {
    local vary_parameter="$1"
    local vary_value="$2"
    local n_nodes="$3"
    local n_flows="$4"
    local pps="$5"
    local node_speed="$6"
    local csv_file="$7"

    local raw_file="$RAW_DIR/${vary_parameter}_${vary_value}_${SAFE_VARIANT}.txt"
    local run_spec="$SIM --tcpVariant=$TCP_VARIANT --nNodes=$n_nodes --nFlows=$n_flows --packetsPerSecond=$pps --nodeSpeed=$node_speed --simulationTime=$SIM_TIME"

    echo "=== $vary_parameter = $vary_value ==="
    echo "    variant=$TCP_VARIANT nNodes=$n_nodes nFlows=$n_flows pps=$pps speed=$node_speed"

    (
        cd "$NS3_DIR"
        ./ns3 run "$run_spec"
    ) | tee "$raw_file"

    local summary_line
    summary_line="$(grep '^variant=' "$raw_file" | tail -n 1 || true)"
    if [[ -z "$summary_line" ]]; then
        echo "ERROR: could not find summary line in $raw_file" >&2
        exit 1
    fi

    local throughput mean_delay pdr drop_ratio total_energy
    throughput="$(extract_from_summary_line throughput_mbps "$summary_line")"
    mean_delay="$(extract_from_summary_line mean_delay_ms "$summary_line")"
    pdr="$(extract_from_summary_line pdr_pct "$summary_line")"
    drop_ratio="$(extract_from_summary_line drop_ratio_pct "$summary_line")"
    total_energy="$(extract_from_summary_line total_energy_j "$summary_line")"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$vary_parameter" \
        "$vary_value" \
        "$TCP_VARIANT" \
        "$n_nodes" \
        "$n_flows" \
        "$pps" \
        "$node_speed" \
        "$throughput" \
        "$mean_delay" \
        "$pdr" \
        "$drop_ratio" \
        "$total_energy" >> "$csv_file"
}

echo "Building $BUILD_TARGET ..."
(cd "$NS3_DIR" && ./ns3 build "$BUILD_TARGET")

echo ""
echo "Running nodes sweep ..."
for value in "${NODE_VALUES[@]}"; do
    run_case "nodes" "$value" "$value" "$BASE_FLOWS" "$BASE_PPS" "$BASE_SPEED" "$NODES_CSV"
done

echo ""
echo "Running flows sweep ..."
for value in "${FLOW_VALUES[@]}"; do
    run_case "flows" "$value" "$BASE_NODES" "$value" "$BASE_PPS" "$BASE_SPEED" "$FLOWS_CSV"
done

echo ""
echo "Running pps sweep ..."
for value in "${PPS_VALUES[@]}"; do
    run_case "pps" "$value" "$BASE_NODES" "$BASE_FLOWS" "$value" "$BASE_SPEED" "$PPS_CSV"
done

echo ""
echo "Running speed sweep ..."
for value in "${SPEED_VALUES[@]}"; do
    run_case "speed" "$value" "$BASE_NODES" "$BASE_FLOWS" "$BASE_PPS" "$value" "$SPEED_CSV"
done

echo ""
echo "Generated CSV files:"
echo "  $NODES_CSV"
echo "  $FLOWS_CSV"
echo "  $PPS_CSV"
echo "  $SPEED_CSV"
