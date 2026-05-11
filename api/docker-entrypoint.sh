#!/usr/bin/env bash
set -euo pipefail

DATA_DIR="${DATA_DIR:-/data}"

if [[ ! -f "$DATA_DIR/references.bin" ]]; then
  echo "Erro: $DATA_DIR/references.bin nao encontrado"
  exit 1
fi

if [[ ! -f "$DATA_DIR/normalization.json" ]]; then
  echo "Erro: $DATA_DIR/normalization.json nao encontrado"
  exit 1
fi

if [[ ! -f "$DATA_DIR/mcc_risk.json" ]]; then
  echo "Erro: $DATA_DIR/mcc_risk.json nao encontrado"
  exit 1
fi

exec /app/fraud_api
