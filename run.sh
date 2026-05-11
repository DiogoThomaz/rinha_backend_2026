#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
API_DIR="$ROOT_DIR/api"
RES_DIR="$ROOT_DIR/resources"
WORK_DIR="${WORK_DIR:-/tmp/rinha_backend_2026_local}"
DATA_DIR="$WORK_DIR/data"
LOG_FILE="$WORK_DIR/api.log"
PID_FILE="$WORK_DIR/api.pid"

USE_FULL_DATASET="${USE_FULL_DATASET:-0}"
PREPARE_DATA="${PREPARE_DATA:-1}"
BUILD_API="${BUILD_API:-1}"
BACKGROUND="${BACKGROUND:-0}"
FORCE_REBUILD_DATA="${FORCE_REBUILD_DATA:-0}"

mkdir -p "$DATA_DIR"

if [[ "$PREPARE_DATA" == "1" ]]; then
  echo "[run] Preparando dados"
  make -C "$API_DIR" preprocess_references

  if [[ "$FORCE_REBUILD_DATA" == "1" || ! -f "$DATA_DIR/references.bin" ]]; then
    if [[ "$USE_FULL_DATASET" == "1" ]]; then
      if ! command -v gzip >/dev/null 2>&1; then
        echo "Erro: gzip nao encontrado para usar references.json.gz"
        exit 1
      fi
      gzip -dc "$RES_DIR/references.json.gz" | "$API_DIR/preprocess_references" - "$DATA_DIR/references.bin"
    else
      "$API_DIR/preprocess_references" "$RES_DIR/example-references.json" "$DATA_DIR/references.bin"
    fi
  else
    echo "[run] references.bin ja existe, reaproveitando"
  fi

  cp "$RES_DIR/normalization.json" "$DATA_DIR/normalization.json"
  cp "$RES_DIR/mcc_risk.json" "$DATA_DIR/mcc_risk.json"
fi

if [[ "$BUILD_API" == "1" ]]; then
  echo "[run] Build da API"
  make -C "$API_DIR" fraud_api
fi

if [[ -f "$PID_FILE" ]]; then
  existing_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [[ -n "$existing_pid" ]] && kill -0 "$existing_pid" >/dev/null 2>&1; then
    echo "[run] API ja em execucao (pid=$existing_pid)."
    echo "[run] Se quiser reiniciar: kill $existing_pid"
    exit 0
  fi
fi

if [[ "$BACKGROUND" == "1" ]]; then
  echo "[run] Subindo API em background"
  : > "$LOG_FILE"
  DATA_DIR="$DATA_DIR" "$API_DIR/fraud_api" >"$LOG_FILE" 2>&1 &
  api_pid=$!
  echo "$api_pid" > "$PID_FILE"

  for _ in $(seq 1 30); do
    if curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:9999/ready" | grep -qE '^2[0-9][0-9]$'; then
      break
    fi
    sleep 1
  done

  if curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:9999/ready" | grep -qE '^2[0-9][0-9]$'; then
    echo "[run] API pronta em http://127.0.0.1:9999 (pid=$api_pid)"
    echo "[run] Logs: $LOG_FILE"
  else
    echo "[run] API nao ficou pronta. Logs:"
    cat "$LOG_FILE"
    exit 1
  fi
else
  echo "[run] Subindo API em foreground (Ctrl+C para encerrar)"
  exec DATA_DIR="$DATA_DIR" "$API_DIR/fraud_api"
fi
