FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    pkg-config \
    libmicrohttpd-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app/
RUN make fraud_api preprocess_references

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libmicrohttpd12t64 \
    gzip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/fraud_api /app/fraud_api
COPY --from=build /app/preprocess_references /app/preprocess_references
COPY docker-entrypoint.sh /app/docker-entrypoint.sh
COPY docker-prepare-data.sh /app/docker-prepare-data.sh

RUN chmod +x /app/docker-entrypoint.sh /app/docker-prepare-data.sh

ENTRYPOINT ["/app/docker-entrypoint.sh"]
