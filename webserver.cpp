#include "webserver.h"
#include "config.h"
#include "camera.h"
#include "network.h"
#include "boot.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"

// Dois servidores httpd (padrão CameraWebServer Espressif):
// - s_httpd        porta 80: UI, /status, /control
// - s_stream_httpd porta 81: /stream (não bloqueia a API)
static httpd_handle_t s_httpd = nullptr;
static httpd_handle_t s_stream_httpd = nullptr;
static bool s_started = false;

static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY;
static const char STREAM_BOUNDARY_LINE[] = "\r\n--" STREAM_BOUNDARY "\r\n";
static const char STREAM_PART_FMT[] =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Página principal — stream na porta 81; status/control na 80
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>ESP32-CAM Stream</title>
<style>
:root{--bg:#0f1419;--card:#1a2332;--text:#e7ecf3;--muted:#8b9bb4;--acc:#3d8bfd;--ok:#3dd68c;--err:#ff6b6b;--border:#2a3548}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);min-height:100vh}
header{padding:1rem 1.25rem;border-bottom:1px solid var(--border);display:flex;flex-wrap:wrap;gap:.75rem;align-items:center;justify-content:space-between}
h1{margin:0;font-size:1.15rem;font-weight:600}
.badge{font-size:.75rem;color:var(--muted);background:var(--card);padding:.35rem .65rem;border-radius:999px;border:1px solid var(--border)}
main{display:grid;gap:1rem;padding:1rem;max-width:1100px;margin:0 auto}
@media(min-width:900px){main{grid-template-columns:1fr 280px}}
.viewer{background:#000;border:1px solid var(--border);border-radius:12px;overflow:hidden;min-height:240px;display:flex;align-items:center;justify-content:center}
.viewer img{width:100%;height:auto;display:block;background:#000}
.panel{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:1rem}
.panel h2{margin:0 0 .75rem;font-size:.95rem;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
label{display:block;font-size:.8rem;color:var(--muted);margin:.65rem 0 .3rem}
select,input[type=range]{width:100%}
select{background:var(--bg);color:var(--text);border:1px solid var(--border);border-radius:8px;padding:.55rem .65rem}
.row{display:flex;justify-content:space-between;align-items:center;font-size:.85rem;margin-top:.35rem}
.val{color:var(--acc);font-variant-numeric:tabular-nums}
.meta{font-size:.8rem;color:var(--muted);line-height:1.5;margin-top:1rem;padding-top:.75rem;border-top:1px solid var(--border)}
.meta strong{color:var(--text);font-weight:500}
button{margin-top:.75rem;width:100%;border:0;border-radius:8px;padding:.65rem;background:var(--acc);color:#fff;font-weight:600;cursor:pointer}
button:active{opacity:.85}
.status{font-size:.8rem;margin-top:.5rem;color:var(--ok);min-height:1.2em}
.status.err{color:var(--err)}
</style>
</head>
<body>
<header>
  <h1>ESP32-CAM · Stream</h1>
  <span class="badge" id="netBadge">carregando…</span>
</header>
<main>
  <section class="viewer">
    <img id="stream" alt="stream"/>
  </section>
  <aside class="panel">
    <h2>Controles</h2>
    <label for="framesize">Resolução</label>
    <select id="framesize">
      <option value="5">QVGA (320×240)</option>
      <option value="6">CIF (400×296)</option>
      <option value="7">HVGA (480×320)</option>
      <option value="8" selected>VGA (640×480)</option>
      <option value="9">SVGA (800×600)</option>
      <option value="10">XGA (1024×768)</option>
      <option value="11">HD (1280×720)</option>
      <option value="12">SXGA (1280×1024)</option>
      <option value="13">UXGA (1600×1200)</option>
    </select>

    <label for="quality">Qualidade JPEG</label>
    <input id="quality" type="range" min="10" max="63" value="12"/>
    <div class="row"><span>melhor ←</span><span class="val" id="qVal">12</span><span>→ menor</span></div>

    <button type="button" id="btnApply">Aplicar</button>
    <div class="status" id="status"></div>

    <div class="meta">
      <div><strong>IP:</strong> <span id="ip">—</span></div>
      <div><strong>Modo:</strong> <span id="mode">—</span></div>
      <div><strong>Stream:</strong> porta <span id="streamPort">81</span></div>
      <div><strong>mDNS:</strong> http://esp32cam.local/</div>
      <div style="margin-top:.5rem">Resoluções altas exigem PSRAM. Se travar, volte para VGA.</div>
    </div>
  </aside>
</main>
<script>
const STREAM_PORT = 81;
const $ = (id) => document.getElementById(id);
const statusEl = $('status');
const q = $('quality');
const qVal = $('qVal');

function streamUrl() {
  return 'http://' + location.hostname + ':' + STREAM_PORT + '/stream?t=' + Date.now();
}

function startStream() {
  $('stream').src = streamUrl();
}

q.addEventListener('input', () => { qVal.textContent = q.value; });

async function loadStatus() {
  try {
    const r = await fetch('/status', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    $('ip').textContent = j.ip || '—';
    $('mode').textContent = j.mode || '—';
    $('netBadge').textContent = (j.ip || 'offline') + ' · ' + (j.mode || '');
    if (j.framesize !== undefined && j.framesize !== null) {
      $('framesize').value = String(j.framesize);
    }
    if (j.quality !== undefined && j.quality !== null) {
      q.value = String(j.quality);
      qVal.textContent = String(j.quality);
    }
    statusEl.classList.remove('err');
  } catch (e) {
    statusEl.textContent = 'Falha ao ler status: ' + e.message;
    statusEl.classList.add('err');
  }
}

async function apply() {
  const fs = $('framesize').value;
  const qu = q.value;
  statusEl.classList.remove('err');
  statusEl.textContent = 'Aplicando…';
  try {
    const r1 = await fetch('/control?var=framesize&val=' + encodeURIComponent(fs), { cache: 'no-store' });
    const t1 = await r1.text();
    if (!r1.ok) throw new Error('framesize: ' + t1);

    const r2 = await fetch('/control?var=quality&val=' + encodeURIComponent(qu), { cache: 'no-store' });
    const t2 = await r2.text();
    if (!r2.ok) throw new Error('quality: ' + t2);

    statusEl.textContent = 'OK — framesize=' + fs + ' quality=' + qu;
    startStream();
    await loadStatus();
  } catch (e) {
    statusEl.textContent = 'Erro: ' + e.message;
    statusEl.classList.add('err');
  }
}

$('btnApply').addEventListener('click', apply);
$('framesize').addEventListener('change', apply);
q.addEventListener('change', apply);

startStream();
loadStatus();
setInterval(loadStatus, 5000);
</script>
</body>
</html>
)rawliteral";

static void setCors(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t handlerRoot(httpd_req_t* req) {
    setCors(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handlerStatus(httpd_req_t* req) {
    char ip[20];
    char mode[12];
    networkFormatIp(ip, sizeof(ip));
    networkFormatMode(mode, sizeof(mode));

    char json[256];
    snprintf(json, sizeof(json),
             "{\"ip\":\"%s\",\"mode\":\"%s\",\"framesize\":%d,\"quality\":%d,"
             "\"camera\":%s,\"heap\":%u,\"psram\":%u,\"stream_port\":%d}",
             ip,
             mode,
             cameraGetFramesize(),
             cameraGetQuality(),
             cameraIsReady() ? "true" : "false",
             (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getFreePsram(),
             STREAM_SERVER_PORT);

    setCors(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handlerControl(httpd_req_t* req) {
    char query[128];
    char var[32] = {0};
    char val[32] = {0};

    if (httpd_req_get_url_query_len(req) == 0 ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing var/val");
        return ESP_FAIL;
    }

    const int ival = atoi(val);
    bool ok = false;

    if (strcmp(var, "framesize") == 0) {
        ok = cameraSetFramesize(ival);
        Serial.printf("[HTTP] control framesize=%d -> %s\n", ival, ok ? "OK" : "FAIL");
    } else if (strcmp(var, "quality") == 0) {
        ok = cameraSetQuality(ival);
        Serial.printf("[HTTP] control quality=%d -> %s\n", ival, ok ? "OK" : "FAIL");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown var");
        return ESP_FAIL;
    }

    setCors(req);
    httpd_resp_set_type(req, "text/plain");
    if (ok) {
        return httpd_resp_send(req, "OK", 2);
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FAIL");
    return ESP_FAIL;
}

static esp_err_t handlerStream(httpd_req_t* req) {
    if (!cameraIsReady()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    char partHdr[64];

    while (true) {
        camera_fb_t* fb = cameraCapture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY_LINE, strlen(STREAM_BOUNDARY_LINE));
        if (res == ESP_OK) {
            const int hlen = snprintf(partHdr, sizeof(partHdr), STREAM_PART_FMT, (unsigned)fb->len);
            res = httpd_resp_send_chunk(req, partHdr, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        }

        cameraRelease(fb);

        if (res != ESP_OK) {
            break;
        }
    }

    // Cliente desconectou ou erro de envio
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

bool webserverStart(void) {
    if (s_started) {
        return true;
    }

    if (!networkIsConnected()) {
        Serial.println(F("[HTTP] sem WiFi — servidor nao iniciado"));
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.server_port = WEB_SERVER_PORT;
    config.ctrl_port = 32768;

    httpd_uri_t uriRoot = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handlerRoot,
        .user_ctx = nullptr
    };
    httpd_uri_t uriStatus = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = handlerStatus,
        .user_ctx = nullptr
    };
    httpd_uri_t uriControl = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = handlerControl,
        .user_ctx = nullptr
    };
    // Catch-all opcional não é necessário se registrarmos as rotas

    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        Serial.println(F("[HTTP] falha ao iniciar servidor API (80)"));
        s_httpd = nullptr;
        return false;
    }

    httpd_register_uri_handler(s_httpd, &uriRoot);
    httpd_register_uri_handler(s_httpd, &uriStatus);
    httpd_register_uri_handler(s_httpd, &uriControl);

    // Servidor dedicado ao stream (porta 81)
    config.server_port = STREAM_SERVER_PORT;
    config.ctrl_port = 32769;

    httpd_uri_t uriStream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = handlerStream,
        .user_ctx = nullptr
    };

    if (httpd_start(&s_stream_httpd, &config) != ESP_OK) {
        Serial.println(F("[HTTP] falha ao iniciar servidor stream (81)"));
        httpd_stop(s_httpd);
        s_httpd = nullptr;
        s_stream_httpd = nullptr;
        return false;
    }

    httpd_register_uri_handler(s_stream_httpd, &uriStream);

    s_started = true;

    char ip[20];
    networkFormatIp(ip, sizeof(ip));
    Serial.printf("[HTTP] UI/API:  http://%s/\n", ip);
    Serial.printf("[HTTP] status:  http://%s/status\n", ip);
    Serial.printf("[HTTP] stream:  http://%s:%d/stream\n", ip, STREAM_SERVER_PORT);
    bootLogHeap("HTTP");
    return true;
}

void webserverUpdate(void) {
    // esp_http_server roda em tasks próprias — não precisa handleClient().
    if (!s_started && networkIsConnected()) {
        webserverStart();
    }
}
