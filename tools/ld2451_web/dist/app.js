"use strict";

const UUID = {
  services: [
    "0000ae00-0000-1000-8000-00805f9b34fb",
    "0000ae30-0000-1000-8000-00805f9b34fb",
    "0000fff0-0000-1000-8000-00805f9b34fb",
    "0000ffe0-0000-1000-8000-00805f9b34fb",
  ],
  write: [
    "0000fff2-0000-1000-8000-00805f9b34fb",
    "0000ae01-0000-1000-8000-00805f9b34fb",
    "0000fff3-0000-1000-8000-00805f9b34fb",
  ],
  notify: [
    "0000fff1-0000-1000-8000-00805f9b34fb",
    "0000ae02-0000-1000-8000-00805f9b34fb",
    "0000fff4-0000-1000-8000-00805f9b34fb",
  ],
};

const REPORT_HEADER = [0xf4, 0xf3, 0xf2, 0xf1];
const REPORT_TAIL = [0xf8, 0xf7, 0xf6, 0xf5];
const COMMAND_HEADER = [0xfd, 0xfc, 0xfb, 0xfa];
const COMMAND_TAIL = [0x04, 0x03, 0x02, 0x01];

const ui = Object.fromEntries([
  "status", "alarmBadge", "radarCanvas", "emptyRadar", "targetCards", "connectButton",
  "allDevicesButton", "disconnectButton", "browserHint", "firmware", "range", "direction",
  "minSpeed", "holdTime", "triggerCount", "snr", "readButton", "applyButton", "demoButton",
  "csvButton", "clearButton", "protocolLog", "packetCount", "uartHealth", "uartTerminal",
].map((id) => [id, document.getElementById(id)]));

const state = {
  device: null,
  server: null,
  writeCharacteristic: null,
  notifyCharacteristic: null,
  rx: [],
  targets: [],
  alarm: false,
  packets: 0,
  lastRxAt: 0,
  connectedAt: 0,
  csvRows: [["timestamp", "target", "angle_deg", "distance_m", "direction", "speed_kmh", "snr", "alarm"]],
  demoTimer: null,
  demoStarted: 0,
};

function setStatus(message, kind = "idle") {
  ui.status.dataset.state = kind;
  ui.status.querySelector("span").textContent = message;
}

function log(message) {
  const stamp = new Date().toLocaleTimeString([], { hour12: false });
  ui.protocolLog.textContent += `\n${stamp}  ${message}`;
  const lines = ui.protocolLog.textContent.split("\n");
  if (lines.length > 500) ui.protocolLog.textContent = lines.slice(-500).join("\n");
  ui.protocolLog.scrollTop = ui.protocolLog.scrollHeight;
}

function uartLog(message) {
  const stamp = new Date().toLocaleTimeString([], { hour12: false });
  if (ui.uartTerminal.textContent.startsWith("Waiting for")) ui.uartTerminal.textContent = "";
  ui.uartTerminal.textContent += `${ui.uartTerminal.textContent ? "\n" : ""}${stamp}  ${message}`;
  const lines = ui.uartTerminal.textContent.split("\n");
  if (lines.length > 100) ui.uartTerminal.textContent = lines.slice(-100).join("\n");
  ui.uartTerminal.scrollTop = ui.uartTerminal.scrollHeight;
}

function setUartHealth(label, stateName) {
  ui.uartHealth.dataset.state = stateName;
  ui.uartHealth.querySelector("b").textContent = label;
}

function updateUartHealth() {
  if (state.demoTimer) { setUartHealth("Demo stream", "receiving"); return; }
  if (!state.server?.connected) { setUartHealth("No data", "idle"); return; }
  if (!state.lastRxAt) {
    const waiting = Math.max(0, Math.floor((Date.now() - state.connectedAt) / 1000));
    setUartHealth(`Waiting · ${waiting}s`, waiting >= 10 ? "silent" : "waiting");
    return;
  }
  const age = Math.max(0, Math.floor((Date.now() - state.lastRxAt) / 1000));
  setUartHealth(age <= 2 ? "Receiving now" : age < 10 ? `Last packet · ${age}s` : `Silent · ${age}s`, age < 10 ? "receiving" : "silent");
}

function hex(bytes) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

function normalizeUuid(value) { return String(value).toLowerCase(); }
function matches(bytes, pattern, offset = 0) { return pattern.every((value, i) => bytes[offset + i] === value); }
function delay(ms) { return new Promise((resolve) => setTimeout(resolve, ms)); }

