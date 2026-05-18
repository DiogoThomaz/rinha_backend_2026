# local_test

Diretório para manter instruções e resultados de testes locais do **rinha_backend_2026**.

## Como rodar (rápido)

### 1) Subir a API local

```bash
# prepara dados + build (pode levar alguns minutos)
./run.sh

# ou em background
BACKGROUND=1 ./run.sh
```

A API fica em: `http://127.0.0.1:9999`.

### 2) Rodar o teste básico

```bash
./test.sh
```

Opcionalmente, habilite benchmark dentro do `test.sh`:

```bash
BENCHMARK=1 BENCHMARK_REQUESTS=200 ./test.sh
```

### 3) Rodar benchmark separado (script benchmark.sh)

```bash
./benchmark.sh

# com quantidade/config
BENCHMARK_REQUESTS=10000 ./benchmark.sh
```

## Workdir padrão

Scripts passam a usar por padrão:

- `local_test/work/` (logs, pid e dataset binário)

Você pode sobrescrever via variável de ambiente:

```bash
WORK_DIR=./local_test/work ./run.sh
```