#include <ctype.h>
#include <math.h>
#include <microhttpd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dataset.h"

#define MAX_JSON_SIZE 65536
#define MAX_MCC_ENTRIES 256

typedef struct {
    double max_amount;
    double max_installments;
    double amount_vs_avg_ratio;
    double max_minutes;
    double max_km;
    double max_tx_count_24h;
    double max_merchant_avg_amount;
} normalization_t;

typedef struct {
    char code[8];
    double risk;
} mcc_entry_t;

typedef struct {
    dataset_t *dataset;
    normalization_t norm;
    mcc_entry_t mcc[MAX_MCC_ENTRIES];
    size_t mcc_count;
    int ready;
} app_ctx_t;

typedef struct {
    char *body;
    size_t len;
    size_t cap;
    int responded;
} request_ctx_t;

static const int DAYS_BEFORE_MONTH[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

static void skip_ws(const char *json, size_t *idx) {
    while (json[*idx] && isspace((unsigned char)json[*idx])) {
        (*idx)++;
    }
}

static int parse_json_string(const char *json, size_t start, size_t *value_start, size_t *value_end, size_t *next) {
    size_t i = start;
    if (json[i] != '"') return -1;
    i++;
    *value_start = i;
    while (json[i]) {
        if (json[i] == '\\') {
            if (!json[i + 1]) return -1;
            i += 2;
            continue;
        }
        if (json[i] == '"') {
            *value_end = i;
            *next = i + 1;
            return 0;
        }
        i++;
    }
    return -1;
}

static int find_value_end(const char *json, size_t start, size_t *end_out, int *type_out) {
    size_t i = start;
    int depth;
    skip_ws(json, &i);
    if (!json[i]) return -1;

    if (json[i] == '"') {
        size_t vs, ve, next;
        if (parse_json_string(json, i, &vs, &ve, &next) != 0) return -1;
        *end_out = next;
        *type_out = 's';
        return 0;
    }

    if (json[i] == '{') {
        depth = 1;
        i++;
        while (json[i] && depth > 0) {
            if (json[i] == '"') {
                size_t vs, ve, next;
                if (parse_json_string(json, i, &vs, &ve, &next) != 0) return -1;
                i = next;
                continue;
            }
            if (json[i] == '{') depth++;
            if (json[i] == '}') depth--;
            i++;
        }
        if (depth != 0) return -1;
        *end_out = i;
        *type_out = 'o';
        return 0;
    }

    if (json[i] == '[') {
        depth = 1;
        i++;
        while (json[i] && depth > 0) {
            if (json[i] == '"') {
                size_t vs, ve, next;
                if (parse_json_string(json, i, &vs, &ve, &next) != 0) return -1;
                i = next;
                continue;
            }
            if (json[i] == '[') depth++;
            if (json[i] == ']') depth--;
            i++;
        }
        if (depth != 0) return -1;
        *end_out = i;
        *type_out = 'a';
        return 0;
    }

    if (!strncmp(json + i, "true", 4)) {
        *end_out = i + 4;
        *type_out = 'b';
        return 0;
    }
    if (!strncmp(json + i, "false", 5)) {
        *end_out = i + 5;
        *type_out = 'b';
        return 0;
    }
    if (!strncmp(json + i, "null", 4)) {
        *end_out = i + 4;
        *type_out = 'n';
        return 0;
    }

    if (json[i] == '-' || isdigit((unsigned char)json[i])) {
        i++;
        while (json[i] && (isdigit((unsigned char)json[i]) || json[i] == '.' || json[i] == 'e' || json[i] == 'E' || json[i] == '+' || json[i] == '-')) {
            i++;
        }
        *end_out = i;
        *type_out = 'd';
        return 0;
    }

    return -1;
}

static int get_object_range(const char *json, size_t start, size_t end, const char *key, size_t *val_start, size_t *val_end, int *type) {
    size_t i = start;
    size_t ks, ke, next;
    size_t value_end;
    int value_type;

    skip_ws(json, &i);
    if (json[i] != '{') return -1;
    i++;

    while (i < end && json[i]) {
        skip_ws(json, &i);
        if (json[i] == '}') return -1;
        if (json[i] != '"') return -1;
        if (parse_json_string(json, i, &ks, &ke, &next) != 0) return -1;
        i = next;
        skip_ws(json, &i);
        if (json[i] != ':') return -1;
        i++;
        skip_ws(json, &i);
        if (find_value_end(json, i, &value_end, &value_type) != 0) return -1;

        if ((size_t)(ke - ks) == strlen(key) && !strncmp(json + ks, key, ke - ks)) {
            *val_start = i;
            *val_end = value_end;
            *type = value_type;
            return 0;
        }

        i = value_end;
        skip_ws(json, &i);
        if (json[i] == ',') {
            i++;
            continue;
        }
        if (json[i] == '}') break;
    }

    return -1;
}

static int get_number_from_object(const char *json, size_t start, size_t end, const char *key, double *out) {
    size_t vs, ve;
    int type;
    char buf[64];
    size_t len;

    if (get_object_range(json, start, end, key, &vs, &ve, &type) != 0 || type != 'd') return -1;
    len = ve - vs;
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, json + vs, len);
    buf[len] = '\0';
    *out = strtod(buf, NULL);
    return 0;
}