function buildCommand(command, payload = []) {
  const body = [command & 0xff, command >> 8, ...payload];
  return new Uint8Array([...COMMAND_HEADER, body.length & 0xff, body.length >> 8, ...body, ...COMMAND_TAIL]);
}

async function requestRadar(showAll = false) {
  if (!navigator.bluetooth) {
    setStatus("Web Bluetooth unavailable", "error");
    ui.browserHint.textContent = "Open this page in Bluefy on iPhone, or Chrome on a desktop computer.";
    log("Web Bluetooth API is not available in this browser.");
    return;
  }
  stopDemo();
  setStatus("Choose the radar…", "busy");
  try {
    const options = showAll
      ? { acceptAllDevices: true, optionalServices: UUID.services }
      : {
          filters: [
            { namePrefix: "HLK" },
            { namePrefix: "LD2451" },
            { services: [UUID.services[0]] },
            { services: [UUID.services[1]] },
          ],
          optionalServices: UUID.services,
        };
    const device = await navigator.bluetooth.requestDevice(options);
    await connectDevice(device);
  } catch (error) {
    if (error.name === "NotFoundError") {
      setStatus("No device selected", "idle");
      log("Device picker closed without a selection.");
    } else {
      setStatus("Connection failed", "error");
      log(`Connection error: ${error.message || error}`);
    }
  }
}

async function connectDevice(device) {
  state.device = device;
  device.addEventListener("gattserverdisconnected", onDisconnected);
  setStatus(`Connecting to ${device.name || "radar"}…`, "busy");
  log(`Selected ${device.name || "unnamed BLE device"}.`);
  state.server = await device.gatt.connect();
  state.connectedAt = Date.now();
  state.lastRxAt = 0;
  setUartHealth("Connected · waiting", "waiting");
  uartLog(`CONNECTED ${device.name || "unnamed BLE device"}`);

  const services = [];
  for (const uuid of UUID.services) {
    try { services.push(await state.server.getPrimaryService(uuid)); } catch (_) { /* optional service absent */ }
  }
  if (!services.length) throw new Error("No supported AE00/AE30/FFF0/FFE0 service was found.");

  const characteristics = [];
  for (const service of services) {
    const found = await service.getCharacteristics();
    characteristics.push(...found);
    log(`Service ${service.uuid}: ${found.map((c) => c.uuid).join(", ")}`);
  }
  state.writeCharacteristic = chooseCharacteristic(characteristics, UUID.write, "write");
  state.notifyCharacteristic = chooseCharacteristic(characteristics, UUID.notify, "notify");
  if (!state.writeCharacteristic || !state.notifyCharacteristic) {
    throw new Error("No writable/notifiable UART characteristics were found.");
  }
  log(`Selected UART write ${state.writeCharacteristic.uuid}; notify ${state.notifyCharacteristic.uuid}.`);

  state.notifyCharacteristic.addEventListener("characteristicvaluechanged", onNotification);
  await state.notifyCharacteristic.startNotifications();
  ui.disconnectButton.disabled = false;
  ui.readButton.disabled = false;
  ui.applyButton.disabled = false;
  setStatus(`Connected · ${device.name || "LD2451"}`, "connected");
  log(`Notifications active on ${state.notifyCharacteristic.uuid}.`);
  await readSettings();
}

function chooseCharacteristic(characteristics, preferred, operation) {
  for (const uuid of preferred) {
    const exact = characteristics.find((item) => normalizeUuid(item.uuid) === uuid);
    if (exact) return exact;
  }
  return characteristics.find((item) => operation === "write"
    ? item.properties.writeWithoutResponse || item.properties.write
    : item.properties.notify || item.properties.indicate);
}

function onDisconnected() {
  state.server = state.writeCharacteristic = state.notifyCharacteristic = null;
  ui.disconnectButton.disabled = true;
  ui.readButton.disabled = true;
  ui.applyButton.disabled = true;
  setStatus("Radar disconnected", "idle");
  setUartHealth("Disconnected", "idle");
  uartLog("DISCONNECTED");
  log("Bluetooth disconnected.");
}

function disconnect() {
  if (state.device?.gatt?.connected) state.device.gatt.disconnect();
  else onDisconnected();
}

