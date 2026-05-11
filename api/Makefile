CC ?= gcc
CFLAGS ?= -std=c11 -O3 -Wall -Wextra
CPPFLAGS += $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
LDLIBS += $(shell pkg-config --libs libmicrohttpd 2>/dev/null) -lm

TARGET := fraud_api
PREPROCESS_TARGET := preprocess_references
SOURCES := main.c dataset.c
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean deps

all: deps $(TARGET) $(PREPROCESS_TARGET)

deps:
	@pkg-config --exists libmicrohttpd || (echo "Erro: libmicrohttpd-dev nao encontrada. Instale a dependencia para compilar." && exit 1)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) $(PREPROCESS_TARGET)

$(PREPROCESS_TARGET): preprocess_references.c
	$(CC) -std=c11 -O3 -Wall -Wextra preprocess_references.c -o $@