static int get_bool_from_object(const char *json, size_t start, size_t end, const char *key, int *out) {
    size_t vs, ve;
    int type;

    if (get_object_range(json, start, end, key, &vs, &ve, &type) != 0 || type != 'b') return -1;
    if ((ve - vs) == 4 && !strncmp(json + vs, "true", 4)) {
        *out = 1;
        return 0;
    }
    if ((ve - vs) == 5 && !strncmp(json + vs, "false", 5)) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int get_string_from_object(const char *json, size_t start, size_t end, const char *key, char *out, size_t out_size) {
    size_t vs, ve;
    size_t s, e, next;
    int type;
    size_t len;

    if (get_object_range(json, start, end, key, &vs, &ve, &type) != 0 || type != 's') return -1;
    if (parse_json_string(json, vs, &s, &e, &next) != 0) return -1;
    len = e - s;
    if (len + 1 > out_size) return -1;
    memcpy(out, json + s, len);
    out[len] = '\0';
    return 0;
}

static int array_contains_string(const char *json, size_t array_start, size_t array_end, const char *needle) {
    size_t i = array_start;
    size_t s, e, next;
    size_t len = strlen(needle);

    skip_ws(json, &i);
    if (json[i] != '[') return 0;
    i++;

    while (i < array_end && json[i]) {
        skip_ws(json, &i);
        if (json[i] == ']') break;
        if (json[i] != '"') return 0;
        if (parse_json_string(json, i, &s, &e, &next) != 0) return 0;
        if ((e - s) == len && !strncmp(json + s, needle, len)) return 1;
        i = next;
        skip_ws(json, &i);
        if (json[i] == ',') i++;
    }

    return 0;
}

static int is_leap_year(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int parse_iso_timestamp(const char *ts, int *year, int *month, int *day, int *hour, int *minute, int *second) {
    if (sscanf(ts, "%d-%d-%dT%d:%d:%dZ", year, month, day, hour, minute, second) != 6) return -1;
    if (*month < 1 || *month > 12 || *day < 1 || *day > 31) return -1;
    if (*hour < 0 || *hour > 23 || *minute < 0 || *minute > 59 || *second < 0 || *second > 59) return -1;
    return 0;
}

static int day_of_week_monday0(int year, int month, int day) {
    int days = DAYS_BEFORE_MONTH[month - 1] + day - 1;
    if (month > 2 && is_leap_year(year)) days += 1;
    int y = year - 1;
    int total = y * 365 + y / 4 - y / 100 + y / 400 + days;
    int w_sunday0 = (total + 1) % 7;
    return (w_sunday0 + 6) % 7;
}

static int64_t timestamp_to_seconds_utc(int year, int month, int day, int hour, int minute, int second) {
    int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

static float clamp01(double x) {
    if (x < 0.0) return 0.0f;
    if (x > 1.0) return 1.0f;
    return (float)x;
}

static int append_body(request_ctx_t *ctx, const char *chunk, size_t chunk_size) {
    size_t needed;
    char *new_ptr;

    if (chunk_size == 0) return 0;
    needed = ctx->len + chunk_size + 1;
    if (needed > MAX_JSON_SIZE) return -1;
    if (needed > ctx->cap) {
        size_t new_cap = ctx->cap ? ctx->cap * 2 : 1024;
        while (new_cap < needed) new_cap *= 2;
        new_ptr = (char *)realloc(ctx->body, new_cap);
        if (!new_ptr) return -1;
        ctx->body = new_ptr;
        ctx->cap = new_cap;
    }
    memcpy(ctx->body + ctx->len, chunk, chunk_size);
    ctx->len += chunk_size;
    ctx->body[ctx->len] = '\0';
    return 0;
}

static enum MHD_Result send_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *r = MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_MUST_COPY);
    enum MHD_Result ret;
    if (!r) return MHD_NO;
    MHD_add_response_header(r, "Content-Type", "application/json");
    ret = MHD_queue_response(connection, status, r);
    MHD_destroy_response(r);
    return ret;
}

static int load_file_to_buffer(const char *path, char **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[sz] = '\0';
    fclose(f);
    *out = buf;
    *out_size = (size_t)sz;
    return 0;
}

static int load_normalization(const char *path, normalization_t *norm) {
    char *json;
    size_t len;
    size_t root_s = 0;
    size_t root_e;

    if (load_file_to_buffer(path, &json, &len) != 0) return -1;
    root_e = len;

    if (get_number_from_object(json, root_s, root_e, "max_amount", &norm->max_amount) != 0 ||
        get_number_from_object(json, root_s, root_e, "max_installments", &norm->max_installments) != 0 ||
        get_number_from_object(json, root_s, root_e, "amount_vs_avg_ratio", &norm->amount_vs_avg_ratio) != 0 ||
        get_number_from_object(json, root_s, root_e, "max_minutes", &norm->max_minutes) != 0 ||
        get_number_from_object(json, root_s, root_e, "max_km", &norm->max_km) != 0 ||
        get_number_from_object(json, root_s, root_e, "max_tx_count_24h", &norm->max_tx_count_24h) != 0 ||
        get_number_from_object(json, root_s, root_e, "max_merchant_avg_amount", &norm->max_merchant_avg_amount) != 0) {
        free(json);
        return -1;
    }

    free(json);
    return 0;
}

static int load_mcc_risk(const char *path, app_ctx_t *app) {
    char *json;
    size_t len;
    size_t i = 0;
    app->mcc_count = 0;

    if (load_file_to_buffer(path, &json, &len) != 0) return -1;
    skip_ws(json, &i);
    if (json[i] != '{') {
        free(json);
        return -1;
    }
    i++;

    while (i < len && json[i]) {
        size_t ks, ke, next;
        size_t vs, ve;
        int type;
        size_t key_len;
        char num_buf[64];
        skip_ws(json, &i);
        if (json[i] == '}') break;
        if (json[i] != '"') {
            free(json);
            return -1;
        }
        if (parse_json_string(json, i, &ks, &ke, &next) != 0) {
            free(json);
            return -1;
        }
        i = next;
        skip_ws(json, &i);
        if (json[i] != ':') {
            free(json);
            return -1;
        }
        i++;
        vs = i;
        if (find_value_end(json, vs, &ve, &type) != 0 || type != 'd') {
            free(json);
            return -1;
        }

        if (app->mcc_count < MAX_MCC_ENTRIES) {
            key_len = ke - ks;
            if (key_len >= sizeof(app->mcc[app->mcc_count].code)) key_len = sizeof(app->mcc[app->mcc_count].code) - 1;
            memcpy(app->mcc[app->mcc_count].code, json + ks, key_len);
            app->mcc[app->mcc_count].code[key_len] = '\0';

            if ((ve - vs) >= sizeof(num_buf)) {
                free(json);
                return -1;
            }
            memcpy(num_buf, json + vs, ve - vs);
            num_buf[ve - vs] = '\0';
            app->mcc[app->mcc_count].risk = strtod(num_buf, NULL);
            app->mcc_count++;
        }

        i = ve;
        skip_ws(json, &i);
        if (json[i] == ',') i++;
    }

    free(json);
    return 0;
}

static double get_mcc_risk(const app_ctx_t *app, const char *mcc) {
    size_t i;
    for (i = 0; i < app->mcc_count; i++) {
        if (!strcmp(app->mcc[i].code, mcc)) {
            return app->mcc[i].risk;
        }
    }
    return 0.5;
}

static int vectorize(const app_ctx_t *app, const char *json_body, float *vec) {
    size_t root_s = 0;
    size_t root_e = strlen(json_body);
    size_t tx_s, tx_e, customer_s, customer_e, merchant_s, merchant_e, terminal_s, terminal_e;
    size_t last_s, last_e;
    size_t known_s, known_e;
    int last_type;
    int is_online, card_present;
    double amount, installments, customer_avg_amount, tx_count_24h, merchant_avg_amount;
    double km_from_home, km_from_current = 0.0;
    char requested_at[32], last_timestamp[32], merchant_id[64], merchant_mcc[16];
    int year, month, day, hour, minute, second;
    int ly, lm, ld, lh, lmin, ls;
    int day_of_week;
    int64_t request_secs, last_secs;
    double minutes_since_last = -1.0;
    int known_merchant;
    double amount_vs_avg;

    if (get_object_range(json_body, root_s, root_e, "transaction", &tx_s, &tx_e, &last_type) != 0 || last_type != 'o') return -1;
    if (get_object_range(json_body, root_s, root_e, "customer", &customer_s, &customer_e, &last_type) != 0 || last_type != 'o') return -1;
    if (get_object_range(json_body, root_s, root_e, "merchant", &merchant_s, &merchant_e, &last_type) != 0 || last_type != 'o') return -1;
    if (get_object_range(json_body, root_s, root_e, "terminal", &terminal_s, &terminal_e, &last_type) != 0 || last_type != 'o') return -1;

    if (get_number_from_object(json_body, tx_s, tx_e, "amount", &amount) != 0) return -1;
    if (get_number_from_object(json_body, tx_s, tx_e, "installments", &installments) != 0) return -1;
    if (get_string_from_object(json_body, tx_s, tx_e, "requested_at", requested_at, sizeof(requested_at)) != 0) return -1;

    if (get_number_from_object(json_body, customer_s, customer_e, "avg_amount", &customer_avg_amount) != 0) return -1;
    if (get_number_from_object(json_body, customer_s, customer_e, "tx_count_24h", &tx_count_24h) != 0) return -1;
    if (get_object_range(json_body, customer_s, customer_e, "known_merchants", &known_s, &known_e, &last_type) != 0 || last_type != 'a') return -1;

    if (get_string_from_object(json_body, merchant_s, merchant_e, "id", merchant_id, sizeof(merchant_id)) != 0) return -1;
    if (get_string_from_object(json_body, merchant_s, merchant_e, "mcc", merchant_mcc, sizeof(merchant_mcc)) != 0) return -1;
    if (get_number_from_object(json_body, merchant_s, merchant_e, "avg_amount", &merchant_avg_amount) != 0) return -1;

    if (get_bool_from_object(json_body, terminal_s, terminal_e, "is_online", &is_online) != 0) return -1;
    if (get_bool_from_object(json_body, terminal_s, terminal_e, "card_present", &card_present) != 0) return -1;
    if (get_number_from_object(json_body, terminal_s, terminal_e, "km_from_home", &km_from_home) != 0) return -1;

    if (parse_iso_timestamp(requested_at, &year, &month, &day, &hour, &minute, &second) != 0) return -1;
    day_of_week = day_of_week_monday0(year, month, day);
    request_secs = timestamp_to_seconds_utc(year, month, day, hour, minute, second);

    if (get_object_range(json_body, root_s, root_e, "last_transaction", &last_s, &last_e, &last_type) != 0) return -1;
    if (last_type == 'o') {
        if (get_string_from_object(json_body, last_s, last_e, "timestamp", last_timestamp, sizeof(last_timestamp)) != 0) return -1;
        if (get_number_from_object(json_body, last_s, last_e, "km_from_current", &km_from_current) != 0) return -1;
        if (parse_iso_timestamp(last_timestamp, &ly, &lm, &ld, &lh, &lmin, &ls) != 0) return -1;
        last_secs = timestamp_to_seconds_utc(ly, lm, ld, lh, lmin, ls);
        minutes_since_last = (double)(request_secs - last_secs) / 60.0;
        if (minutes_since_last < 0.0) minutes_since_last = 0.0;
    } else if (last_type != 'n') {
        return -1;
    }

    known_merchant = array_contains_string(json_body, known_s, known_e, merchant_id);

    vec[0] = clamp01(amount / app->norm.max_amount);
    vec[1] = clamp01(installments / app->norm.max_installments);
    if (customer_avg_amount <= 0.0) {
        amount_vs_avg = 1.0;
    } else {
        amount_vs_avg = amount / customer_avg_amount;
    }
    vec[2] = clamp01(amount_vs_avg / app->norm.amount_vs_avg_ratio);
    vec[3] = clamp01((double)hour / 23.0);
    vec[4] = clamp01((double)day_of_week / 6.0);

    if (last_type == 'n') {
        vec[5] = -1.0f;
        vec[6] = -1.0f;
    } else {
        vec[5] = clamp01(minutes_since_last / app->norm.max_minutes);
        vec[6] = clamp01(km_from_current / app->norm.max_km);
    }

    vec[7] = clamp01(km_from_home / app->norm.max_km);
    vec[8] = clamp01(tx_count_24h / app->norm.max_tx_count_24h);
    vec[9] = is_online ? 1.0f : 0.0f;
    vec[10] = card_present ? 1.0f : 0.0f;
    vec[11] = known_merchant ? 0.0f : 1.0f;
    vec[12] = clamp01(get_mcc_risk(app, merchant_mcc));
    vec[13] = clamp01(merchant_avg_amount / app->norm.max_merchant_avg_amount);

    return 0;
}

static void request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    request_ctx_t *ctx = (request_ctx_t *)*con_cls;
    (void)cls;
    (void)connection;
    (void)toe;
    if (!ctx) return;
    free(ctx->body);
    free(ctx);
    *con_cls = NULL;
}