async function writeBytes(bytes) {
  if (!state.writeCharacteristic) throw new Error("Connect to the radar first.");
  log(`TX ${hex(bytes)}`);
  for (let offset = 0; offset < bytes.length; offset += 20) {
    const chunk = bytes.slice(offset, offset + 20);
    if (state.writeCharacteristic.properties.writeWithoutResponse && state.writeCharacteristic.writeValueWithoutResponse) {
      await state.writeCharacteristic.writeValueWithoutResponse(chunk);
    } else if (state.writeCharacteristic.writeValueWithResponse) {
      await state.writeCharacteristic.writeValueWithResponse(chunk);
    } else {
      await state.writeCharacteristic.writeValue(chunk);
    }
    await delay(24);
  }
}

async function configSequence(commands) {
  try {
    await writeBytes(buildCommand(0x00ff, [0x01, 0x00]));
    await delay(220);
    for (const [command, payload] of commands) {
      await writeBytes(buildCommand(command, payload));
      await delay(280);
    }
    await writeBytes(buildCommand(0x00fe));
  } catch (error) {
    setStatus("Command failed", "error");
    log(`Write error: ${error.message || error}`);
  }
}

async function readSettings() {
  if (!state.writeCharacteristic) return;
  setStatus("Reading settings…", "busy");
  await configSequence([[0x0012, []], [0x0013, []], [0x00a0, []]]);
  if (state.server?.connected) setStatus(`Connected · ${state.device.name || "LD2451"}`, "connected");
}

async function applySettings() {
  const values = {
    range: clampNumber(ui.range, 10, 100),
    direction: Number(ui.direction.value),
    speed: clampNumber(ui.minSpeed, 0, 120),
    hold: clampNumber(ui.holdTime, 0, 255),
    trigger: clampNumber(ui.triggerCount, 1, 10),
    snr: Number(ui.snr.value),
  };
  setStatus("Applying settings…", "busy");
  await configSequence([
    [0x0002, [values.range, values.direction, values.speed, values.hold]],
    [0x0003, [values.trigger, values.snr, 0, 0]],
  ]);
  if (state.server?.connected) setStatus(`Connected · ${state.device.name || "LD2451"}`, "connected");
  drawRadar();
}

function clampNumber(input, minimum, maximum) {
  const value = Math.max(minimum, Math.min(maximum, Math.round(Number(input.value) || 0)));
  input.value = value;
  return value;
}

function onNotification(event) {
  const view = event.target.value;
  const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
  state.packets += 1;
  state.lastRxAt = Date.now();
  updateUartHealth();
  ui.packetCount.textContent = `${state.packets} packet${state.packets === 1 ? "" : "s"}`;
  log(`RX ${hex(bytes)}`);
  state.rx.push(...bytes);
  extractFrames();
}

function extractFrames() {
  while (true) {
    const reportAt = findHeader(state.rx, REPORT_HEADER);
    const commandAt = findHeader(state.rx, COMMAND_HEADER);
    const starts = [reportAt, commandAt].filter((position) => position >= 0);
    if (!starts.length) {
      if (state.rx.length > 3) state.rx = state.rx.slice(-3);
      return;
    }
    const start = Math.min(...starts);
    if (start) state.rx.splice(0, start);
    if (state.rx.length < 6) return;
    const length = state.rx[4] | (state.rx[5] << 8);
    if (length > 1024) { state.rx.shift(); continue; }
    const total = 10 + length;
    if (state.rx.length < total) return;
    const frame = state.rx.slice(0, total);
    const tail = matches(frame, REPORT_HEADER) ? REPORT_TAIL : COMMAND_TAIL;
    if (!matches(frame, tail, total - 4)) { state.rx.shift(); continue; }
    state.rx.splice(0, total);
    if (matches(frame, REPORT_HEADER)) parseReport(frame);
    else parseAck(frame);
  }
}

function findHeader(buffer, header) {
  for (let i = 0; i <= buffer.length - header.length; i += 1) if (matches(buffer, header, i)) return i;
  return -1;
}

