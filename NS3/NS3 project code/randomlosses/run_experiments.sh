#!/usr/bin/env bash
# Run the random-loss TCP experiments and emit the combined CSVs used by the
# selected-results plots:
#   fig5.csv, fig6.csv, fig7.csv, fig8.csv

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "ERROR: Cannot find ns3 executable in $NS3_DIR"
    echo "       Make sure this script lives in <ns3-root>/scratch/randomlosses/"
    exit 1
fi

SIM="scratch/randomlosses/tcp-loosy-sim"

# Match the location of the provided CSVs.
OUTDIR="$NS3_DIR/scratch/randomlosses/results/main result"
RAWDIR="$OUTDIR/raw_runs"
mkdir -p "$OUTDIR" "$RAWDIR"

echo "ns-3 root : $NS3_DIR"
echo "Simulation: $SIM"
echo "CSV dir   : $OUTDIR"
echo "Raw dir   : $RAWDIR"
echo ""

declare -a VARIANT_KEYS=("reno" "sack" "fr" "gfr" "jitter")
declare -a VARIANT_LABELS=("Reno" "Sack" "FR" "GFR" "Jitter")
declare -a VARIANT_VALS=(
    "ns3::TcpNewReno"
    "ns3::TcpNewReno"
    "ns3::TcpFr"
    "ns3::TcpFrGfr"
    "ns3::TcpFrGfrJitter"
)
declare -a VARIANT_SACK=("false" "true" "false" "false" "false")

SIM_RESULTS_REL="scratch/randomlosses-results.txt"
SIM_TPUT_REL="scratch/randomlosses-throughput-vs-time.txt"
SIM_CWND_REL="scratch/randomlosses-cwnd.txt"
SIM_SSTHRESH_REL="scratch/randomlosses-ssthresh.txt"

run_one() {
    local key="$1"
    local label="$2"
    local variant="$3"
    local sack="$4"
    local conns="$5"
    local bw="$6"
    local delay="$7"
    local tag="$8"

    local result_copy="${RAWDIR}/${tag}-${key}-results.txt"
    local tput_copy="${RAWDIR}/${tag}-${key}-throughput-vs-time.txt"
    local cwnd_copy="${RAWDIR}/${tag}-${key}-cwnd.txt"
    local ssthresh_copy="${RAWDIR}/${tag}-${key}-ssthresh.txt"

    local cmd=(
        ./ns3 run "$SIM"
        "--"
        "--tcpVariant=${variant}"
        "--numConnections=${conns}"
        "--bandwidth=${bw}Mbps"
        "--delay=${delay}ms"
        "--simTime=200"
        "--enableSack=${sack}"
    )

    echo "  Running ${label}: conns=${conns} bw=${bw}Mbps delay=${delay}ms" >&2

    pushd "$NS3_DIR" > /dev/null
    rm -f "$SIM_RESULTS_REL" "$SIM_TPUT_REL" "$SIM_CWND_REL" "$SIM_SSTHRESH_REL"

    if ! "${cmd[@]}" > "${RAWDIR}/${tag}-${key}-stdout.log" 2> "${RAWDIR}/${tag}-${key}-stderr.log"; then
        echo "  ERROR: simulation crashed for ${label}. See ${RAWDIR}/${tag}-${key}-stderr.log" >&2
        popd > /dev/null
        return 1
    fi

    if [[ ! -f "$SIM_RESULTS_REL" ]]; then
        echo "  ERROR: simulator did not produce $SIM_RESULTS_REL" >&2
        popd > /dev/null
        return 1
    fi

    cp "$SIM_RESULTS_REL" "$result_copy"
    [[ -f "$SIM_TPUT_REL" ]] && cp "$SIM_TPUT_REL" "$tput_copy"
    [[ -f "$SIM_CWND_REL" ]] && cp "$SIM_CWND_REL" "$cwnd_copy"
    [[ -f "$SIM_SSTHRESH_REL" ]] && cp "$SIM_SSTHRESH_REL" "$ssthresh_copy"
    popd > /dev/null

    local goodput
    goodput="$(awk -F= '/^avgGoodput_Mbps=/{print $2; exit}' "$result_copy")"
    if [[ -z "$goodput" ]]; then
        echo "  ERROR: could not parse avgGoodput_Mbps from $result_copy" >&2
        return 1
    fi

    echo "  goodput = ${goodput} Mb/s" >&2
    printf '%s\n' "$goodput"
}

emit_fig8_csv() {
    local csv_out="$1"
    local tag="$2"

    local reno_file="${RAWDIR}/${tag}-reno-throughput-vs-time.txt"
    local sack_file="${RAWDIR}/${tag}-sack-throughput-vs-time.txt"
    local fr_file="${RAWDIR}/${tag}-fr-throughput-vs-time.txt"
    local gfr_file="${RAWDIR}/${tag}-gfr-throughput-vs-time.txt"
    local jitter_file="${RAWDIR}/${tag}-jitter-throughput-vs-time.txt"

    local tmpdir
    tmpdir="$(mktemp -d)"

    awk 'BEGIN{OFS=","} !/^#/ && NF >= 2 {print $1, $2}' "$reno_file" > "$tmpdir/reno.csv"
    awk 'BEGIN{OFS=","} !/^#/ && NF >= 2 {print $1, $2}' "$sack_file" > "$tmpdir/sack.csv"
    awk 'BEGIN{OFS=","} !/^#/ && NF >= 2 {print $1, $2}' "$fr_file" > "$tmpdir/fr.csv"
    awk 'BEGIN{OFS=","} !/^#/ && NF >= 2 {print $1, $2}' "$gfr_file" > "$tmpdir/gfr.csv"
    awk 'BEGIN{OFS=","} !/^#/ && NF >= 2 {print $1, $2}' "$jitter_file" > "$tmpdir/jitter.csv"

    {
        printf 'time_s,reno,sack,fr,gfr,jitter\n'
        paste -d, \
            "$tmpdir/reno.csv" \
            <(cut -d, -f2 "$tmpdir/sack.csv") \
            <(cut -d, -f2 "$tmpdir/fr.csv") \
            <(cut -d, -f2 "$tmpdir/gfr.csv") \
            <(cut -d, -f2 "$tmpdir/jitter.csv")
    } > "$csv_out"

    rm -rf "$tmpdir"
}

