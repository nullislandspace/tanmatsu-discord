#include "attach_upload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char TAG[] = "attach_upload";

#define BOUNDARY     "----TanmatsuFormBoundary7Ndg9Ksz"
#define CHUNK_SZ     4096
#define API_BASE     "https://discord.com/api/v10"

// Build a properly normalized "Bot <token>" header value (caller frees).
static char *make_auth(const char *token) {
    if (!token) return NULL;
    if (strncmp(token, "Bot ", 4) == 0) token += 4;
    while (*token == ' ') token++;
    size_t need = strlen(token) + 5;  // "Bot " + token + NUL
    char  *out  = malloc(need);
    if (!out) return NULL;
    snprintf(out, need, "Bot %s", token);
    return out;
}

// Last path component, e.g. "/sd/DCIM/IMG_001.jpg" -> "IMG_001.jpg"
static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

esp_err_t attach_upload_jpeg(const char *bot_token, const char *channel_id,
                             const char *jpeg_path, const char *caption) {
    if (!bot_token || !channel_id || !jpeg_path) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(jpeg_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", jpeg_path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return ESP_ERR_INVALID_SIZE; }

    const char *fname = basename_of(jpeg_path);
    char *auth        = make_auth(bot_token);
    if (!auth) { fclose(f); return ESP_ERR_NO_MEM; }

    // Pre-compose the part headers/footers so we can sum the total
    // Content-Length without buffering the file in RAM.
    char part_payload[256];
    int  payload_len = 0;
    if (caption && *caption) {
        payload_len = snprintf(part_payload, sizeof(part_payload),
            "--" BOUNDARY "\r\n"
            "Content-Disposition: form-data; name=\"payload_json\"\r\n"
            "Content-Type: application/json\r\n\r\n"
            "{\"content\":\"%s\"}\r\n", caption);
        if (payload_len < 0 || payload_len >= (int)sizeof(part_payload)) {
            payload_len = snprintf(part_payload, sizeof(part_payload),
                "--" BOUNDARY "\r\n"
                "Content-Disposition: form-data; name=\"payload_json\"\r\n"
                "Content-Type: application/json\r\n\r\n"
                "{}\r\n");
        }
    }

    char part_file[384];
    int  file_hdr_len = snprintf(part_file, sizeof(part_file),
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"files[0]\"; filename=\"%s\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n", fname);

    const char closing[] = "\r\n--" BOUNDARY "--\r\n";
    int closing_len      = (int)sizeof(closing) - 1;

    int total_len = payload_len + file_hdr_len + (int)fsz + closing_len;

    char url[256];
    snprintf(url, sizeof(url), "%s/channels/%s/messages", API_BASE, channel_id);

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 60000,
    };
    esp_http_client_handle_t hc = esp_http_client_init(&cfg);
    if (!hc) { fclose(f); free(auth); return ESP_FAIL; }

    esp_http_client_set_header(hc, "Authorization", auth);
    esp_http_client_set_header(hc, "User-Agent", "TanmatsuDiscord/0.1");
    esp_http_client_set_header(hc, "Content-Type",
                               "multipart/form-data; boundary=" BOUNDARY);

    esp_err_t r = esp_http_client_open(hc, total_len);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(r));
        esp_http_client_cleanup(hc);
        fclose(f); free(auth); return r;
    }

    if (payload_len > 0) esp_http_client_write(hc, part_payload, payload_len);
    esp_http_client_write(hc, part_file, file_hdr_len);

    // Heap-allocated chunk buffer — keep the task stack small so we don't
    // need a giant stack just to upload one file.
    uint8_t *buf = malloc(CHUNK_SZ);
    long     sent = 0;
    if (buf) {
        while (sent < fsz) {
            size_t want = fsz - sent;
            if (want > CHUNK_SZ) want = CHUNK_SZ;
            size_t got = fread(buf, 1, want, f);
            if (got == 0) break;
            int wn = esp_http_client_write(hc, (const char *)buf, got);
            if (wn < 0) { ESP_LOGE(TAG, "stream write failed"); break; }
            sent += wn;
        }
        free(buf);
    } else {
        ESP_LOGE(TAG, "chunk alloc failed");
    }
    fclose(f);

    esp_http_client_write(hc, closing, closing_len);

    int hdr = esp_http_client_fetch_headers(hc);
    int status = esp_http_client_get_status_code(hc);
    (void)hdr;

    // Drain any response body (and log on error).
    char respbuf[256];
    int  rd, total_resp = 0;
    while ((rd = esp_http_client_read(hc, respbuf,
                                      sizeof(respbuf) - 1)) > 0) {
        respbuf[rd] = '\0';
        if (status >= 400 && total_resp < 200) {
            ESP_LOGW(TAG, "resp: %.*s", rd, respbuf);
        }
        total_resp += rd;
    }

    esp_http_client_cleanup(hc);
    free(auth);

    if (status == 200 || status == 201) {
        ESP_LOGI(TAG, "uploaded %s (%ld B) → channel %s", fname, fsz, channel_id);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "upload failed, http %d", status);
    return ESP_FAIL;
}