function parseReport(frame) {
  const body = frame.slice(6, -4);
  const count = body[0];
  if (body.length !== 2 + count * 5) { log("Ignored malformed target report."); return; }
  state.alarm = Boolean(body[1]);
  state.targets = [];
  for (let i = 0; i < count; i += 1) {
    const offset = 2 + i * 5;
    state.targets.push({
      angle: body[offset] - 0x80,
      distance: body[offset + 1],
      approaching: body[offset + 2] === 0,
      speed: body[offset + 3],
      snr: body[offset + 4],
    });
  }
  uartLog(`RX TARGET ${hex(frame)}`);
  if (!state.targets.length) {
    uartLog(`REPORT clear · no targets · alarm=${state.alarm ? 1 : 0}`);
  } else {
    state.targets.forEach((target, index) => uartLog(
      `T${index + 1} ${target.approaching ? "APPROACH" : "AWAY"} · ${target.speed} km/h · ${target.distance} m · ${target.angle >= 0 ? "+" : ""}${target.angle}° · SNR ${target.snr}`
    ));
  }
  const timestamp = new Date().toISOString();
  state.targets.forEach((target, index) => state.csvRows.push([
    timestamp, index + 1, target.angle, target.distance,
    target.approaching ? "approaching" : "away", target.speed, target.snr, Number(state.alarm),
  ]));
  updateTargets();
}

function parseAck(frame) {
  const body = frame.slice(6, -4);
  if (body.length < 4) return;
  const response = body[0] | (body[1] << 8);
  const command = response & 0xff;
  const status = body[2] | (body[3] << 8);
  const payload = body.slice(4);
  log(`ACK 0x${command.toString(16).padStart(2, "0").toUpperCase()}: ${status ? `failed (${status})` : "OK"}`);
  if (status) return;
  if (command === 0x12 && payload.length >= 4) {
    ui.range.value = payload[0];
    ui.direction.value = Math.min(payload[1], 2);
    ui.minSpeed.value = payload[2];
    ui.holdTime.value = payload[3];
  } else if (command === 0x13 && payload.length >= 2) {
    ui.triggerCount.value = payload[0];
    if ([0, 3, 4, 5, 6, 7, 8].includes(payload[1])) ui.snr.value = payload[1];
  } else if (command === 0xa0 && payload.length >= 8) {
    const radarType = payload[0] | (payload[1] << 8);
    const build = payload.slice(4, 8).reverse().map((byte) => byte.toString(16).padStart(2, "0")).join("").toUpperCase();
    ui.firmware.textContent = `0x${radarType.toString(16).padStart(4, "0").toUpperCase()} · V${payload[2]}.${String(payload[3]).padStart(2, "0")}.${build}`;
  }
  drawRadar();
}

function speedColor(speed) {
  if (speed >= 45) return "#ff4d67";
  if (speed >= 20) return "#ffcf56";
  return "#23d5ab";
}

function updateTargets() {
  ui.emptyRadar.classList.toggle("hidden", state.targets.length > 0);
  ui.alarmBadge.classList.toggle("active", state.alarm);
  ui.alarmBadge.querySelector("span").textContent = state.alarm ? "Target alert" : "Clear";
  if (!state.targets.length) {
    ui.targetCards.innerHTML = '<div class="target-empty">No targets in range</div>';
  } else {
    ui.targetCards.innerHTML = state.targets.map((target, index) => `
      <article class="target-card" style="border-top-color:${speedColor(target.speed)}">
        <header><span>Target ${index + 1}</span><span>${target.approaching ? "Approaching" : "Away"}</span></header>
        <strong style="color:${speedColor(target.speed)}">${target.speed} km/h</strong>
        <footer><span>${target.distance} m</span><span>${target.angle > 0 ? "+" : ""}${target.angle}° · SNR ${target.snr}</span></footer>
      </article>`).join("");
  }
  drawRadar();
}