echo "======================================================="
echo "Figure 5: Goodput vs connections"
echo "======================================================="
FIG5_CSV="$OUTDIR/fig5.csv"
FIG5_BW=45
FIG5_DELAY=250
FIG5_CONNS=(1 5 10 20 30)
printf 'connections,%s\n' "$(IFS=,; echo "${VARIANT_KEYS[*]}")" > "$FIG5_CSV"

for C in "${FIG5_CONNS[@]}"; do
    row="$C"
    tag="fig5-c${C}-bw${FIG5_BW}-d${FIG5_DELAY}"
    for i in "${!VARIANT_KEYS[@]}"; do
        value="$(run_one \
            "${VARIANT_KEYS[$i]}" \
            "${VARIANT_LABELS[$i]}" \
            "${VARIANT_VALS[$i]}" \
            "${VARIANT_SACK[$i]}" \
            "$C" "$FIG5_BW" "$FIG5_DELAY" "$tag")"
        row="${row},${value}"
    done
    printf '%s\n' "$row" >> "$FIG5_CSV"
done

echo ""
echo "======================================================="
echo "Figure 6: Goodput vs bottleneck bandwidth"
echo "======================================================="
FIG6_CSV="$OUTDIR/fig6.csv"
FIG6_CONNS=10
FIG6_DELAY=250
FIG6_BWS=(2 40 150)
printf 'bandwidth_Mbps,%s\n' "$(IFS=,; echo "${VARIANT_KEYS[*]}")" > "$FIG6_CSV"

for BW in "${FIG6_BWS[@]}"; do
    row="$BW"
    tag="fig6-c${FIG6_CONNS}-bw${BW}-d${FIG6_DELAY}"
    for i in "${!VARIANT_KEYS[@]}"; do
        value="$(run_one \
            "${VARIANT_KEYS[$i]}" \
            "${VARIANT_LABELS[$i]}" \
            "${VARIANT_VALS[$i]}" \
            "${VARIANT_SACK[$i]}" \
            "$FIG6_CONNS" "$BW" "$FIG6_DELAY" "$tag")"
        row="${row},${value}"
    done
    printf '%s\n' "$row" >> "$FIG6_CSV"
done

echo ""
echo "======================================================="
echo "Figure 7: Goodput vs RTT"
echo "======================================================="
FIG7_CSV="$OUTDIR/fig7.csv"
FIG7_CONNS=10
FIG7_BW=45
FIG7_DELAYS=(5 25 50 250 500)
printf 'rtt_s,%s\n' "$(IFS=,; echo "${VARIANT_KEYS[*]}")" > "$FIG7_CSV"

for D in "${FIG7_DELAYS[@]}"; do
    RTT_S="$(awk "BEGIN {printf \"%.4f\", $D * 2 / 1000}")"
    row="$RTT_S"
    tag="fig7-c${FIG7_CONNS}-bw${FIG7_BW}-d${D}"
    for i in "${!VARIANT_KEYS[@]}"; do
        value="$(run_one \
            "${VARIANT_KEYS[$i]}" \
            "${VARIANT_LABELS[$i]}" \
            "${VARIANT_VALS[$i]}" \
            "${VARIANT_SACK[$i]}" \
            "$FIG7_CONNS" "$FIG7_BW" "$D" "$tag")"
        row="${row},${value}"
    done
    printf '%s\n' "$row" >> "$FIG7_CSV"
done

echo ""
echo "======================================================="
echo "Figure 8: Throughput vs time"
echo "======================================================="
FIG8_CSV="$OUTDIR/fig8.csv"
FIG8_CONNS=10
FIG8_BW=45
FIG8_DELAY=250
FIG8_TAG="fig8-c${FIG8_CONNS}-bw${FIG8_BW}-d${FIG8_DELAY}"

for i in "${!VARIANT_KEYS[@]}"; do
    run_one \
        "${VARIANT_KEYS[$i]}" \
        "${VARIANT_LABELS[$i]}" \
        "${VARIANT_VALS[$i]}" \
        "${VARIANT_SACK[$i]}" \
        "$FIG8_CONNS" "$FIG8_BW" "$FIG8_DELAY" "$FIG8_TAG" > /dev/null
done

emit_fig8_csv "$FIG8_CSV" "$FIG8_TAG"

echo ""
echo "======================================================="
echo "All experiments done."
echo "Generated:"
echo "  $FIG5_CSV"
echo "  $FIG6_CSV"
echo "  $FIG7_CSV"
echo "  $FIG8_CSV"
echo "======================================================="
