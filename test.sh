#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
API_DIR="$ROOT_DIR/api"
RES_DIR="$ROOT_DIR/resources"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/local_test/work}"
DATA_DIR="$WORK_DIR/data"
LOG_FILE="$WORK_DIR/api.log"
PID_FILE="$WORK_DIR/api.pid"

USE_FULL_DATASET="${USE_FULL_DATASET:-0}"
BENCHMARK="${BENCHMARK:-0}"
BENCHMARK_REQUESTS="${BENCHMARK_REQUESTS:-200}"

if ! command -v curl >/dev/null 2>&1; then
  echo "Erro: curl nao encontrado."
  exit 1
fi

if ! command -v sort >/dev/null 2>&1 || ! command -v awk >/dev/null 2>&1; then
  echo "Erro: sort e awk sao necessarios para o script."
  exit 1
fi

if ! [[ "$BENCHMARK_REQUESTS" =~ ^[0-9]+$ ]] || [[ "$BENCHMARK_REQUESTS" -lt 1 ]]; then
  echo "Erro: BENCHMARK_REQUESTS deve ser inteiro positivo."
  exit 1
fi

mkdir -p "$DATA_DIR"

stop_managed_api() {
  if [[ -f "$PID_FILE" ]]; then
    local pid
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
  fi
}

cleanup() {
  stop_managed_api
}

trap cleanup EXIT

stop_managed_api
if curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:9999/ready" | grep -qE '^2[0-9][0-9]$'; then
  echo "Erro: ja existe uma API respondendo em http://127.0.0.1:9999"
  echo "Dica: encerre a API em execucao antes de rodar o teste para evitar falso positivo."
  exit 1
fi

echo "[1/6] Build do conversor de referencias"
make -C "$API_DIR" preprocess_references

echo "[2/6] Gerando dataset binario"
if [[ "$USE_FULL_DATASET" == "1" ]]; then
  if ! command -v gzip >/dev/null 2>&1; then
    echo "Erro: gzip nao encontrado para usar references.json.gz"
    exit 1
  fi
  gzip -dc "$RES_DIR/references.json.gz" | "$API_DIR/preprocess_references" - "$DATA_DIR/references.bin"
else
  "$API_DIR/preprocess_references" "$RES_DIR/example-references.json" "$DATA_DIR/references.bin"
fi

cp "$RES_DIR/normalization.json" "$DATA_DIR/normalization.json"
cp "$RES_DIR/mcc_risk.json" "$DATA_DIR/mcc_risk.json"

echo "[3/6] Build da API"
if ! make -C "$API_DIR" fraud_api; then
  echo "Erro no build da API."
  echo "Dica: instale a dependencia do sistema libmicrohttpd-dev."
  exit 1
fi

echo "[4/6] Subindo API local"
: > "$LOG_FILE"
DATA_DIR="$DATA_DIR" "$API_DIR/fraud_api" >"$LOG_FILE" 2>&1 &
API_PID=$!
echo "$API_PID" > "$PID_FILE"

echo "[5/6] Aguardando endpoint /ready"
for _ in $(seq 1 30); do
  if curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:9999/ready" | grep -qE '^2[0-9][0-9]$'; then
    break
  fi
  sleep 1
done

if ! curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:9999/ready" | grep -qE '^2[0-9][0-9]$'; then
  echo "API nao ficou pronta. Logs:"
  cat "$LOG_FILE"
  exit 1
fi

echo "[6/6] Testando POST /fraud-score"
PAYLOAD='{
  "id":"tx-local-1",
  "transaction":{"amount":384.88,"installments":3,"requested_at":"2026-03-11T20:23:35Z"},
  "customer":{"avg_amount":769.76,"tx_count_24h":3,"known_merchants":["MERC-009","MERC-001"]},
  "merchant":{"id":"MERC-001","mcc":"5912","avg_amount":298.95},
  "terminal":{"is_online":false,"card_present":true,"km_from_home":13.7090520965},
  "last_transaction":{"timestamp":"2026-03-11T14:58:35Z","km_from_current":18.8626479774}
}'

RESP="$(curl -sS -X POST "http://127.0.0.1:9999/fraud-score" -H "Content-Type: application/json" -d "$PAYLOAD")"

echo "Resposta da API:"
echo "$RESP"

if [[ "$BENCHMARK" == "1" ]]; then
  echo
  echo "[benchmark] Rodando $BENCHMARK_REQUESTS requisicoes sequenciais em /fraud-score"

  TIMES_FILE="$WORK_DIR/benchmark_times.txt"
  SORTED_FILE="$WORK_DIR/benchmark_times_sorted.txt"
  : > "$TIMES_FILE"

  success_count=0
  error_count=0
  start_epoch_ns="$(date +%s%N)"

  for i in $(seq 1 "$BENCHMARK_REQUESTS"); do
    out="$(curl -sS -o /dev/null -w "%{time_total} %{http_code}" -X POST "http://127.0.0.1:9999/fraud-score" -H "Content-Type: application/json" -d "$PAYLOAD" || true)"
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

    if (( i % 50 == 0 )) || (( i == BENCHMARK_REQUESTS )); then
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

  if [[ "$sample_count" -gt 0 ]]; then
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
  else
    echo "[benchmark] Nenhuma amostra coletada."
    exit 1
  fi
fi

echo
echo "Teste local concluído com sucesso."
echo "Logs da API: $LOG_FILE"
echo "Dados usados: $DATA_DIR"
if [[ "$USE_FULL_DATASET" == "1" ]]; then
  echo "Dataset: resources/references.json.gz"
else
  echo "Dataset: resources/example-references.json"
fi