function drawRadar() {
  const canvas = ui.radarCanvas;
  const rect = canvas.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, rect.width);
  const height = Math.max(1, rect.height);
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const originX = width / 2;
  const originY = 28;
  const maxRange = Math.max(10, Number(ui.range.value) || 100);
  const usable = height - originY - 26;
  const halfWidth = Math.min(width * .47, usable * .72);
  const coneAngle = Math.atan2(halfWidth, usable);
  const radius = Math.hypot(halfWidth, usable);

  ctx.save();
  const glow = ctx.createRadialGradient(originX, originY, 0, originX, originY, radius);
  glow.addColorStop(0, "rgba(35,213,171,.12)");
  glow.addColorStop(1, "rgba(35,213,171,0)");
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.moveTo(originX, originY);
  ctx.arc(originX, originY, radius, Math.PI / 2 - coneAngle, Math.PI / 2 + coneAngle);
  ctx.closePath();
  ctx.fill();

  ctx.strokeStyle = "rgba(89,219,233,.25)";
  ctx.lineWidth = 1;
  ctx.setLineDash([5, 7]);
  [-1, 0, 1].forEach((position) => {
    ctx.beginPath();
    ctx.moveTo(originX, originY);
    ctx.lineTo(originX + halfWidth * position, originY + usable);
    ctx.stroke();
  });

  ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, monospace";
  ctx.textAlign = "center";
  for (let ring = 1; ring <= 4; ring += 1) {
    const fraction = ring / 4;
    const yRadius = usable * fraction;
    const xRadius = halfWidth * fraction;
    ctx.beginPath();
    ctx.ellipse(originX, originY, xRadius, yRadius, 0, 0, Math.PI);
    ctx.strokeStyle = ring === 4 ? "rgba(89,219,233,.26)" : "rgba(143,164,184,.14)";
    ctx.stroke();
    ctx.fillStyle = "rgba(143,164,184,.72)";
    ctx.fillText(`${Math.round(maxRange * fraction)} m`, originX, originY + yRadius - 6);
  }
  ctx.restore();

  for (const target of state.targets) {
    const fraction = Math.min(target.distance / maxRange, 1);
    const angle = target.angle * Math.PI / 180;
    const y = originY + usable * fraction * Math.max(.2, Math.cos(angle));
    const x = originX + usable * fraction * Math.sin(angle);
    const color = speedColor(target.speed);
    ctx.save();
    ctx.shadowColor = color;
    ctx.shadowBlur = 18;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
    ctx.strokeStyle = "rgba(255,255,255,.9)";
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.font = "700 12px ui-sans-serif, -apple-system, sans-serif";
    ctx.textAlign = "center";
    ctx.fillStyle = "#ecf6ff";
    ctx.fillText(`${target.speed} km/h`, x, y + 26);
    ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, monospace";
    ctx.fillStyle = "#8fa4b8";
    ctx.fillText(`${target.distance} m`, x, y + 41);
    ctx.restore();
  }
}

function startDemo() {
  if (state.demoTimer) { stopDemo(); return; }
  state.demoStarted = performance.now();
  ui.demoButton.textContent = "Stop demo";
  uartLog("DEMO target stream started");
  setStatus("Demo targets", "busy");
  const tick = () => {
    const seconds = (performance.now() - state.demoStarted) / 1000;
    state.alarm = true;
    state.targets = [
      { angle: Math.round(8 * Math.sin(seconds / 3)), distance: Math.max(8, 78 - Math.floor(seconds * 2) % 70), approaching: true, speed: 46, snr: 21 },
      { angle: -14, distance: 54, approaching: true, speed: 82, snr: 15 },
      ...(Math.floor(seconds) % 8 < 5 ? [{ angle: 17, distance: 35, approaching: false, speed: 18, snr: 11 }] : []),
    ];
    state.lastRxAt = Date.now();
    setUartHealth("Demo stream", "receiving");
    updateTargets();
  };
  tick();
  state.demoTimer = setInterval(tick, 180);
}

function stopDemo() {
  if (!state.demoTimer) return;
  clearInterval(state.demoTimer);
  state.demoTimer = null;
  ui.demoButton.textContent = "Start demo";
  uartLog("DEMO target stream stopped");
  updateUartHealth();
  if (!state.server?.connected) setStatus("Ready to connect", "idle");
}

function downloadCsv() {
  const csv = state.csvRows.map((row) => row.map((cell) => `"${String(cell).replaceAll('"', '""')}"`).join(",")).join("\n");
  const link = document.createElement("a");
  link.href = URL.createObjectURL(new Blob([csv], { type: "text/csv" }));
  link.download = `ld2451-${new Date().toISOString().replaceAll(":", "-")}.csv`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

ui.connectButton.addEventListener("click", () => requestRadar(false));
ui.allDevicesButton.addEventListener("click", () => requestRadar(true));
ui.disconnectButton.addEventListener("click", disconnect);
ui.readButton.addEventListener("click", readSettings);
ui.applyButton.addEventListener("click", applySettings);
ui.demoButton.addEventListener("click", startDemo);
ui.csvButton.addEventListener("click", downloadCsv);
ui.clearButton.addEventListener("click", () => { ui.protocolLog.textContent = "Log cleared."; });
ui.range.addEventListener("input", drawRadar);
window.addEventListener("resize", drawRadar);
setInterval(updateUartHealth, 1000);

if (!navigator.bluetooth) {
  ui.browserHint.textContent = "Web Bluetooth is unavailable here. On iPhone, open this page in the Bluefy app.";
}
drawRadar();
