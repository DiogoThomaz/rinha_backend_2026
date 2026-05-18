# Rinha Backend 2026 - Implementação Atual

Este repositório contém uma implementação inicial em C para o desafio de detecção de fraude por busca vetorial da Rinha Backend 2026.

## Objetivo da fase atual

Entregar uma base funcional, mensurável e pronta para evolução de performance:

- API respondendo na porta 9999.
- Vetorização completa em 14 dimensões conforme as regras oficiais.
- Pipeline de conversão de dataset para formato binário enxuto.
- Scripts para subir a API localmente e rodar benchmark.

## Técnicas utilizadas até o momento

### 1) API HTTP leve com libmicrohttpd (C)

- Servidor HTTP implementado em C com libmicrohttpd.
- Endpoints implementados:
  - GET /ready
  - POST /fraud-score
- Tratamento de corpo de requisição em chunks (acúmulo seguro antes de parse).

Motivação:
- Reduzir overhead de runtime e dependência de frameworks pesados.

### 2) Vetorização 14D conforme especificação

Implementado no fluxo do POST /fraud-score:
- Extração dos campos do payload JSON.
- Normalização com clamp no intervalo [0, 1].
- Tratamento especial de last_transaction = null com sentinela -1.

### 3) Busca k-NN inicial com distância euclidiana

- Estrutura atual usa busca linear sobre dataset carregado em memória.

### 4) Pre-processamento de dataset para binário

Utilitário criado:
- api/preprocess_references.c

### 5) Carregamento de configurações por arquivo

Arquivos usados:
- resources/normalization.json

### 6) Scripts de operação local

Scripts criados:
- run.sh: sobe a API (foreground/background), prepara dados e build.

## Status Atual

- API funcional localmente.
- Vetorização completa e endpoint de score respondendo JSON válido.

## Como executar rapidamente

### Subir API local

```bash
./run.sh
```

### Teste funcional rápido

```bash
./test.sh
```

### Benchmark

```bash
BENCHMARK_REQUESTS=1000 ./benchmark.sh
```

## Contêinerização (docker-compose)

Arquivos:
- docker-compose.yml

Topologia:
- lb (nginx) escutando na porta 9999
- api1
- api2

## Benchmark Local

[benchmark] Resultado
- requisições feitas: 10000
- sucesso nas requisições:  10000
- erros nas requisições:   0

---

O objetivo deste documento é auxiliar desenvolvedores e usuários a interagir com a API e entender a estrutura do projeto.