#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/local_test/work}"

URL_BASE="${URL_BASE:-http://127.0.0.1:9999}"
URL="$URL_BASE/fraud-score"
READY_URL="$URL_BASE/ready"
BENCHMARK_REQUESTS="${BENCHMARK_REQUESTS:-10000}"
PAYLOAD_FILE="${PAYLOAD_FILE:-}"

if ! command -v curl >/dev/null 2>&1; then
  echo "Erro: curl nao encontrado."
  exit 1
fi

if ! command -v sort >/dev/null 2>&1 || ! command -v awk >/dev/null 2>&1; then
  echo "Erro: sort e awk sao necessarios para benchmark."
  exit 1
fi

if ! [[ "$BENCHMARK_REQUESTS" =~ ^[0-9]+$ ]] || [[ "$BENCHMARK_REQUESTS" -lt 1 ]]; then
  echo "Erro: BENCHMARK_REQUESTS deve ser inteiro positivo."
  exit 1
fi

if ! curl -sS -o /dev/null -w "%{http_code}" "$READY_URL" | grep -qE '^2[0-9][0-9]$'; then
  echo "Erro: API nao esta pronta em $READY_URL"
  echo "Dica: rode primeiro BACKGROUND=1 ./run.sh"
  exit 1
fi

if [[ -n "$PAYLOAD_FILE" ]]; then
  if [[ ! -f "$PAYLOAD_FILE" ]]; then
    echo "Erro: PAYLOAD_FILE nao encontrado: $PAYLOAD_FILE"
    exit 1
  fi
  PAYLOAD="$(cat "$PAYLOAD_FILE")"
else
  PAYLOAD='{
    "id":"tx-bench-1",
    "transaction":{"amount":384.88,"installments":3,"requested_at":"2026-03-11T20:23:35Z"},
    "customer":{"avg_amount":769.76,"tx_count_24h":3,"known_merchants":["MERC-009","MERC-001"]},
    "merchant":{"id":"MERC-001","mcc":"5912","avg_amount":298.95},
    "terminal":{"is_online":false,"card_present":true,"km_from_home":13.7090520965},
    "last_transaction":{"timestamp":"2026-03-11T14:58:35Z","km_from_current":18.8626479774}
  }'
fi

mkdir -p "$WORK_DIR"
TIMES_FILE="$WORK_DIR/benchmark_times.txt"
SORTED_FILE="$WORK_DIR/benchmark_times_sorted.txt"
: > "$TIMES_FILE"

success_count=0
error_count=0
start_epoch_ns="$(date +%s%N)"

echo "[benchmark] Rodando $BENCHMARK_REQUESTS requisicoes em $URL"
for i in $(seq 1 "$BENCHMARK_REQUESTS"); do
  out="$(curl -sS -o /dev/null -w "%{time_total} %{http_code}" -X POST "$URL" -H "Content-Type: application/json" -d "$PAYLOAD" || true)"
  time_s="${out%% *}"
  code="${out##* }"

  if [[ "$time_s" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    echo "$time_s" >> "$TIMES_FILE"
  fi

  if [[ "$code" == "200" ]]; then
    success_count=$((success_count + 1))
  else
    error_count=$((error_count + 1))
  fi

  if (( i % 100 == 0 )) || (( i == BENCHMARK_REQUESTS )); then
    echo "[benchmark] progresso: $i/$BENCHMARK_REQUESTS"
  fi
done

end_epoch_ns="$(date +%s%N)"
duration_ns=$((end_epoch_ns - start_epoch_ns))
if [[ "$duration_ns" -le 0 ]]; then
  duration_ns=1
fi

sort -n "$TIMES_FILE" > "$SORTED_FILE"
sample_count="$(wc -l < "$SORTED_FILE" | tr -d ' ')"

if [[ "$sample_count" -eq 0 ]]; then
  echo "[benchmark] Nenhuma amostra coletada."
  exit 1
fi

p95_idx=$(((sample_count * 95 + 99) / 100))
p99_idx=$(((sample_count * 99 + 99) / 100))

min_s="$(head -n 1 "$SORTED_FILE")"
max_s="$(tail -n 1 "$SORTED_FILE")"
avg_s="$(awk '{s+=$1} END {if (NR>0) printf "%.6f", s/NR; else printf "0.000000"}' "$SORTED_FILE")"
p95_s="$(sed -n "${p95_idx}p" "$SORTED_FILE")"
p99_s="$(sed -n "${p99_idx}p" "$SORTED_FILE")"

avg_ms="$(awk -v v="$avg_s" 'BEGIN {printf "%.2f", v*1000}')"
p95_ms="$(awk -v v="$p95_s" 'BEGIN {printf "%.2f", v*1000}')"
p99_ms="$(awk -v v="$p99_s" 'BEGIN {printf "%.2f", v*1000}')"
min_ms="$(awk -v v="$min_s" 'BEGIN {printf "%.2f", v*1000}')"
max_ms="$(awk -v v="$max_s" 'BEGIN {printf "%.2f", v*1000}')"
rps="$(awk -v n="$BENCHMARK_REQUESTS" -v d="$duration_ns" 'BEGIN {printf "%.2f", (n*1000000000)/d}')"

echo
echo "[benchmark] Resultado"
echo "  requests: $BENCHMARK_REQUESTS"
echo "  success:  $success_count"
echo "  errors:   $error_count"
echo "  avg:      ${avg_ms} ms"
echo "  p95:      ${p95_ms} ms"
echo "  p99:      ${p99_ms} ms"
echo "  min:      ${min_ms} ms"
echo "  max:      ${max_ms} ms"
echo "  aprox rps:${rps}"
echo "  raw times: $TIMES_FILE"