#!/bin/bash
set -euo pipefail

# Script para deploy da aplicação com suporte a gzip

log() {
  echo -e "\033[1;32m[INFO]\033[0m $1"
}

error() {
  echo -e "\033[1;31m[ERRO]\033[0m $1" >&2
}

warn() {
  echo -e "\033[1;33m[AVISO]\033[0m $1"
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="ghcr.io/diogothomaz/rinhabackend2026:latest"

# Usar newgrp docker para evitar problemas de permissão
newgrp docker <<'EOF_DOCKER' || {
  error "Falha ao executar comandos Docker. Verifique permissões."
  exit 1
}

log "Build da imagem Docker..."
docker build -f "$ROOT_DIR/api/Dockerfile" -t "$IMAGE_NAME" "$ROOT_DIR/api/"
log "Build concluído!"

log "Push da imagem para GHCR..."
if docker push "$IMAGE_NAME"; then
  log "Push concluído com sucesso!"
else
  warn "Push falhou. Verifique autenticação GHCR."
  warn "Dica: execute 'gh auth login' e configure credenciais"
fi

log "Limpando containers antigos..."
docker compose -f "$ROOT_DIR/docker-compose.yml" rm -f || true

log "Iniciando serviços com a nova imagem..."
docker compose -f "$ROOT_DIR/docker-compose.yml" up -d

log "Aguardando serviços ficarem prontos..."
sleep 10

log "Status dos serviços:"
docker compose -f "$ROOT_DIR/docker-compose.yml" ps

log "Testando endpoint /ready..."
if curl -s -f http://127.0.0.1:9999/ready >/dev/null; then
  log "Serviços estão prontos e respondendo!"
else
  warn "Serviços ainda estão inicializando. Aguarde alguns segundos."
fi

log "Deploy concluído!"

EOF_DOCKER
