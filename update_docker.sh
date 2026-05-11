#!/usr/bin/env bash
set -euo pipefail

# Variáveis
IMAGE_NAME="ghcr.io/diogothomaz/rinhabackend2026:latest"
DOCKERFILE_PATH="api/Dockerfile"
CONTEXT_PATH="api"

# Função para exibir mensagens
log() {
  echo -e "\033[1;32m[INFO]\033[0m $1"
}

# Build da imagem
log "Construindo a imagem Docker..."
docker build -f "$DOCKERFILE_PATH" -t "$IMAGE_NAME" "$CONTEXT_PATH"

# Push da imagem
log "Fazendo push da imagem para o repositório..."
docker push "$IMAGE_NAME"

# Atualizar o docker-compose
log "Atualizando serviços com a nova imagem..."
docker compose pull
log "Recriando serviços..."
docker compose up -d

log "Processo concluído com sucesso!"