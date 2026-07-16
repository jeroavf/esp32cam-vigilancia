# ESP32-CAM Stream — Documentação do projeto

**[English version](#english)**

Firmware MVP para **ESP32-CAM AI-Thinker** com **shield CAM-MB**: stream de vídeo ao vivo (MJPEG) no navegador, na **rede local**, com Wi‑Fi via portal, **DHCP ou IP fixo**, e resolução/qualidade configuráveis na página.

Este README reúne o **planejamento**, as **decisões de projeto**, a **arquitetura**, o **uso**, e os **acertos/aprendizados** obtidos durante implementação e testes.

---

## 1. Objetivo

Disponibilizar uma câmera IP simples e autônoma:

- Ver o stream em **qualquer navegador** (sem app nativo).
- Configurar Wi‑Fi **sem recompilar** (portal captivo).
- Escolher **DHCP ou IP fixo** dentro da LAN.
- Ajustar **resolução e qualidade JPEG** pela interface web.
- Lidar com as limitações conhecidas do conjunto **ESP32-CAM + CAM-MB** (alimentação, boot, serial).

### Fora do escopo do MVP

| Item | Motivo |
|------|--------|
| Acesso pela internet | Complexidade (NAT, túnel, segurança) |
| Autenticação (usuário/senha) | Rede local considerada suficiente no MVP |
| Snapshot / gravação / microSD | Não pedidos no MVP |
| LED flash, detecção de movimento, IA | Pós-MVP |
| App nativo / Home Assistant | Integração futura opcional |
| Vários clientes de stream simultâneos | Limitação do `WebServer` síncrono |

---

## 2. Planejamento e decisões (definição com o usuário)

Decisões tomadas **antes** da implementação, em rodadas de perguntas:

| Tema | Decisão |
|------|----------|
| Objetivo principal | **Streaming ao vivo** |
| Cliente | **Navegador** (web) |
| Rede | **Somente local** (Wi‑Fi de casa) |
| Hardware | **ESP32-CAM-MB** (AI-Thinker + shield USB) |
| Qualidade de imagem | **Configurável na página web** |
| Autenticação | **Nenhuma** (rede local basta) |
| Extras no MVP | **Só o stream** (mínimo viável) |
| Wi‑Fi | **Portal WiFiManager** |
| IP | **DHCP ou IP fixo** (configurável no portal, persistido em NVS) |
| Entrega inicial | Plano primeiro; depois implementação do MVP |

### Requisitos técnicos adicionados no plano

1. **IP configurável**
   - DHCP (padrão) ou estático (IP, gateway, máscara, DNS).
   - Persistência em **Preferences/NVS** (sobrevive a reboot).
   - Validação básica; IP inválido → fallback DHCP + log no Serial.
   - Recomendação: IP fixo **fora do pool DHCP** do roteador.

2. **Problemas de inicialização com CAM-MB**
   - Brownout / picos de corrente com USB.
   - Mitigações no firmware e documentação de hardware.
   - Sequência de boot pensada para portal e estabilidade.

---

## 3. Arquitetura

```
[Celular/PC mesma LAN]
        │
        │  http://IP/  ou  http://esp32cam.local/
        ▼
┌───────────────────────────────────────┐
│              ESP32-CAM                │
│                                       │
│  boot     → brownout off, logs        │
│  network  → WiFiManager, DHCP/static  │
│  camera   → OV2640, retry, framesize  │
│  webserver→ HTML + MJPEG + /control   │
└───────────────────────────────────────┘
```

### Ordem de boot (versão atual)

```
1. Desliga brownout detector
2. Settle + Serial (115200)
3. Wi-Fi / portal WiFiManager   ← AP sobe cedo se não houver rede
4. Câmera OV2640 (retry)
5. Servidor HTTP (página + stream)
```

> **Histórico:** a primeira versão iniciava a **câmera antes do Wi‑Fi** (menos pico simultâneo). Nos testes, isso **atrasava ou impedia** o AP de configuração se a câmera falhasse/reiniciasse.  
> **Acerto:** portal **antes** da câmera — prioriza configuração e acesso à rede.

### Stack

| Camada | Tecnologia |
|--------|------------|
| Framework | Arduino + ESP32 core |
| Board FQBN | `esp32:esp32:esp32cam` |
| Wi‑Fi config | WiFiManager 2.x |
| Persistência IP | Preferences (NVS), namespace `netcfg` |
| HTTP | `WebServer` (porta 80) |
| Stream | MJPEG (`multipart/x-mixed-replace`) |
| Descoberta | mDNS `esp32cam.local` (quando o SO resolver) |

### Estrutura de arquivos

```
esp32cam/
├── esp32cam.ino       # setup()/loop() — só orquestra
├── config.h           # pinos, SSID do portal, defaults
├── boot.h / boot.cpp  # brownout, settle, motivo de reset
├── camera.h / camera.cpp
├── network.h / network.cpp
├── webserver.h / webserver.cpp
└── README.md          # este documento
```

**Regra de modularização:** o `.ino` não implementa lógica de Wi‑Fi/câmera/HTTP; cada módulo expõe API em `.h` e esconde estado `static` no `.cpp`.

---

## 4. Hardware

### Material

- Módulo **ESP32-CAM** (AI-Thinker), sensor **OV2640**.
- **Shield CAM-MB** (USB-serial + botões IO0/RST) para programação e alimentação via USB.

### Pinout da câmera (AI-Thinker)

Definido em `config.h` — PWDN 32, XCLK 0, SIOD 26, SIOC 27, Y2–Y9, VSYNC 25, HREF 23, PCLK 22, etc.

### Placa no Arduino

- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled** quando a opção existir (melhora buffers de frame)

### Cuidados de alimentação (críticos)

A ESP32-CAM puxa **picos altos** ao ligar Wi‑Fi e câmera. Com CAM-MB + porta USB fraca:

| Sintoma | Causa típica |
|---------|----------------|
| Reboot em loop | Brownout / queda de tensão |
| `camera init failed` | Energia ou flat da câmera |
| Funciona num PC e noutro não | Corrente da porta USB |
| Trava ao abrir serial | Reset por DTR + boot pesado de novo |

**Recomendações:**

1. Cabo USB **curto** e de qualidade (dados + alimentação).  
2. Preferir porta com boa corrente ou **hub USB alimentado**.  
3. Se instável: fonte **5 V ≥ 1 A** em **5V/GND** da placa.  
4. Opcional: capacitor **100–470 µF** entre 5V e GND.  
5. Wi‑Fi da casa em **2.4 GHz** (ESP32 não usa 5 GHz).

---

## 5. Funcionalidades do MVP

### 5.1 Stream e página web

| URL | Função |
|-----|--------|
| `http://IP/` | UI: preview + resolução + qualidade (porta **80**) |
| `http://IP:81/stream` | Stream MJPEG contínuo (porta **81**, servidor separado) |
| `http://IP/status` | JSON (IP, modo DHCP/STATIC, framesize, quality, heap, PSRAM) |
| `http://IP/control?var=framesize&val=N` | Altera resolução (`framesize_t`) |
| `http://IP/control?var=quality&val=N` | JPEG quality **10–63** (menor = melhor) |

> **Por que duas portas?** Com um único `WebServer` síncrono, o `/stream` bloqueava a API: a página **não atualizava IP/modo** e o botão **Aplicar** não alterava framesize/quality. Solução (padrão Espressif): UI/API na **80**, stream na **81** via `esp_http_server`.

Defaults no boot: **VGA (8)**, quality **12**.

### 5.2 Portal Wi‑Fi

| Item | Valor |
|------|--------|
| SSID do AP de setup | **`ESP32CAM-Setup`** |
| Senha do AP | **Nenhuma** (rede aberta) |
| URL do portal | `http://192.168.4.1` |
| Hostname / mDNS | `esp32cam` → `http://esp32cam.local/` |

Comportamento:

- **Sem credenciais salvas:** abre o AP **na hora**, **sem timeout** (fica até configurar).  
- **Com credenciais:** tenta a rede salva; se falhar, reabre o portal.  
- Campos extras no portal: IP fixo (0/1), IP, gateway, máscara, DNS.

### 5.3 DHCP vs IP fixo

1. Usuário escolhe no portal.  
2. Valores vão para NVS (`netcfg`).  
3. No boot: se estático → `WiFi.config(...)` **antes** de conectar.  
4. Após portal com IP fixo: desconecta, reaplica config e reconecta (evita ficar só com DHCP da primeira associação).  
5. Página e Serial mostram IP e modo (`DHCP` / `STATIC`).

---

## 6. Compilar, gravar e monitorar

### Dependências

```bash
arduino-cli lib install WiFiManager
```

Core ESP32 instalado; FQBN: `esp32:esp32:esp32cam`.

### Comandos

```bash
cd /caminho/para/esp32cam

# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32cam .

# Porta
arduino-cli board list

# Upload (ajuste a porta)
arduino-cli upload --fqbn esp32:esp32:esp32cam --port /dev/ttyUSB0 .

# Monitor SEM forçar reset contínuo (muito importante no CAM-MB)
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200,dtr=off,rts=off
```

### Upload manual se auto-reset falhar

1. Segure **IO0** (program).  
2. Toque **RST**.  
3. Solte IO0 e envie o sketch.

---

## 7. Como usar no dia a dia

### Primeira configuração

1. Grave o firmware e alimente a placa.  
2. No celular/PC (**Wi‑Fi 2.4 GHz**), conecte em **`ESP32CAM-Setup`** (aberta).  
3. Abra `http://192.168.4.1`.  
4. Selecione a rede da casa + senha.  
5. IP: `0` = DHCP · `1` = fixo (preencha IP/GW/máscara/DNS).  
6. Salve e volte à Wi‑Fi da casa.  
7. Acesse `http://IP/` (IP no Serial ou via nmap/roteador).

### Acessar o stream

- Navegador na **mesma LAN**: `http://IP/`  
- Atalho mDNS (se funcionar no SO): `http://esp32cam.local/`  
- **Não use HTTPS** (só HTTP).  
- Serial **não é necessária** para uso normal após o Wi‑Fi configurado.

### Achar o IP com nmap

Troque a rede pelo prefixo da sua LAN (`ip -4 route`):

```bash
# Hosts com porta 80 aberta
nmap -p 80 --open 192.168.1.0/24

# Só listar IPs candidatos
nmap -p 80 --open -oG - 192.168.1.0/24 | awk '/80\/open/{print $2}'
```

Para cada IP candidato: abrir `http://IP/` ou `curl -sI http://IP/`.

### Reconfigurar Wi‑Fi

Apagar credenciais (erase flash + reupload) ou sketch mínimo:

```cpp
#include <WiFi.h>
void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(1000);
  ESP.restart();
}
void loop() {}
```

No boot seguinte, o AP **`ESP32CAM-Setup`** volta a aparecer.

---

## 8. Acertos e aprendizados nos testes

Registro do que foi observado e **corrigido** durante o uso real com CAM-MB.

### 8.1 AP `ESP32CAM-Setup` não aparecia

| Hipótese | O que vimos | Acerto no firmware |
|----------|-------------|--------------------|
| Portal só depois da câmera | Init da OV2640 demora/falha/reinicia; usuário nunca vê o AP | **Wi‑Fi/portal primeiro**, câmera depois |
| `autoConnect` com credenciais antigas | Tenta STA e atrasa o AP | Sem rede salva → **`startConfigPortal` imediato**, timeout 0 |
| Celular em 5 GHz | ESP32 só 2.4 GHz | Documentar; canal AP fixo **1** |
| AP “invisível” | Potência/canal | `WiFi.setTxPower` alto, canal 1, rede aberta, logs claros no Serial |

**Mensagem esperada no Serial quando o portal está ativo:**

```text
[NET ] sem rede salva — abrindo AP "ESP32CAM-Setup" agora
========================================
 PORTAL WiFi ATIVO
 SSID:  ESP32CAM-Setup
 Senha: (nenhuma — rede aberta)
 URL:   http://192.168.4.1
========================================
```

### 8.2 Abrir a porta serial “trava” ou parece reset

| Fato | Detalhe |
|------|---------|
| Causa | Shield USB (CH340 etc.) pulsa **DTR/RTS** → pino **RST** da ESP32 |
| Efeito | **Um reboot** ao abrir o monitor é **normal** |
| Loop de reset | Reboot + pico Wi‑Fi/câmera + USB fraco → brownout ou crash em loop |
| Default ruim | `arduino-cli monitor` usa **`dtr=on`, `rts=on`** |

**Acerto na prática:**

```bash
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200,dtr=off,rts=off
```

**Acerto no firmware:**

- Brownout desligado cedo.  
- `BOOT_SETTLE_MS` maior (500 ms) após reset.  
- Log do **motivo de reset** (`POWERON`, `EXT` = DTR/RST, `BROWNOUT`, `PANIC`, etc.).  
- Dicas no Serial quando o motivo for `EXT` ou `BROWNOUT`.

**Boa ordem de debug:** deixar a placa bootar ~10–15 s → só então abrir o monitor com DTR/RTS off.  
Para só usar o stream, **não precisa** conectar serial.

### 8.3 Brownout e alimentação

| Mitigação de software | Mitigação de hardware |
|----------------------|------------------------|
| `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` | Cabo/porta/fonte melhores |
| Defaults VGA (não UXGA no boot) | 5 V externo ≥ 1 A se preciso |
| Retry na câmera | Capacitor em 5V/GND se persistir |
| Separar no tempo Wi‑Fi e câmera | Evitar cabo longo / hub sem alimentação |

Desligar o brownout **mascara** a queda de tensão para não ficar em loop de detecção; a corrente ainda precisa ser suficiente para Wi‑Fi + JPEG estáveis.

### 8.4 IP e acesso após configurar o Wi‑Fi

Fluxo validado nos testes:

1. Portal configura SSID/senha (e opcionalmente IP).  
2. Placa associa na LAN.  
3. Serial (se estável) mostra `[NET ] WiFi OK IP=...` e `[HTTP] http://...`.  
4. Sem serial: **nmap** na porta 80 ou lista de clientes do roteador.  
5. Navegador: `http://IP/` — mesma rede, HTTP, 2.4 GHz.

### 8.5 Limitações confirmadas no MVP

- **Um cliente** de stream por vez: o handler de `/stream` bloqueia o `WebServer` enquanto a conexão MJPEG está aberta.  
- Resoluções muito altas (SXGA/UXGA) exigem **PSRAM** e bom Wi‑Fi; se travar, voltar para **VGA/SVGA**.  
- mDNS (`esp32cam.local`) depende do SO/rede (funciona melhor em Linux/macOS; Windows é inconsistente).

---

## 9. Logs úteis no Serial (boot saudável)

Exemplo aproximado de sequência OK:

```text
========================================
 ESP32-CAM Stream MVP
========================================
[BOOT] brownout detector disabled
[BOOT] reset reason: 1 (POWERON)   # ou EXT se abriu serial
[BOOT] heap free=... psram=...
[NET ] iniciando WiFi/portal (antes da camera)
[NET ] modo DHCP (padrao)
[NET ] credenciais WiFi salvas: sim
[NET ] WiFi OK  IP=192.168.x.x  RSSI=...  mode=DHCP
[HTTP] http://192.168.x.x/
[CAM ] PSRAM found — fb_count=2
[CAM ] init OK
[MAIN] setup concluido
```

| Sintoma no log | Ação |
|----------------|------|
| `reset reason: BROWNOUT` | Melhorar alimentação |
| `reset reason: EXT` | Normal ao abrir serial; use dtr/rts off |
| `reset reason: PANIC` | Crash de software — copiar stack se houver |
| `esp_camera_init failed` | Flat OV2640, board, PSRAM, energia |
| Portal sem sumir e sem rede | Ainda no setup; configure ou apague credenciais |
| WiFi OK mas página vazia | Testar `/stream`; baixar framesize |

---

## 10. Troubleshooting resumido

| Problema | O que fazer |
|----------|-------------|
| Não vejo `ESP32CAM-Setup` | Firmware atual? 2.4 GHz? Serial mostra portal? Sem credenciais antigas? Energia OK? |
| Configurei Wi‑Fi e não acho a placa | nmap porta 80; roteador; Serial se possível |
| Abrir Serial reinicia | Esperado no CAM-MB; `dtr=off,rts=off` + `stty -hupcl` |
| Loop de reset | Fonte 5 V, cabo, hub alimentado; ver motivo no Serial |
| Stream não carrega | Mesma rede; HTTP; um cliente; VGA; `/status` |
| Quero mudar de rede | Apagar Wi‑Fi NVS / erase flash e reconfigurar portal |
| Upload falha | IO0 + RST; driver CH340; cabo dados |

---

## 11. Evolução futura (pós-MVP)

Ideias alinhadas ao que ficou de fora, por prioridade sugerida:

1. Botão/hold para **forçar portal** (reset de Wi‑Fi sem erase flash).  
2. Snapshot (`/capture`) e opcional LED flash.  
3. Autenticação HTTP básica se a LAN não for confiável.  
4. `esp_http_server` assíncrono (vários viewers).  
5. Integração Home Assistant / MQTT.  
6. Gravação em microSD ou envio periódico de fotos.  
7. Acesso remoto (túnel controlado + auth).

---

## 12. Constantes principais (`config.h`)

| Símbolo | Valor típico | Significado |
|---------|--------------|-------------|
| `WIFI_PORTAL_NAME` | `ESP32CAM-Setup` | SSID do AP de configuração |
| `WIFI_HOSTNAME` / `WIFI_MDNS_NAME` | `esp32cam` | Hostname e mDNS |
| `CAM_DEFAULT_FRAMESIZE` | `8` (VGA) | Resolução no boot |
| `CAM_DEFAULT_QUALITY` | `12` | JPEG (10–63) |
| `CAM_INIT_RETRIES` | `5` | Tentativas de init da câmera |
| `BOOT_SETTLE_MS` | `500` | Pausa pós-reset / serial |
| `WEB_SERVER_PORT` | `80` | HTTP |
| `NET_PREFS_NS` | `netcfg` | Namespace NVS do IP |

---

## 13. Linha do tempo do projeto (resumo)

| Etapa | Resultado |
|-------|-----------|
| Definição de objetivo | Stream local no navegador, MVP mínimo |
| Requisitos de IP e CAM-MB | DHCP/fixo + plano anti-brownout |
| Implementação modular | boot, camera, network, webserver |
| Compilação | OK com `esp32:esp32:esp32cam` |
| Teste: AP não listado | Portal antes da câmera; `startConfigPortal` sem timeout |
| Teste: Wi‑Fi configurado | Acesso via IP na LAN |
| Teste: serial “trava” | Documentado DTR/RST; monitor com dtr/rts off; logs de reset |
| Documentação | Este README unificado |

---

## 14. Licença e uso

Projeto de uso pessoal/estudo. Use apenas em redes em que você tem autorização. Sem autenticação no MVP: **não exponha a porta 80 na internet** sem camadas extras de segurança.

---

*Documento gerado a partir do planejamento, implementação e testes do MVP ESP32-CAM Stream.*

---
---

<a id="english"></a>

# ESP32-CAM Stream — Project Documentation

MVP firmware for **ESP32-CAM AI-Thinker** with **CAM-MB shield**: live video stream (MJPEG) in the browser, on the **local network**, with Wi‑Fi via portal, **DHCP or static IP**, and resolution/quality configurable on the page.

This README gathers the **planning**, **design decisions**, **architecture**, **usage**, and the **fixes/lessons learned** from implementation and testing.

---

## 1. Goal

Provide a simple, self-contained IP camera:

- View the stream in **any browser** (no native app).
- Configure Wi‑Fi **without recompiling** (captive portal).
- Choose **DHCP or static IP** on the LAN.
- Adjust **resolution and JPEG quality** from the web UI.
- Handle known limitations of the **ESP32-CAM + CAM-MB** stack (power, boot, serial).

### Out of MVP scope

| Item | Reason |
|------|--------|
| Internet access | Complexity (NAT, tunnel, security) |
| Authentication (user/password) | Local network considered enough for MVP |
| Snapshot / recording / microSD | Not requested in MVP |
| Flash LED, motion detection, AI | Post-MVP |
| Native app / Home Assistant | Optional future integration |
| Multiple simultaneous stream clients | Limitation of the synchronous `WebServer` |

---

## 2. Planning and decisions (agreed with the user)

Decisions made **before** implementation, through rounds of questions:

| Topic | Decision |
|-------|----------|
| Main goal | **Live streaming** |
| Client | **Browser** (web) |
| Network | **Local only** (home Wi‑Fi) |
| Hardware | **ESP32-CAM-MB** (AI-Thinker + USB shield) |
| Image quality | **Configurable on the web page** |
| Authentication | **None** (local network is enough) |
| MVP extras | **Stream only** (minimum viable) |
| Wi‑Fi | **WiFiManager portal** |
| IP | **DHCP or static** (portal-configurable, persisted in NVS) |
| Initial delivery | Plan first; then MVP implementation |

### Additional technical requirements added to the plan

1. **Configurable IP**
   - DHCP (default) or static (IP, gateway, netmask, DNS).
   - Persistence in **Preferences/NVS** (survives reboot).
   - Basic validation; invalid IP → DHCP fallback + Serial log.
   - Recommendation: static IP **outside the router’s DHCP pool**.

2. **CAM-MB boot issues**
   - Brownout / current spikes over USB.
   - Firmware mitigations and hardware documentation.
   - Boot sequence designed for portal availability and stability.

---

## 3. Architecture

```
[Phone/PC on same LAN]
        │
        │  http://IP/  or  http://esp32cam.local/
        ▼
┌───────────────────────────────────────┐
│              ESP32-CAM                │
│                                       │
│  boot     → brownout off, logs        │
│  network  → WiFiManager, DHCP/static  │
│  camera   → OV2640, retry, framesize  │
│  webserver→ HTML + MJPEG + /control   │
└───────────────────────────────────────┘
```

### Boot order (current version)

```
1. Disable brownout detector
2. Settle + Serial (115200)
3. Wi-Fi / WiFiManager portal   ← AP comes up early if no network
4. OV2640 camera (retry)
5. HTTP server (page + stream)
```

> **History:** the first version started the **camera before Wi‑Fi** (less simultaneous peak load). In testing, that **delayed or prevented** the config AP if the camera failed/rebooted.  
> **Fix:** portal **before** the camera — prioritizes configuration and network access.

### Stack

| Layer | Technology |
|-------|------------|
| Framework | Arduino + ESP32 core |
| Board FQBN | `esp32:esp32:esp32cam` |
| Wi‑Fi config | WiFiManager 2.x |
| IP persistence | Preferences (NVS), namespace `netcfg` |
| HTTP | `WebServer` (port 80) |
| Stream | MJPEG (`multipart/x-mixed-replace`) |
| Discovery | mDNS `esp32cam.local` (when the OS resolves it) |

### File structure

```
esp32cam/
├── esp32cam.ino       # setup()/loop() — orchestration only
├── config.h           # pins, portal SSID, defaults
├── boot.h / boot.cpp  # brownout, settle, reset reason
├── camera.h / camera.cpp
├── network.h / network.cpp
├── webserver.h / webserver.cpp
└── README.md          # this document
```

**Modularization rule:** the `.ino` does not implement Wi‑Fi/camera/HTTP logic; each module exposes an API in `.h` and hides `static` state in the `.cpp`.

---

## 4. Hardware

### Bill of materials

- **ESP32-CAM** module (AI-Thinker), **OV2640** sensor.
- **CAM-MB shield** (USB-serial + IO0/RST buttons) for programming and USB power.

### Camera pinout (AI-Thinker)

Defined in `config.h` — PWDN 32, XCLK 0, SIOD 26, SIOC 27, Y2–Y9, VSYNC 25, HREF 23, PCLK 22, etc.

### Board settings in Arduino

- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled** when the option exists (improves frame buffers)

### Power supply notes (critical)

The ESP32-CAM draws **high current peaks** when Wi‑Fi and the camera start. With CAM-MB + a weak USB port:

| Symptom | Typical cause |
|---------|----------------|
| Reboot loop | Brownout / voltage sag |
| `camera init failed` | Power or camera ribbon cable |
| Works on one PC, not another | USB port current |
| Freezes when opening serial | Reset via DTR + heavy boot again |

**Recommendations:**

1. **Short**, good-quality USB cable (data + power).  
2. Prefer a high-current port or a **powered USB hub**.  
3. If unstable: **5 V ≥ 1 A** supply on the board’s **5V/GND**.  
4. Optional: **100–470 µF** capacitor between 5V and GND.  
5. Home Wi‑Fi on **2.4 GHz** (ESP32 does not use 5 GHz).

---

## 5. MVP features

### 5.1 Stream and web page

| URL | Function |
|-----|----------|
| `http://IP/` | UI: preview + resolution + quality (port **80**) |
| `http://IP:81/stream` | Continuous MJPEG stream (port **81**, separate server) |
| `http://IP/status` | JSON (IP, DHCP/STATIC mode, framesize, quality, heap, PSRAM) |
| `http://IP/control?var=framesize&val=N` | Change resolution (`framesize_t`) |
| `http://IP/control?var=quality&val=N` | JPEG quality **10–63** (lower = better) |

> **Why two ports?** With a single synchronous `WebServer`, `/stream` blocked the API: the page **did not refresh IP/mode** and the **Apply** button did not change framesize/quality. Solution (Espressif pattern): UI/API on **80**, stream on **81** via `esp_http_server`.

Boot defaults: **VGA (8)**, quality **12**.

### 5.2 Wi‑Fi portal

| Item | Value |
|------|--------|
| Setup AP SSID | **`ESP32CAM-Setup`** |
| AP password | **None** (open network) |
| Portal URL | `http://192.168.4.1` |
| Hostname / mDNS | `esp32cam` → `http://esp32cam.local/` |

Behavior:

- **No saved credentials:** opens the AP **immediately**, **no timeout** (stays until configured).  
- **With credentials:** tries the saved network; on failure, reopens the portal.  
- Extra portal fields: static IP (0/1), IP, gateway, netmask, DNS.

### 5.3 DHCP vs static IP

1. User chooses in the portal.  
2. Values go to NVS (`netcfg`).  
3. On boot: if static → `WiFi.config(...)` **before** connecting.  
4. After portal with static IP: disconnect, reapply config, reconnect (avoids staying on DHCP from the first association).  
5. Page and Serial show IP and mode (`DHCP` / `STATIC`).

---

## 6. Build, flash, and monitor

### Dependencies

```bash
arduino-cli lib install WiFiManager
```

ESP32 core installed; FQBN: `esp32:esp32:esp32cam`.

### Commands

```bash
cd /path/to/esp32cam

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32cam .

# Port
arduino-cli board list

# Upload (adjust the port)
arduino-cli upload --fqbn esp32:esp32:esp32cam --port /dev/ttyUSB0 .

# Monitor WITHOUT continuous forced reset (very important on CAM-MB)
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200,dtr=off,rts=off
```

### Manual upload if auto-reset fails

1. Hold **IO0** (program).  
2. Tap **RST**.  
3. Release IO0 and upload the sketch.

---

## 7. Day-to-day usage

### First-time setup

1. Flash the firmware and power the board.  
2. On phone/PC (**2.4 GHz Wi‑Fi**), connect to **`ESP32CAM-Setup`** (open).  
3. Open `http://192.168.4.1`.  
4. Select home network + password.  
5. IP: `0` = DHCP · `1` = static (fill IP/GW/netmask/DNS).  
6. Save and return to home Wi‑Fi.  
7. Open `http://IP/` (IP from Serial or via nmap/router).

### Accessing the stream

- Browser on the **same LAN**: `http://IP/`  
- mDNS shortcut (if the OS supports it): `http://esp32cam.local/`  
- **Do not use HTTPS** (HTTP only).  
- Serial is **not required** for normal use after Wi‑Fi is configured.

### Finding the IP with nmap

Replace the network with your LAN prefix (`ip -4 route`):

```bash
# Hosts with port 80 open
nmap -p 80 --open 192.168.1.0/24

# List candidate IPs only
nmap -p 80 --open -oG - 192.168.1.0/24 | awk '/80\/open/{print $2}'
```

For each candidate IP: open `http://IP/` or `curl -sI http://IP/`.

### Reconfiguring Wi‑Fi

Erase credentials (erase flash + reupload) or use a minimal sketch:

```cpp
#include <WiFi.h>
void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(1000);
  ESP.restart();
}
void loop() {}
```

On the next boot, the **`ESP32CAM-Setup`** AP appears again.

---

## 8. Fixes and lessons from testing

What was observed and **fixed** during real use with CAM-MB.

### 8.1 `ESP32CAM-Setup` AP did not appear

| Hypothesis | What we saw | Firmware fix |
|------------|-------------|--------------|
| Portal only after camera | OV2640 init is slow/fails/reboots; user never sees the AP | **Wi‑Fi/portal first**, camera later |
| `autoConnect` with old credentials | Tries STA and delays the AP | No saved network → **immediate `startConfigPortal`**, timeout 0 |
| Phone on 5 GHz | ESP32 is 2.4 GHz only | Document; AP channel fixed to **1** |
| AP “invisible” | Power/channel | High `WiFi.setTxPower`, channel 1, open network, clear Serial logs |

**Expected Serial message when the portal is active:**

```text
[NET ] sem rede salva — abrindo AP "ESP32CAM-Setup" agora
========================================
 PORTAL WiFi ATIVO
 SSID:  ESP32CAM-Setup
 Senha: (nenhuma — rede aberta)
 URL:   http://192.168.4.1
========================================
```

### 8.2 Opening the serial port “freezes” or looks like a reset

| Fact | Detail |
|------|--------|
| Cause | USB shield (CH340 etc.) pulses **DTR/RTS** → ESP32 **RST** pin |
| Effect | **One reboot** when opening the monitor is **normal** |
| Reset loop | Reboot + Wi‑Fi/camera peak + weak USB → brownout or crash loop |
| Bad default | `arduino-cli monitor` uses **`dtr=on`, `rts=on`** |

**Practical fix:**

```bash
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200,dtr=off,rts=off
```

**Firmware fixes:**

- Brownout disabled early.  
- Larger `BOOT_SETTLE_MS` (500 ms) after reset.  
- Log of **reset reason** (`POWERON`, `EXT` = DTR/RST, `BROWNOUT`, `PANIC`, etc.).  
- Serial tips when reason is `EXT` or `BROWNOUT`.

**Good debug order:** let the board boot ~10–15 s → only then open the monitor with DTR/RTS off.  
To only use the stream, serial is **not needed**.

### 8.3 Brownout and power

| Software mitigation | Hardware mitigation |
|---------------------|---------------------|
| `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` | Better cable/port/supply |
| VGA defaults (not UXGA at boot) | External 5 V ≥ 1 A if needed |
| Camera retry | Capacitor on 5V/GND if it persists |
| Stagger Wi‑Fi and camera in time | Avoid long cable / unpowered hub |

Disabling brownout **masks** voltage sag so detection does not loop; current still needs to be enough for stable Wi‑Fi + JPEG.

### 8.4 IP and access after configuring Wi‑Fi

Flow validated in tests:

1. Portal configures SSID/password (and optionally IP).  
2. Board joins the LAN.  
3. Serial (if stable) shows `[NET ] WiFi OK IP=...` and `[HTTP] http://...`.  
4. Without serial: **nmap** on port 80 or the router’s client list.  
5. Browser: `http://IP/` — same network, HTTP, 2.4 GHz.

### 8.5 Confirmed MVP limitations

- **One stream client** at a time: the `/stream` handler blocks the `WebServer` while the MJPEG connection is open.  
- Very high resolutions (SXGA/UXGA) need **PSRAM** and good Wi‑Fi; if it hangs, drop back to **VGA/SVGA**.  
- mDNS (`esp32cam.local`) depends on OS/network (works better on Linux/macOS; Windows is inconsistent).

---

## 9. Useful Serial logs (healthy boot)

Approximate OK sequence:

```text
========================================
 ESP32-CAM Stream MVP
========================================
[BOOT] brownout detector disabled
[BOOT] reset reason: 1 (POWERON)   # or EXT if serial was opened
[BOOT] heap free=... psram=...
[NET ] iniciando WiFi/portal (antes da camera)
[NET ] modo DHCP (padrao)
[NET ] credenciais WiFi salvas: sim
[NET ] WiFi OK  IP=192.168.x.x  RSSI=...  mode=DHCP
[HTTP] http://192.168.x.x/
[CAM ] PSRAM found — fb_count=2
[CAM ] init OK
[MAIN] setup concluido
```

| Log symptom | Action |
|-------------|--------|
| `reset reason: BROWNOUT` | Improve power supply |
| `reset reason: EXT` | Normal when opening serial; use dtr/rts off |
| `reset reason: PANIC` | Software crash — copy stack if available |
| `esp_camera_init failed` | OV2640 ribbon, board, PSRAM, power |
| Portal never goes away / no network | Still in setup; configure or clear credentials |
| WiFi OK but blank page | Test `/stream`; lower framesize |

---

## 10. Troubleshooting summary

| Problem | What to do |
|---------|------------|
| Cannot see `ESP32CAM-Setup` | Current firmware? 2.4 GHz? Serial shows portal? No old credentials? Power OK? |
| Configured Wi‑Fi but cannot find the board | nmap port 80; router; Serial if possible |
| Opening Serial reboots | Expected on CAM-MB; `dtr=off,rts=off` + `stty -hupcl` |
| Reset loop | 5 V supply, cable, powered hub; check reason on Serial |
| Stream does not load | Same network; HTTP; one client; VGA; `/status` |
| Want to change network | Clear Wi‑Fi NVS / erase flash and reconfigure portal |
| Upload fails | IO0 + RST; CH340 driver; data cable |

---

## 11. Future evolution (post-MVP)

Ideas aligned with what was left out, by suggested priority:

1. Button/hold to **force portal** (Wi‑Fi reset without flash erase).  
2. Snapshot (`/capture`) and optional flash LED.  
3. Basic HTTP auth if the LAN is not trusted.  
4. Async `esp_http_server` (multiple viewers).  
5. Home Assistant / MQTT integration.  
6. microSD recording or periodic photo upload.  
7. Remote access (controlled tunnel + auth).

---

## 12. Main constants (`config.h`)

| Symbol | Typical value | Meaning |
|--------|---------------|---------|
| `WIFI_PORTAL_NAME` | `ESP32CAM-Setup` | Config AP SSID |
| `WIFI_HOSTNAME` / `WIFI_MDNS_NAME` | `esp32cam` | Hostname and mDNS |
| `CAM_DEFAULT_FRAMESIZE` | `8` (VGA) | Resolution at boot |
| `CAM_DEFAULT_QUALITY` | `12` | JPEG (10–63) |
| `CAM_INIT_RETRIES` | `5` | Camera init attempts |
| `BOOT_SETTLE_MS` | `500` | Post-reset / serial pause |
| `WEB_SERVER_PORT` | `80` | HTTP |
| `NET_PREFS_NS` | `netcfg` | NVS namespace for IP |

---

## 13. Project timeline (summary)

| Stage | Outcome |
|-------|---------|
| Goal definition | Local browser stream, minimal MVP |
| IP and CAM-MB requirements | DHCP/static + anti-brownout plan |
| Modular implementation | boot, camera, network, webserver |
| Build | OK with `esp32:esp32:esp32cam` |
| Test: AP not listed | Portal before camera; `startConfigPortal` with no timeout |
| Test: Wi‑Fi configured | Access via LAN IP |
| Test: serial “freezes” | Documented DTR/RST; monitor with dtr/rts off; reset logs |
| Documentation | This unified README |

---

## 14. License and usage

Personal/study project. Use only on networks you are authorized to use. No authentication in the MVP: **do not expose port 80 to the internet** without extra security layers.

---

*Document produced from the planning, implementation, and testing of the ESP32-CAM Stream MVP.*
