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

URL_BASE="${URL_BASE:-http://127.0.0.1:9999}"
URLREADY="$URL_BASE/ready"

if [[ "${BACKGROUND:-0}" == "1" ]]; then
  echo "[run] Subindo API em background"
else
  echo "[run] Subindo API em foreground (Ctrl+C para encerrar)"
fi

mkdir -p "$WORK_DIR"
mkdir -p "$DATA_DIR"

if [[ ! -f "$DATA_DIR/resources.references.json" ]] && [[ -f "$RES_DIR/example-references.json" ]]; then
  cp "$RES_DIR/example-references.json" "$DATA_DIR/resources.references.json"
fi

if [[ ! -f "$DATA_DIR/normalization.json" ]] && [[ -f "$RES_DIR/normalization.json" ]]; then
  cp "$RES_DIR/normalization.json" "$DATA_DIR/normalization.json"
fi

if [[ "${BUILD:-1}" == "1" ]]; then
  echo "[run] Build da API"
  (cd "$API_DIR" && ./build.sh)
fi

if ! curl -sS -o /dev/null -w "%{http_code}" "$URLREADY" | grep -qE '^2[0-9][0-9]$'; then
  if [[ "${BACKGROUND:-0}" == "1" ]]; then
    rm -f "$PID_FILE" "$LOG_FILE"
    # shellcheck disable=SC2002
    (exec DATA_DIR="$DATA_DIR" LOG_FILE="$LOG_FILE" "$API_DIR/fraud_api" "$URL_BASE" >/dev/null 2>&1 &) 
    # captura pid
    sleep 0.2
    # se o binario nao escreve pid, entao tentamos inferir pelo log.
    pids=$(pgrep -f "$API_DIR/fraud_api" || true)
    if [[ -n "$pids" ]]; then
      echo "$pids" | head -n 1 > "$PID_FILE"
    fi
  else
    exec DATA_DIR="$DATA_DIR" "$API_DIR/fraud_api" "$URL_BASE"
  fi
fi

# loop de wait para ready
for _ in $(seq 1 60); do
  if curl -sS -o /dev/null -w "%{http_code}" "$URLREADY" | grep -qE '^2[0-9][0-9]$'; then
    echo "[run] API pronta em $URLREADY"
    break
  fi
  sleep 0.2
  echo "[run] Aguardando pronta..."
done