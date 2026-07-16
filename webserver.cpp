#include "webserver.h"
#include "config.h"
#include "camera.h"
#include "network.h"
#include "mqtt.h"
#include "led.h"
#include "boot.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"

// Servidor principal na porta 80: UI, API e /stream.
// (esp_http_server usa worker por conexão — stream não bloqueia /status.)
// Porta 81 opcional: espelho de /stream (compatibilidade); se falhar, 80 basta.
static httpd_handle_t s_httpd = nullptr;
static httpd_handle_t s_stream_httpd = nullptr;
static bool s_started = false;
static bool s_stream81_ok = false;

static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY;
static const char STREAM_BOUNDARY_LINE[] = "\r\n--" STREAM_BOUNDARY "\r\n";
static const char STREAM_PART_FMT[] =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Página principal — stream na porta 81; status/control/mqtt na 80
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
@media(min-width:900px){main{grid-template-columns:1fr 300px}}
.viewer{background:#000;border:1px solid var(--border);border-radius:12px;overflow:hidden;min-height:240px;display:flex;align-items:center;justify-content:center}
.viewer img{width:100%;height:auto;display:block;background:#000}
.side{display:flex;flex-direction:column;gap:1rem}
.panel{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:1rem}
.panel h2{margin:0 0 .75rem;font-size:.95rem;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
label{display:block;font-size:.8rem;color:var(--muted);margin:.65rem 0 .3rem}
select,input[type=range],input[type=text],input[type=password],input[type=number]{width:100%}
select,input[type=text],input[type=password],input[type=number]{background:var(--bg);color:var(--text);border:1px solid var(--border);border-radius:8px;padding:.55rem .65rem;font:inherit}
input[type=number]{appearance:textfield;-moz-appearance:textfield}
.hint{font-size:.75rem;color:var(--muted);margin-top:.35rem;line-height:1.4}
.row{display:flex;justify-content:space-between;align-items:center;font-size:.85rem;margin-top:.35rem}
.val{color:var(--acc);font-variant-numeric:tabular-nums}
.meta{font-size:.8rem;color:var(--muted);line-height:1.5;margin-top:1rem;padding-top:.75rem;border-top:1px solid var(--border)}
.meta strong{color:var(--text);font-weight:500}
button{margin-top:.75rem;width:100%;border:0;border-radius:8px;padding:.65rem;background:var(--acc);color:#fff;font-weight:600;cursor:pointer;font:inherit}
button:active{opacity:.85}
button.mqtt{background:#5b6cff}
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
  <div class="side">
    <aside class="panel">
      <h2>Imagem</h2>
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

      <button type="button" id="btnApply">Aplicar imagem</button>
      <div class="status" id="status"></div>

      <div class="meta">
        <div><strong>IP:</strong> <span id="ip">—</span></div>
        <div><strong>Modo:</strong> <span id="mode">—</span></div>
        <div><strong>Stream:</strong> <span id="streamUrl">/stream</span></div>
        <div><strong>MQTT:</strong> <span id="mqtt">—</span></div>
        <div><strong>LED:</strong> <span id="led">—</span></div>
        <div><strong>mDNS:</strong> http://esp32cam.local/</div>
        <div style="margin-top:.5rem">Resoluções altas exigem PSRAM. Se travar, volte para VGA.</div>
      </div>
    </aside>

    <aside class="panel">
      <h2>MQTT</h2>
      <label for="mqttHost">Servidor (vazio = desliga)</label>
      <input id="mqttHost" type="text" maxlength="63" placeholder="192.168.1.10 ou broker.local" autocomplete="off"/>

      <label for="mqttPort">Porta</label>
      <input id="mqttPort" type="number" min="1" max="65535" value="1883"/>

      <label for="mqttUser">Usuário (opcional)</label>
      <input id="mqttUser" type="text" maxlength="47" autocomplete="username"/>

      <label for="mqttPass">Senha (opcional)</label>
      <input id="mqttPass" type="password" maxlength="47" autocomplete="current-password"/>

      <label for="mqttTopicIn">Tópico entrada (comandos)</label>
      <input id="mqttTopicIn" type="text" maxlength="95" placeholder="esp32cam/cmd"/>

      <label for="mqttTopicOut">Tópico saída (imagem JPEG)</label>
      <input id="mqttTopicOut" type="text" maxlength="95" placeholder="esp32cam/image"/>

      <p class="hint">Comandos: <code>capture</code>, <code>led_toggle</code>, <code>led_on</code>, <code>led_off</code></p>

      <button type="button" class="mqtt" id="btnMqttSave">Salvar MQTT</button>
      <div class="status" id="mqttStatus"></div>
    </aside>
  </div>
</main>
<script>
const $ = (id) => document.getElementById(id);
const statusEl = $('status');
const mqttStatusEl = $('mqttStatus');
const q = $('quality');
const qVal = $('qVal');

// Mesma origem (porta 80) — a porta 81 costuma falhar em algumas redes/roteadores
function streamUrl() {
  return '/stream?t=' + Date.now();
}

function startStream() {
  const img = $('stream');
  img.onerror = () => {
    statusEl.textContent = 'Stream falhou — tentando de novo…';
    statusEl.classList.add('err');
    setTimeout(startStream, 2000);
  };
  img.onload = () => {
    // MJPEG em <img> pode não disparar load continuamente; limpa erro se havia
    if (statusEl.classList.contains('err') && statusEl.textContent.indexOf('Stream') === 0) {
      statusEl.textContent = '';
      statusEl.classList.remove('err');
    }
  };
  img.src = streamUrl();
}

q.addEventListener('input', () => { qVal.textContent = q.value; });

function fillMqttForm(j) {
  if (!j) return;
  $('mqttHost').value = j.host || '';
  $('mqttPort').value = (j.port !== undefined && j.port !== null) ? j.port : 1883;
  $('mqttUser').value = j.user || '';
  $('mqttPass').value = j.pass || '';
  $('mqttTopicIn').value = j.topic_in || '';
  $('mqttTopicOut').value = j.topic_out || '';
}

async function loadMqttConfig() {
  try {
    const r = await fetch('/mqtt', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    fillMqttForm(await r.json());
  } catch (e) {
    mqttStatusEl.textContent = 'Falha ao ler MQTT: ' + e.message;
    mqttStatusEl.classList.add('err');
  }
}

async function loadStatus() {
  try {
    const r = await fetch('/status', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    $('ip').textContent = j.ip || '—';
    $('mode').textContent = j.mode || '—';
    $('netBadge').textContent = (j.ip || 'offline') + ' · ' + (j.mode || '');
    if (j.mqtt_enabled) {
      $('mqtt').textContent = (j.mqtt_connected ? 'conectado' : 'offline') +
        ' · ' + (j.mqtt_host || '—');
    } else {
      $('mqtt').textContent = 'desligado';
    }
    $('led').textContent = j.led ? 'ON' : 'OFF';
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

async function saveMqtt() {
  mqttStatusEl.classList.remove('err');
  mqttStatusEl.textContent = 'Salvando…';
  const body = new URLSearchParams({
    host: $('mqttHost').value.trim(),
    port: String($('mqttPort').value || '1883'),
    user: $('mqttUser').value,
    pass: $('mqttPass').value,
    topic_in: $('mqttTopicIn').value.trim(),
    topic_out: $('mqttTopicOut').value.trim()
  });
  try {
    const r = await fetch('/mqtt', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString(),
      cache: 'no-store'
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    mqttStatusEl.textContent = t || 'OK';
    await loadMqttConfig();
    await loadStatus();
  } catch (e) {
    mqttStatusEl.textContent = 'Erro: ' + e.message;
    mqttStatusEl.classList.add('err');
  }
}

$('btnApply').addEventListener('click', apply);
$('framesize').addEventListener('change', apply);
q.addEventListener('change', apply);
$('btnMqttSave').addEventListener('click', saveMqtt);

startStream();
loadStatus();
loadMqttConfig();
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
    char mqttHost[80];
    char mqttIn[96];
    char mqttOut[96];
    networkFormatIp(ip, sizeof(ip));
    networkFormatMode(mode, sizeof(mode));
    mqttFormatHost(mqttHost, sizeof(mqttHost));
    mqttFormatTopicIn(mqttIn, sizeof(mqttIn));
    mqttFormatTopicOut(mqttOut, sizeof(mqttOut));

    char json[512];
    snprintf(json, sizeof(json),
             "{\"ip\":\"%s\",\"mode\":\"%s\",\"framesize\":%d,\"quality\":%d,"
             "\"camera\":%s,\"heap\":%u,\"psram\":%u,\"stream_port\":%d,"
             "\"stream_port_alt\":%d,"
             "\"mqtt_enabled\":%s,\"mqtt_connected\":%s,\"mqtt_host\":\"%s\","
             "\"mqtt_topic_in\":\"%s\",\"mqtt_topic_out\":\"%s\",\"led\":%s}",
             ip,
             mode,
             cameraGetFramesize(),
             cameraGetQuality(),
             cameraIsReady() ? "true" : "false",
             (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getFreePsram(),
             WEB_SERVER_PORT,
             s_stream81_ok ? STREAM_SERVER_PORT : 0,
             mqttIsEnabled() ? "true" : "false",
             mqttIsConnected() ? "true" : "false",
             mqttHost,
             mqttIn,
             mqttOut,
             ledIsOn() ? "true" : "false");

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

static esp_err_t handlerMqttGet(httpd_req_t* req) {
    char json[640];
    if (!mqttGetConfigJson(json, sizeof(json))) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config");
        return ESP_FAIL;
    }
    setCors(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// Decodifica %XX e + → espaço (in-place / para dst).
static void urlDecode(const char* src, char* dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dstLen; ++i) {
        if (src[i] == '+') {
            dst[j++] = ' ';
        } else if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            char hex[3] = {src[i + 1], src[i + 2], 0};
            dst[j++] = (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

// Extrai valor de form-urlencoded (key=value&...).
static bool formGet(const char* body, const char* key, char* out, size_t outLen) {
    if (!body || !key || !out || outLen == 0) return false;
    out[0] = '\0';

    const size_t keyLen = strlen(key);
    const char* p = body;
    while (p && *p) {
        const char* amp = strchr(p, '&');
        size_t pairLen = amp ? (size_t)(amp - p) : strlen(p);

        const char* eq = (const char*)memchr(p, '=', pairLen);
        if (eq) {
            const size_t klen = (size_t)(eq - p);
            if (klen == keyLen && strncmp(p, key, keyLen) == 0) {
                const size_t vlen = pairLen - klen - 1;
                char raw[160];
                if (vlen >= sizeof(raw)) return false;
                memcpy(raw, eq + 1, vlen);
                raw[vlen] = '\0';
                urlDecode(raw, out, outLen);
                return true;
            }
        }

        p = amp ? amp + 1 : nullptr;
    }
    return false;
}

static esp_err_t handlerMqttPost(httpd_req_t* req) {
    char body[512];
    int total = 0;
    int remaining = req->content_len;

    if (remaining < 0 || remaining >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        const int got = httpd_req_recv(req, body + total, remaining);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        total += got;
        remaining -= got;
    }
    body[total] = '\0';

    char host[64] = {0};
    char portStr[8] = {0};
    char user[48] = {0};
    char pass[48] = {0};
    char tin[96] = {0};
    char tout[96] = {0};

    formGet(body, "host", host, sizeof(host));
    formGet(body, "port", portStr, sizeof(portStr));
    formGet(body, "user", user, sizeof(user));
    formGet(body, "pass", pass, sizeof(pass));
    formGet(body, "topic_in", tin, sizeof(tin));
    formGet(body, "topic_out", tout, sizeof(tout));

    int port = atoi(portStr);
    if (port <= 0 || port > 65535) port = MQTT_DEFAULT_PORT;

    Serial.printf("[HTTP] MQTT save host=%s port=%d\n",
                  host[0] ? host : "(off)", port);

    if (!mqttApplyConfig(host, (uint16_t)port, user, pass, tin, tout)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }

    char resp[96];
    if (!mqttIsEnabled()) {
        snprintf(resp, sizeof(resp), "OK — MQTT desligado");
    } else if (mqttIsConnected()) {
        snprintf(resp, sizeof(resp), "OK — conectado");
    } else {
        snprintf(resp, sizeof(resp), "OK — salvo (conectando…)");
    }

    setCors(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handlerStream(httpd_req_t* req) {
    if (!cameraIsReady()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
        return ESP_FAIL;
    }

    Serial.println(F("[HTTP] stream client connected"));

    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");

    char partHdr[64];
    // Primeiro boundary sem \r\n inicial (melhor compatibilidade com <img>)
    static const char FIRST_BOUNDARY[] = "--" STREAM_BOUNDARY "\r\n";
    bool first = true;

    while (true) {
        camera_fb_t* fb = cameraCapture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (first) {
            res = httpd_resp_send_chunk(req, FIRST_BOUNDARY, strlen(FIRST_BOUNDARY));
            first = false;
        } else {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY_LINE, strlen(STREAM_BOUNDARY_LINE));
        }

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

        // Pequena folga para WiFi/TCP e outras requisições HTTP
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Serial.println(F("[HTTP] stream client disconnected"));
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
    config.max_uri_handlers = 12;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
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
    httpd_uri_t uriMqttGet = {
        .uri = "/mqtt",
        .method = HTTP_GET,
        .handler = handlerMqttGet,
        .user_ctx = nullptr
    };
    httpd_uri_t uriMqttPost = {
        .uri = "/mqtt",
        .method = HTTP_POST,
        .handler = handlerMqttPost,
        .user_ctx = nullptr
    };
    httpd_uri_t uriStream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = handlerStream,
        .user_ctx = nullptr
    };

    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        Serial.println(F("[HTTP] falha ao iniciar servidor (80)"));
        s_httpd = nullptr;
        return false;
    }

    httpd_register_uri_handler(s_httpd, &uriRoot);
    httpd_register_uri_handler(s_httpd, &uriStatus);
    httpd_register_uri_handler(s_httpd, &uriControl);
    httpd_register_uri_handler(s_httpd, &uriMqttGet);
    httpd_register_uri_handler(s_httpd, &uriMqttPost);
    // Stream na MESMA porta 80 (mesma origem da página)
    httpd_register_uri_handler(s_httpd, &uriStream);

    // Porta 81 opcional (espelho). Não derruba a UI se falhar.
    s_stream81_ok = false;
    s_stream_httpd = nullptr;
    {
        httpd_config_t cfg81 = HTTPD_DEFAULT_CONFIG();
        cfg81.max_uri_handlers = 4;
        cfg81.max_open_sockets = 3;
        cfg81.lru_purge_enable = true;
        cfg81.stack_size = 8192;
        cfg81.server_port = STREAM_SERVER_PORT;
        cfg81.ctrl_port = 32769;

        if (httpd_start(&s_stream_httpd, &cfg81) == ESP_OK) {
            httpd_register_uri_handler(s_stream_httpd, &uriStream);
            s_stream81_ok = true;
            Serial.printf("[HTTP] stream alt: porta %d OK\n", STREAM_SERVER_PORT);
        } else {
            s_stream_httpd = nullptr;
            Serial.printf("[HTTP] stream alt: porta %d falhou (usando /stream na 80)\n",
                          STREAM_SERVER_PORT);
        }
    }

    s_started = true;

    char ip[20];
    networkFormatIp(ip, sizeof(ip));
    Serial.printf("[HTTP] UI/API:  http://%s/\n", ip);
    Serial.printf("[HTTP] status:  http://%s/status\n", ip);
    Serial.printf("[HTTP] mqtt:    http://%s/mqtt\n", ip);
    Serial.printf("[HTTP] stream:  http://%s/stream\n", ip);
    if (s_stream81_ok) {
        Serial.printf("[HTTP] stream:  http://%s:%d/stream\n", ip, STREAM_SERVER_PORT);
    }
    bootLogHeap("HTTP");
    return true;
}

void webserverUpdate(void) {
    if (!s_started && networkIsConnected()) {
        webserverStart();
    }
}
