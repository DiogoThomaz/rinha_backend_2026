#!/bin/bash
set -euo pipefail

# Usar newgrp docker para evitar problemas de permissão
newgrp docker <<'DOCKER_CMD'
echo "[INFO] Obtendo lista de containers..."
CONTAINERS=$(docker ps -a --filter "name=rinha_backend" --format "{{.ID}}")

if [[ -n "$CONTAINERS" ]]; then
    echo "[INFO] Matando containers Rinha..."
    echo "$CONTAINERS" | while read -r cid; do
        docker kill "$cid" 2>/dev/null || true
        docker rm -f "$cid" 2>/dev/null || true
    done
    sleep 2
fi

echo "[INFO] Iniciando serviços com a nova imagem..."
docker compose up -d

echo "[INFO] Aguardando serviços ficarem prontos..."
sleep 15

echo "[INFO] Status dos serviços:"
docker compose ps

echo "[INFO] Testando endpoint /ready..."
curl -s http://127.0.0.1:9999/ready || echo "Ainda inicializando..."
DOCKER_CMD

echo "[INFO] Serviços reiniciados com sucesso!"
