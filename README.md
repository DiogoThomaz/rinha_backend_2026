# Rinha Backend 2026 - Implementacao Atual

Este repositorio contem uma implementacao inicial em C para o desafio de deteccao de fraude por busca vetorial da Rinha Backend 2026.

## Objetivo da fase atual

Entregar uma base funcional, mensuravel e pronta para evolucao de performance:

- API respondendo na porta 9999.
- Vetorizacao completa em 14 dimensoes conforme as regras oficiais.
- Pipeline de conversao de dataset para formato binario enxuto.
- Scripts para subir a API localmente e rodar benchmark.

## Tecnicas utilizadas ate o momento

### 1) API HTTP leve com libmicrohttpd (C)

- Servidor HTTP implementado em C com libmicrohttpd.
- Endpoints implementados:
  - GET /ready
  - POST /fraud-score
- Tratamento de corpo de requisicao em chunks (acumulo seguro antes de parse).

Motivacao:

- Reduzir overhead de runtime e dependencia de frameworks pesados.
- Ter controle fino de alocacao e caminho quente de latencia.
- Manter alinhamento com objetivo de p99 baixo.

### 2) Vetorizacao 14D conforme especificacao

Implementado no fluxo do POST /fraud-score:

- Extracao dos campos do payload JSON.
- Normalizacao com clamp no intervalo [0, 1].
- Tratamento especial de last_transaction = null com sentinela -1 nas dimensoes 5 e 6.
- unknown_merchant calculado por busca em known_merchants.
- mcc_risk com fallback padrao 0.5 para MCC nao mapeado.
- Extracao de hora e dia da semana (UTC) e calculo de minutos desde a ultima transacao.

Motivacao:

- Garantir corretude funcional antes de otimizar agressivamente.
- Evitar divergencia silenciosa entre vetor de query e vetor de referencia.
- Preservar compatibilidade com a regra de score do desafio.

### 3) Busca k-NN inicial com distancia euclidiana

- Estrutura atual usa busca linear sobre dataset carregado em memoria.
- Heap de top-k (k=5) para manter os vizinhos mais proximos.
- Correcoes aplicadas na manutencao do heap e no calculo do score.

Motivacao:

- Estabelecer baseline simples e verificavel.
- Facilitar depuracao de corretude com menor complexidade inicial.
- Criar referencia de comparacao para futuras otimizacoes de indice.

### 4) Pre-processamento de dataset para binario

Utilitario criado:

- api/preprocess_references.c

Fluxo:

- Le referencias em JSON.
- Converte para formato binario compacto:
  - cabecalho: quantidade de registros (uint32)
  - corpo: float[14] + label (uint8) por registro

Motivacao:

- Evitar custo de parse JSON grande no caminho de execucao da API.
- Reduzir overhead de I/O e parse na inicializacao.
- Melhorar previsibilidade de uso de memoria.

### 5) Carregamento de configuracoes por arquivo

Arquivos usados:

- resources/normalization.json
- resources/mcc_risk.json

Comportamento:

- A API tenta carregar de DATA_DIR (default /data).
- Em ambiente local, possui fallback para resources.

Motivacao:

- Evitar constantes hardcoded.
- Facilitar execucao local e futura conteinerizacao.
- Manter regras alteraveis sem recompilar codigo.

### 6) Scripts de operacao local

Scripts criados:

- run.sh: sobe a API (foreground/background), prepara dados e build.
- test.sh: smoke test ponta a ponta.
- benchmark.sh: executa carga sequencial e reporta avg, p95, p99, min, max, rps, sucesso/erro.

Motivacao:

- Padronizar ciclo de desenvolvimento local.
- Permitir validacao rapida apos alteracoes.
- Medir impacto de mudancas em latencia desde cedo.

## Estado atual

Ja concluido:

- API funcional localmente.
- Vetorizacao completa e endpoint de score respondendo JSON valido.
- Pipeline de dataset em binario funcionando.
- Benchmark basico operacional.
- Stack docker-compose com load balancer + 2 instancias da API.

Ainda pendente:

- Otimizacao de busca vetorial para reduzir custo de consulta em escala.
- Suite automatizada mais robusta (regressao funcional + regressao de performance).

## Como executar rapidamente

### Subir API local

./run.sh

Ou em background:

BACKGROUND=1 ./run.sh

### Teste funcional rapido

./test.sh

### Benchmark

BENCHMARK_REQUESTS=1000 ./benchmark.sh

## Containerizacao (docker-compose)

Arquivos:

- docker-compose.yml
- api/Dockerfile
- api/docker-entrypoint.sh
- api/docker-prepare-data.sh
- nginx.conf

Topologia:

- lb (nginx) escutando na porta 9999
- api1
- api2
- data-prep (job one-shot para preparar /data/references.bin)

Comandos uteis:

- Subir stack completa:
  docker compose up -d --build

- Ver logs:
  docker compose logs -f

- Testar endpoint:
  curl http://127.0.0.1:9999/ready

- Derrubar stack:
  docker compose down

Observacoes importantes:

- O data-prep usa example-references.json por padrao (USE_FULL_DATASET=0) para manter compatibilidade com limites estritos de memoria nesta versao inicial.
- Para forcar references.json.gz no preparo, altere USE_FULL_DATASET para 1 no servico data-prep em docker-compose.yml.
- Com o KNN linear atual e duas instancias, usar dataset completo tende a exigir mais memoria do que o limite configurado; a proxima etapa e reduzir footprint/estrategia de busca para viabilizar isso dentro das restricoes.

## Principais motivacoes arquiteturais

1. Comecar simples para validar corretude e contrato da API.
2. Medir cedo para evitar otimizar no escuro.
3. Manter caminho de evolucao para tecnicas mais avancadas de indexacao vetorial.
4. Priorizar eficiencia de runtime em C para latencia baixa sob limite de recurso.
