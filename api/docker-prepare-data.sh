#!/usr/bin/env bash
set -euo pipefail

DATA_DIR="${DATA_DIR:-/data}"
RESOURCES_DIR="${RESOURCES_DIR:-/resources}"
USE_FULL_DATASET="${USE_FULL_DATASET:-0}"

mkdir -p "$DATA_DIR"

if [[ ! -f "$RESOURCES_DIR/normalization.json" ]]; then
  echo "Erro: normalization.json nao encontrado em $RESOURCES_DIR"
  exit 1
fi

if [[ ! -f "$RESOURCES_DIR/mcc_risk.json" ]]; then
  echo "Erro: mcc_risk.json nao encontrado em $RESOURCES_DIR"
  exit 1
fi

cp "$RESOURCES_DIR/normalization.json" "$DATA_DIR/normalization.json"
cp "$RESOURCES_DIR/mcc_risk.json" "$DATA_DIR/mcc_risk.json"

if [[ ! -f "$DATA_DIR/references.bin" ]]; then
  if [[ "$USE_FULL_DATASET" == "1" && -f "$RESOURCES_DIR/references.json.gz" ]]; then
    echo "Preparando references.bin a partir de references.json.gz..."
    gzip -dc "$RESOURCES_DIR/references.json.gz" | /app/preprocess_references - "$DATA_DIR/references.bin"
  elif [[ -f "$RESOURCES_DIR/example-references.json" ]]; then
    echo "Preparando references.bin a partir de example-references.json..."
    /app/preprocess_references "$RESOURCES_DIR/example-references.json" "$DATA_DIR/references.bin"
  elif [[ -f "$RESOURCES_DIR/references.json.gz" ]]; then
    echo "example-references.json nao encontrado; usando references.json.gz..."
    gzip -dc "$RESOURCES_DIR/references.json.gz" | /app/preprocess_references - "$DATA_DIR/references.bin"
  else
    echo "Erro: nenhum arquivo de referencias encontrado em $RESOURCES_DIR"
    exit 1
  fi
else
  echo "references.bin ja existe em $DATA_DIR, reaproveitando."
fi

echo "Dataset preparado com sucesso em $DATA_DIR"
