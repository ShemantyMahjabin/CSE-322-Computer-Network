#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: could not find ./ns3 under $NS3_DIR" >&2
    exit 1
fi

SIM="scratch/wireless802.15.4/wireless802154"
BUILD_TARGET="wireless802154"

RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR/results_wireless802154}"
RAW_DIR="$RESULTS_DIR/raw"

TCP_VARIANT="${TCP_VARIANT:-TcpFr}"
SIM_TIME="${SIM_TIME:-20}"
CLEAR_OLD="${CLEAR_OLD:-1}"

NODE_VALUES_CSV="${NODE_VALUES_CSV:-20,40,60,80,100}"
FLOW_VALUES_CSV="${FLOW_VALUES_CSV:-10,20,30,40,50}"
PPS_VALUES_CSV="${PPS_VALUES_CSV:-100,200,300,400,500}"
COVERAGE_VALUES_CSV="${COVERAGE_VALUES_CSV:-1,2,3,4,5}"

BASE_COVERAGE="${BASE_COVERAGE:-5}"
BASE_PPS="${BASE_PPS:-400}"
BASE_FLOWS="${BASE_FLOWS:-50}"
BASE_NODES="${BASE_NODES:-10}"

mkdir -p "$RESULTS_DIR" "$RAW_DIR"

if [[ "$CLEAR_OLD" == "1" ]]; then
    rm -f "$RESULTS_DIR"/nodes.csv \
          "$RESULTS_DIR"/flows.csv \
          "$RESULTS_DIR"/pps.csv \
          "$RESULTS_DIR"/coverage.csv
    rm -f "$RAW_DIR"/*.txt
fi

IFS=',' read -r -a NODE_VALUES <<< "$NODE_VALUES_CSV"
IFS=',' read -r -a FLOW_VALUES <<< "$FLOW_VALUES_CSV"
IFS=',' read -r -a PPS_VALUES <<< "$PPS_VALUES_CSV"
IFS=',' read -r -a COVERAGE_VALUES <<< "$COVERAGE_VALUES_CSV"

SAFE_VARIANT="${TCP_VARIANT//[^A-Za-z0-9_-]/_}"

CSV_HEADER="vary_parameter,vary_value,tcp_variant,n_nodes,n_flows,packets_per_second,coverage_multiplier,simulation_time_s,throughput_mbps,mean_throughput_per_node_mbps,mean_delay_ms,pdr_pct,drop_ratio_pct,total_energy_j,mean_energy_per_node_j"

NODES_CSV="$RESULTS_DIR/nodes.csv"
FLOWS_CSV="$RESULTS_DIR/flows.csv"
PPS_CSV="$RESULTS_DIR/pps.csv"
COVERAGE_CSV="$RESULTS_DIR/coverage.csv"

printf '%s\n' "$CSV_HEADER" > "$NODES_CSV"
printf '%s\n' "$CSV_HEADER" > "$FLOWS_CSV"
printf '%s\n' "$CSV_HEADER" > "$PPS_CSV"
printf '%s\n' "$CSV_HEADER" > "$COVERAGE_CSV"

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
    local coverage="$6"
    local csv_file="$7"

    local raw_file="$RAW_DIR/${vary_parameter}_${vary_value}_${SAFE_VARIANT}.txt"
    local run_spec="$SIM --tcpVariant=$TCP_VARIANT --nNodes=$n_nodes --nFlows=$n_flows --packetsPerSecond=$pps --coverageMultiplier=$coverage --simulationTime=$SIM_TIME"

    echo "=== $vary_parameter = $vary_value ==="
    echo "    variant=$TCP_VARIANT nNodes=$n_nodes nFlows=$n_flows pps=$pps coverage=$coverage simTime=$SIM_TIME"

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

    local throughput mean_throughput_per_node mean_delay pdr drop_ratio total_energy mean_energy_per_node
    throughput="$(extract_from_summary_line throughput_mbps "$summary_line")"
    mean_throughput_per_node="$(extract_from_summary_line mean_throughput_per_node_mbps "$summary_line")"
    mean_delay="$(extract_from_summary_line mean_delay_ms "$summary_line")"
    pdr="$(extract_from_summary_line pdr_pct "$summary_line")"
    drop_ratio="$(extract_from_summary_line drop_ratio_pct "$summary_line")"
    total_energy="$(extract_from_summary_line total_energy_j "$summary_line")"
    mean_energy_per_node="$(extract_from_summary_line mean_energy_per_node_j "$summary_line")"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$vary_parameter" \
        "$vary_value" \
        "$TCP_VARIANT" \
        "$n_nodes" \
        "$n_flows" \
        "$pps" \
        "$coverage" \
        "$SIM_TIME" \
        "$throughput" \
        "$mean_throughput_per_node" \
        "$mean_delay" \
        "$pdr" \
        "$drop_ratio" \
        "$total_energy" \
        "$mean_energy_per_node" >> "$csv_file"
}

echo "Building $BUILD_TARGET ..."
(cd "$NS3_DIR" && ./ns3 build "$BUILD_TARGET")

echo ""
echo "Running nodes sweep ..."
for value in "${NODE_VALUES[@]}"; do
    run_case "nodes" "$value" "$value" "$BASE_FLOWS" "$BASE_PPS" "$BASE_COVERAGE" "$NODES_CSV"
done

echo ""
echo "Running flows sweep ..."
for value in "${FLOW_VALUES[@]}"; do
    run_case "flows" "$value" "$BASE_NODES" "$value" "$BASE_PPS" "$BASE_COVERAGE" "$FLOWS_CSV"
done

echo ""
echo "Running pps sweep ..."
for value in "${PPS_VALUES[@]}"; do
    run_case "pps" "$value" "$BASE_NODES" "$BASE_FLOWS" "$value" "$BASE_COVERAGE" "$PPS_CSV"
done

echo ""
echo "Running coverage sweep ..."
for value in "${COVERAGE_VALUES[@]}"; do
    run_case "coverage" "$value" "$BASE_NODES" "$BASE_FLOWS" "$BASE_PPS" "$value" "$COVERAGE_CSV"
done

echo ""
echo "Generated CSV files:"
echo "  $NODES_CSV"
echo "  $FLOWS_CSV"
echo "  $PPS_CSV"
echo "  $COVERAGE_CSV"