static enum MHD_Result handle_request(void *cls,
                                      struct MHD_Connection *connection,
                                      const char *url,
                                      const char *method,
                                      const char *version,
                                      const char *upload_data,
                                      size_t *upload_data_size,
                                      void **con_cls) {
    app_ctx_t *app = (app_ctx_t *)cls;
    request_ctx_t *req = (request_ctx_t *)*con_cls;
    float query_vec[14];
    float score;
    int approved;
    char response[128];
    (void)version;

    if (!req) {
        req = (request_ctx_t *)calloc(1, sizeof(*req));
        if (!req) return MHD_NO;
        *con_cls = req;
        return MHD_YES;
    }

    if (!strcmp(method, "GET") && !strcmp(url, "/ready")) {
        if (req->responded) return MHD_YES;
        req->responded = 1;
        if (!app->ready) {
            return send_json(connection, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"status\":\"starting\"}");
        }
        return send_json(connection, MHD_HTTP_OK, "{\"status\":\"ok\"}");
    }

    if (strcmp(method, "POST") || strcmp(url, "/fraud-score")) {
        if (req->responded) return MHD_YES;
        req->responded = 1;
        return send_json(connection, MHD_HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
    }

    if (*upload_data_size > 0) {
        if (append_body(req, upload_data, *upload_data_size) != 0) {
            req->responded = 1;
            *upload_data_size = 0;
            return send_json(connection, MHD_HTTP_CONTENT_TOO_LARGE, "{\"error\":\"payload too large\"}");
        }
        *upload_data_size = 0;
        return MHD_YES;
    }

    if (req->responded) return MHD_YES;
    req->responded = 1;

    if (!app->ready || !req->body || vectorize(app, req->body, query_vec) != 0) {
        return send_json(connection, MHD_HTTP_BAD_REQUEST, "{\"error\":\"invalid json\"}");
    }

    score = find_fraud_score(app->dataset, query_vec);
    approved = score < 0.6f;
    snprintf(response, sizeof(response), "{\"approved\":%s,\"fraud_score\":%.1f}", approved ? "true" : "false", score);
    return send_json(connection, MHD_HTTP_OK, response);
}

int main(void) {
    dataset_t ds;
    app_ctx_t app;
    struct MHD_Daemon *daemon;
    const char *data_dir;
    char dataset_path[256];
    char norm_path[256];
    char mcc_path[256];

    memset(&app, 0, sizeof(app));
    app.dataset = &ds;

    data_dir = getenv("DATA_DIR");
    if (!data_dir || !data_dir[0]) {
        data_dir = "/data";
    }

    snprintf(dataset_path, sizeof(dataset_path), "%s/references.bin", data_dir);
    snprintf(norm_path, sizeof(norm_path), "%s/normalization.json", data_dir);
    snprintf(mcc_path, sizeof(mcc_path), "%s/mcc_risk.json", data_dir);

    if (load_dataset(dataset_path, &ds) != 0) {
        fprintf(stderr, "Erro ao carregar dataset %s\n", dataset_path);
        return 1;
    }

    if (load_normalization(norm_path, &app.norm) != 0) {
        if (load_normalization("../resources/normalization.json", &app.norm) != 0) {
            fprintf(stderr, "Erro ao carregar normalization.json\n");
            free_dataset(&ds);
            return 1;
        }
    }

    if (load_mcc_risk(mcc_path, &app) != 0) {
        if (load_mcc_risk("../resources/mcc_risk.json", &app) != 0) {
            fprintf(stderr, "Erro ao carregar mcc_risk.json\n");
            free_dataset(&ds);
            return 1;
        }
    }

    app.ready = 1;

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        9999,
        NULL,
        NULL,
        &handle_request,
        &app,
        MHD_OPTION_THREAD_POOL_SIZE,
        2,
        MHD_OPTION_NOTIFY_COMPLETED,
        &request_completed,
        NULL,
        MHD_OPTION_END);

    if (!daemon) {
        free_dataset(&ds);
        return 1;
    }

    printf("Servidor rodando na porta 9999...\n");
    fflush(stdout);
    while (1) {
        sleep(3600);
    }

    MHD_stop_daemon(daemon);
    free_dataset(&ds);
    return 0;
}