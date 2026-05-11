#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float vector[14];
    uint8_t label;
} reference_t;

static int read_all(FILE *in, char **out_buf, size_t *out_len) {
    size_t cap = 1 << 20;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;

    while (!feof(in)) {
        size_t n;
        if (len == cap) {
            char *new_buf;
            cap *= 2;
            new_buf = (char *)realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                return -1;
            }
            buf = new_buf;
        }
        n = fread(buf + len, 1, cap - len, in);
        len += n;
        if (ferror(in)) {
            free(buf);
            return -1;
        }
    }

    if (len == cap) {
        char *new_buf = (char *)realloc(buf, cap + 1);
        if (!new_buf) {
            free(buf);
            return -1;
        }
        buf = new_buf;
    }
    buf[len] = '\0';
    *out_buf = buf;
    *out_len = len;
    return 0;
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int parse_vector14(const char *start, float out[14], const char **next_pos) {
    const char *p = start;
    char *endptr;
    int i;

    skip_ws(&p);
    if (*p != '[') return -1;
    p++;

    for (i = 0; i < 14; i++) {
        skip_ws(&p);
        out[i] = strtof(p, &endptr);
        if (endptr == p) return -1;
        p = endptr;
        skip_ws(&p);
        if (i < 13) {
            if (*p != ',') return -1;
            p++;
        }
    }

    skip_ws(&p);
    if (*p != ']') return -1;
    p++;

    *next_pos = p;
    return 0;
}

static int parse_label(const char *start, uint8_t *label, const char **next_pos) {
    const char *p = start;
    const char *q;

    q = strstr(p, "\"label\"");
    if (!q) return -1;
    p = q + strlen("\"label\"");

    while (*p && *p != ':') p++;
    if (*p != ':') return -1;
    p++;

    skip_ws(&p);
    if (*p != '"') return -1;
    p++;

    if (!strncmp(p, "fraud\"", 6)) {
        *label = 1;
        p += 6;
    } else if (!strncmp(p, "legit\"", 6)) {
        *label = 0;
        p += 6;
    } else {
        return -1;
    }

    *next_pos = p;
    return 0;
}

int main(int argc, char **argv) {
    FILE *in;
    FILE *out;
    char *json = NULL;
    size_t json_len = 0;
    const char *p;
    uint32_t count = 0;

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <input.json|-> <output.bin>\n", argv[0]);
        fprintf(stderr, "Dica para .gz: gzip -dc references.json.gz | %s - references.bin\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "-")) {
        in = stdin;
    } else {
        in = fopen(argv[1], "rb");
        if (!in) {
            perror("fopen input");
            return 1;
        }
    }

    if (read_all(in, &json, &json_len) != 0) {
        fprintf(stderr, "Erro ao ler input\n");
        if (in != stdin) fclose(in);
        return 1;
    }

    if (in != stdin) fclose(in);

    out = fopen(argv[2], "wb");
    if (!out) {
        perror("fopen output");
        free(json);
        return 1;
    }

    if (fwrite(&count, sizeof(count), 1, out) != 1) {
        fprintf(stderr, "Erro ao escrever cabecalho\n");
        fclose(out);
        free(json);
        return 1;
    }

    p = json;
    while (1) {
        const char *vkey = strstr(p, "\"vector\"");
        reference_t ref;
        const char *after_vector;
        const char *after_label;

        if (!vkey) break;
        p = vkey + strlen("\"vector\"");

        while (*p && *p != ':') p++;
        if (*p != ':') {
            fprintf(stderr, "Formato invalido: sem ':' em vector\n");
            fclose(out);
            free(json);
            return 1;
        }
        p++;

        if (parse_vector14(p, ref.vector, &after_vector) != 0) {
            fprintf(stderr, "Formato invalido ao ler vetor\n");
            fclose(out);
            free(json);
            return 1;
        }

        if (parse_label(after_vector, &ref.label, &after_label) != 0) {
            fprintf(stderr, "Formato invalido ao ler label\n");
            fclose(out);
            free(json);
            return 1;
        }

        if (fwrite(ref.vector, sizeof(float), 14, out) != 14 ||
            fwrite(&ref.label, sizeof(uint8_t), 1, out) != 1) {
            fprintf(stderr, "Erro ao escrever registro\n");
            fclose(out);
            free(json);
            return 1;
        }

        count++;
        p = after_label;
    }

    if (fseek(out, 0, SEEK_SET) != 0 || fwrite(&count, sizeof(count), 1, out) != 1) {
        fprintf(stderr, "Erro ao atualizar cabecalho\n");
        fclose(out);
        free(json);
        return 1;
    }

    fclose(out);
    free(json);

    fprintf(stdout, "Conversao concluida. Registros: %u\n", count);
    return 0;
}
