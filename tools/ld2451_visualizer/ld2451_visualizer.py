#!/usr/bin/env python3
"""Qt BLE visualizer and settings editor for the Hi-Link HLK-LD2451."""

from __future__ import annotations

import argparse
import asyncio
import csv
from datetime import datetime
import math
import os
from pathlib import Path
import queue
import shutil
import stat
import sys
import tempfile
import threading
import time

import PySide6

# Files inside an iCloud-backed Documents folder can appear hidden to Qt's C++
# directory scanner, while remaining readable by Python. Put the three small
# platform plugins in /tmp and link their expected Qt library directory there.
_QT_PACKAGE = Path(PySide6.__file__).resolve().parent / "Qt"
_QT_SOURCE_PLUGINS = _QT_PACKAGE / "plugins"
_QT_PLUGIN_ROOT = _QT_SOURCE_PLUGINS
if sys.platform == "darwin":
    runtime = Path(tempfile.gettempdir()) / (
        f"ld2451-qt-{os.getuid()}-{PySide6.__version__.replace('.', '_')}"
    )
    runtime_platforms = runtime / "plugins" / "platforms"
    try:
        runtime_platforms.mkdir(parents=True, exist_ok=True)
        for source in (_QT_SOURCE_PLUGINS / "platforms").glob("*.dylib"):
            destination = runtime_platforms / source.name
            if not destination.exists() or destination.stat().st_size != source.stat().st_size:
                shutil.copyfile(source, destination)
                destination.chmod(0o755)
            hidden_flag = getattr(stat, "UF_HIDDEN", 0)
            if hidden_flag and destination.stat().st_flags & hidden_flag:
                os.chflags(destination, destination.stat().st_flags & ~hidden_flag)
        runtime_lib = runtime / "lib"
        if not runtime_lib.exists():
            runtime_lib.symlink_to(_QT_PACKAGE / "lib", target_is_directory=True)
        _QT_PLUGIN_ROOT = runtime / "plugins"
    except OSError:
        # Normal PySide6 discovery remains the fallback outside iCloud/TCC cases.
        _QT_PLUGIN_ROOT = _QT_SOURCE_PLUGINS

_QT_PLATFORM_PLUGINS = _QT_PLUGIN_ROOT / "platforms"
os.environ["QT_PLUGIN_PATH"] = str(_QT_PLUGIN_ROOT)
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(_QT_PLATFORM_PLUGINS)

from PySide6.QtCore import QCoreApplication, QPointF, QRectF, Qt, QTimer
from PySide6.QtGui import QColor, QFont, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QSpinBox,
    QSplitter, QVBoxLayout, QWidget,
)

from ld2451_protocol import (
    COMMAND_HEADER, REPORT_HEADER, FrameStream, Report, Target, build_command,
    parse_ack, parse_report,
)


WRITE_UUIDS = (
    "0000fff2-0000-1000-8000-00805f9b34fb",
    "0000ae01-0000-1000-8000-00805f9b34fb",
    "0000fff3-0000-1000-8000-00805f9b34fb",
)
NOTIFY_UUIDS = (
    "0000fff1-0000-1000-8000-00805f9b34fb",
    "0000ae02-0000-1000-8000-00805f9b34fb",
    "0000fff4-0000-1000-8000-00805f9b34fb",
)
SCAN_SERVICE_UUIDS = (
    "0000ae00-0000-1000-8000-00805f9b34fb",
    "0000ae30-0000-1000-8000-00805f9b34fb",
    "0000fff0-0000-1000-8000-00805f9b34fb",
    "0000ffe0-0000-1000-8000-00805f9b34fb",
)


class BleWorker:
    """Run Bleak on an asyncio thread and pass events to the Qt thread."""

    def __init__(self, events: queue.Queue) -> None:
        self.events = events
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        self.client = None
        self.write_uuid = None

    def _run(self) -> None:
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def submit(self, coroutine) -> None:
        asyncio.run_coroutine_threadsafe(coroutine, self.loop)

    async def scan(self) -> None:
        self.events.put(("scan_started", None))
        try:
            from bleak import BleakScanner

            if sys.platform == "darwin":
                try:
                    from CoreBluetooth import CBCentralManager
                    authorization_names = {
                        0: "not determined",
                        1: "restricted",
                        2: "denied",
                        3: "allowed",
                    }
                    authorization = int(CBCentralManager.authorization())
                    self.events.put((
                        "diagnostic",
                        f"CoreBluetooth authorization: "
                        f"{authorization_names.get(authorization, authorization)}",
                    ))
                except Exception as exc:
                    self.events.put(("diagnostic", f"Authorization query failed: {exc}"))

            seen: set[str] = set()

            def detected(device, advertisement) -> None:
                name = advertisement.local_name or device.name or "Unknown BLE device"
                services = tuple(str(uuid).lower() for uuid in advertisement.service_uuids)
                self.events.put((
                    "device",
                    (name, device.address, advertisement.rssi, services),
                ))
                seen.add(device.address)

            self.events.put((
                "scan_phase",
                "Targeted scan: AE00, AE30, FFF0 and FFE0 services…",
            ))
            scanner = BleakScanner(
                detection_callback=detected,
                service_uuids=list(SCAN_SERVICE_UUIDS),
            )
            await asyncio.wait_for(scanner.start(), timeout=5.0)
            await asyncio.sleep(10.0)
            await asyncio.wait_for(scanner.stop(), timeout=5.0)

            self.events.put(("scan_phase", "General unfiltered BLE scan…"))
            scanner = BleakScanner(detection_callback=detected)
            await asyncio.wait_for(scanner.start(), timeout=5.0)
            await asyncio.sleep(12.0)
            await asyncio.wait_for(scanner.stop(), timeout=5.0)
            self.events.put(("scan_done", len(seen)))
        except Exception as exc:
            detail = str(exc)
            if sys.platform == "darwin" and (
                "unsupported" in detail.lower() or "permission" in detail.lower()
            ):
                detail += (
                    ". Enable Python or your terminal in System Settings → "
                    "Privacy & Security → Bluetooth, then restart the app"
                )
            self.events.put(("error", f"BLE scan failed: {detail}"))

    async def connect(self, address: str) -> None:
        try:
            from bleak import BleakClient
            if self.client and self.client.is_connected:
                await self.client.disconnect()
            self.events.put(("status", "Connecting…"))
            self.client = BleakClient(address, disconnected_callback=self._disconnected)
            await self.client.connect(timeout=15.0)
            services = self.client.services
            chars = [c for service in services for c in service.characteristics]
            self.write_uuid = self._choose(chars, WRITE_UUIDS, "write")
            notify_uuid = self._choose(chars, NOTIFY_UUIDS, "notify")
            inventory = [
                f"{service.uuid}: " + ", ".join(
                    f"{c.uuid} [{'/'.join(c.properties)}]"
                    for c in service.characteristics
                )
                for service in services
            ]
            self.events.put(("gatt", inventory))
            if not self.write_uuid or not notify_uuid:
                raise RuntimeError("no writable/notifiable UART characteristics found")
            self.events.put((
                "diagnostic",
                f"Selected UART write {self.write_uuid}; notify {notify_uuid}",
            ))
            await self.client.start_notify(notify_uuid, self._notification)
            self.events.put(("connected", str(self.client.address)))
        except Exception as exc:
            self.events.put(("error", f"BLE connection failed: {exc}"))

    @staticmethod
    def _choose(chars, preferred, operation):
        by_uuid = {str(c.uuid).lower(): c for c in chars}
        for uuid in preferred:
            if uuid in by_uuid:
                return str(by_uuid[uuid].uuid)
        wanted = (
            ("write", "write-without-response")
            if operation == "write" else ("notify", "indicate")
        )
        for char in chars:
            if any(prop in char.properties for prop in wanted):
                return str(char.uuid)
        return None

    def _notification(self, _sender, data: bytearray) -> None:
        self.events.put(("bytes", bytes(data)))

    def _disconnected(self, _client) -> None:
        self.events.put(("status", "Disconnected"))

    async def write(self, data: bytes) -> None:
        if not self.client or not self.client.is_connected or not self.write_uuid:
            self.events.put(("error", "Connect to the radar first."))
            return
        try:
            for offset in range(0, len(data), 20):
                await self.client.write_gatt_char(
                    self.write_uuid, data[offset:offset + 20], response=False
                )
                await asyncio.sleep(0.02)
            self.events.put(("tx", data))
        except Exception as exc:
            self.events.put(("error", f"BLE write failed: {exc}"))

    async def disconnect(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.disconnect()


class RadarView(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.report = Report(False, ())
        self.range_m = 100
        self.setMinimumSize(500, 500)

    def set_data(self, report: Report, range_m: int) -> None:
        self.report = report
        self.range_m = max(10, range_m)
        self.update()

    def paintEvent(self, _event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor("#07111e"))
        width, height = self.width(), self.height()
        # Rider/radar is at the top; increasing range moves down the screen.
        origin = QPointF(width / 2, 58)
        radius = min(width * 0.92, height * 1.65)
        half_angle = math.radians(20)
        left = QPointF(origin.x() - radius * math.sin(half_angle),
                       origin.y() + radius * math.cos(half_angle))
        right = QPointF(origin.x() + radius * math.sin(half_angle),
                        origin.y() + radius * math.cos(half_angle))
        p.setPen(QPen(QColor("#1f9ccc"), 2))
        p.setBrush(QColor("#0b2940"))
        p.drawPolygon(QPolygonF([origin, left, right]))

        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setFont(QFont("Helvetica", 10))
        for fraction in (0.25, 0.5, 0.75, 1.0):
            ring = radius * fraction
            p.setPen(QPen(QColor("#24516a"), 1))
            p.drawArc(QRectF(origin.x() - ring, origin.y() - ring,
                             ring * 2, ring * 2), 250 * 16, 40 * 16)
            p.setPen(QColor("#66879a"))
            p.drawText(QRectF(origin.x() - 40, origin.y() + ring * .96 - 10,
                              80, 20), Qt.AlignmentFlag.AlignCenter,
                       f"{self.range_m * fraction:.0f} m")

        p.setFont(QFont("Helvetica", 15, QFont.Weight.Bold))
        p.setPen(QColor("#ff4f5e" if self.report.alarm else "#51d890"))
        p.drawText(QRectF(18, 12, width / 2, 30),
                   Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                   "APPROACH ALERT" if self.report.alarm else "ROAD CLEAR")
        p.setFont(QFont("Helvetica", 12))
        p.setPen(QColor("#a8bfd0"))
        count = len(self.report.targets)
        p.drawText(QRectF(width / 2, 12, width / 2 - 18, 30),
                   Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                   f"{count} target{'s' if count != 1 else ''}")

        p.setPen(QPen(QColor("#60cbed"), 2))
        p.setBrush(QColor("#e8f3f8"))
        p.drawPolygon(QPolygonF([
            QPointF(origin.x(), origin.y() + 18),
            QPointF(origin.x() - 11, origin.y() - 13),
            QPointF(origin.x() + 11, origin.y() - 13),
        ]))

        scale = radius / self.range_m
        for number, target in enumerate(self.report.targets, 1):
            x = origin.x() + target.x_m * scale
            y = origin.y() + target.y_m * scale
            p.setPen(QPen(QColor("white"), 2))
            p.setBrush(QColor(self._target_color(target)))
            p.drawEllipse(QPointF(x, y), 15, 15)
            p.setPen(QColor("#07111e"))
            p.setFont(QFont("Helvetica", 11, QFont.Weight.Bold))
            p.drawText(QRectF(x - 15, y - 15, 30, 30),
                       Qt.AlignmentFlag.AlignCenter, str(number))
            direction = "↓ approaching" if target.approaching else "↑ away"
            label = (f"{target.speed_kmh} km/h  •  {target.distance_m} m\n"
                     f"{target.angle_deg:+d}°  •  SNR {target.snr}  •  {direction}")
            p.setPen(QColor("#e8f3f8"))
            p.setFont(QFont("Helvetica", 10, QFont.Weight.Bold))
            p.drawText(QRectF(x - 145, y + 20, 290, 45),
                       Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
                       label)

    @staticmethod
    def _target_color(target: Target) -> str:
        if not target.approaching:
            return "#58a6ff"
        if target.speed_kmh >= 70:
            return "#ff4f5e"
        if target.speed_kmh >= 35:
            return "#ffad3d"
        return "#f7dc5d"


class App(QMainWindow):
    def __init__(self, demo: bool = False) -> None:
        super().__init__()
        self.setWindowTitle("LD2451 Radar Console")
        self.resize(1250, 760)
        self.setMinimumSize(980, 620)
        self.events: queue.Queue = queue.Queue()
        self.worker = BleWorker(self.events)
        self.stream = FrameStream()
        self.last_report = Report(False, ())
        self.device_map: dict[str, str] = {}
        self.device_labels: dict[str, str] = {}
        self.diagnostic_addresses: set[str] = set()
        self.csv_writer = self.csv_file = None
        self.rx_seen = False
        self.demo_started = time.monotonic()
        self._build_ui()
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self._poll_events)
        self.poll_timer.start(40)
        if demo:
            self._set_status("Demo mode")
            self.demo_timer = QTimer(self)
            self.demo_timer.timeout.connect(self._demo_tick)
            self.demo_timer.start(250)
            self._demo_tick()

    def _build_ui(self) -> None:
        self.setStyleSheet("""
            QMainWindow, QWidget { background: #07111e; color: #dcecf5; }
            QGroupBox { border: 1px solid #29465c; border-radius: 6px;
                margin-top: 12px; padding-top: 10px; font-weight: bold; color: #8fdcff; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
            QPushButton { background: #18344a; border: 1px solid #2e6483;
                border-radius: 5px; padding: 7px 12px; }
            QPushButton:hover { background: #23506f; }
            QComboBox, QSpinBox, QPlainTextEdit { background: #0e1c2b;
                border: 1px solid #29465c; border-radius: 4px; padding: 5px; }
            QSplitter::handle { background: #29465c; width: 2px; }
        """)
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        outer.setContentsMargins(8, 8, 8, 8)

        toolbar = QHBoxLayout()
        scan = QPushButton("Scan")
        scan.clicked.connect(lambda: self.worker.submit(self.worker.scan()))
        toolbar.addWidget(scan)
        self.device_box = QComboBox()
        self.device_box.setMinimumWidth(320)
        toolbar.addWidget(self.device_box)
        self.hlk_filter = QCheckBox("LD2451 / HLK only")
        self.hlk_filter.setChecked(True)
        self.hlk_filter.setToolTip(
            "Show only likely LD2451 radar advertisements. "
            "Uncheck to inspect every BLE advertisement."
        )
        toolbar.addWidget(self.hlk_filter)
        connect = QPushButton("Connect")
        connect.clicked.connect(self._connect)
        toolbar.addWidget(connect)
        disconnect = QPushButton("Disconnect")
        disconnect.clicked.connect(lambda: self.worker.submit(self.worker.disconnect()))
        toolbar.addWidget(disconnect)
        self.status_label = QLabel("Ready — click Scan")
        toolbar.addWidget(self.status_label, 1)
        self.log_button = QPushButton("Start CSV log")
        self.log_button.clicked.connect(self._toggle_log)
        toolbar.addWidget(self.log_button)
        outer.addLayout(toolbar)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.radar = RadarView()
        splitter.addWidget(self.radar)
        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(12, 4, 4, 4)
        splitter.addWidget(right)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 2)
        outer.addWidget(splitter, 1)

        settings = QGroupBox("Radar settings")
        form = QFormLayout(settings)
        self.range_spin = self._spin(10, 100, 100, " m")
        form.addRow("Maximum range", self.range_spin)
        self.direction_box = QComboBox()
        self.direction_box.addItems(("Away only", "Approaching only", "Both"))
        self.direction_box.setCurrentIndex(1)
        form.addRow("Direction", self.direction_box)
        self.speed_spin = self._spin(0, 120, 5, " km/h")
        form.addRow("Minimum speed", self.speed_spin)
        self.hold_spin = self._spin(0, 255, 2, " s")
        form.addRow("No-target delay", self.hold_spin)
        self.trigger_spin = self._spin(1, 10, 1, " hits")
        form.addRow("Trigger count", self.trigger_spin)
        self.snr_spin = self._spin(0, 8, 0, " (0 = default)")
        form.addRow("SNR threshold", self.snr_spin)
        buttons = QHBoxLayout()
        for label, callback in (("Read settings", self._read_settings),
                                ("Apply", self._apply_settings),
                                ("Firmware", self._read_firmware)):
            button = QPushButton(label)
            button.clicked.connect(callback)
            buttons.addWidget(button)
        form.addRow(buttons)
        right_layout.addWidget(settings)

        live = QGroupBox("Live data")
        live_layout = QVBoxLayout(live)
        self.live_label = QLabel("No target frames received")
        self.live_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.firmware_label = QLabel("Firmware: —")
        live_layout.addWidget(self.live_label)
        live_layout.addWidget(self.firmware_label)
        right_layout.addWidget(live)

        protocol = QGroupBox("Protocol log / GATT details")
        protocol_layout = QVBoxLayout(protocol)
        self.raw_text = QPlainTextEdit()
        self.raw_text.setReadOnly(True)
        self.raw_text.document().setMaximumBlockCount(800)
        protocol_layout.addWidget(self.raw_text, 1)
        clear = QPushButton("Clear")
        clear.clicked.connect(self.raw_text.clear)
        protocol_layout.addWidget(clear, 0, Qt.AlignmentFlag.AlignRight)
        right_layout.addWidget(protocol, 1)

    @staticmethod
    def _spin(low: int, high: int, value: int, suffix: str) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(low, high)
        spin.setValue(value)
        spin.setSuffix(suffix)
        return spin

    def _connect(self) -> None:
        label = self.device_box.currentText()
        if not label:
            QMessageBox.information(self, "LD2451", "Scan, then select an LD2451 device.")
            return
        self.worker.submit(self.worker.connect(self.device_map[label]))

    def _read_settings(self) -> None:
        self._config_sequence([(0x0012, b""), (0x0013, b"")])

    def _read_firmware(self) -> None:
        self._config_sequence([(0x00A0, b"")])

    def _apply_settings(self) -> None:
        snr = self.snr_spin.value()
        if snr not in (0, 3, 4, 5, 6, 7, 8):
            QMessageBox.critical(self, "Invalid SNR",
                                 "Use 0 for the default, or a threshold from 3 to 8.")
            return
        detection = bytes((self.range_spin.value(), self.direction_box.currentIndex(),
                           self.speed_spin.value(), self.hold_spin.value()))
        sensitivity = bytes((self.trigger_spin.value(), snr, 0, 0))
        self._config_sequence([(0x0002, detection), (0x0003, sensitivity)])

    def _config_sequence(self, commands) -> None:
        async def sequence():
            await self.worker.write(build_command(0x00FF, b"\x01\x00"))
            await asyncio.sleep(.25)
            for command, value in commands:
                await self.worker.write(build_command(command, value))
                await asyncio.sleep(.3)
            await self.worker.write(build_command(0x00FE))
        self.worker.submit(sequence())

    def _poll_events(self) -> None:
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "scan_started":
                    self.device_map.clear()
                    self.device_labels.clear()
                    self.diagnostic_addresses.clear()
                    self.device_box.clear()
                    self._set_status("Starting targeted radar scan…")
                    self._append_log("BLE scan started. Close HLKRadarTool on nearby phones.")
                elif kind == "scan_phase":
                    self._set_status(value)
                    self._append_log(value)
                elif kind == "diagnostic":
                    self._append_log(value)
                elif kind == "device":
                    name, address, rssi, services = value
                    likely_radar = (
                        "LD2451" in name.upper()
                        or any("ae00" in uuid or "ae30" in uuid for uuid in services)
                    )
                    name_upper = name.upper()
                    visible_radar = (
                        name_upper.startswith("HLK")
                        or "LD2451" in name_upper
                        or likely_radar
                    )
                    if self.hlk_filter.isChecked() and not visible_radar:
                        if likely_radar and address not in self.diagnostic_addresses:
                            self.diagnostic_addresses.add(address)
                            self._append_log(
                                f"Radar-like advertisement filtered: {name}, "
                                f"{address}, {rssi} dBm, services: "
                                + (", ".join(services) if services else "not advertised")
                            )
                        continue
                    marker = "★ " if likely_radar else ""
                    label = f"{marker}{name}  ({address})  {rssi} dBm"
                    old_label = self.device_labels.get(address)
                    if old_label == label:
                        continue
                    if old_label:
                        index = self.device_box.findText(old_label)
                        if index >= 0:
                            self.device_box.removeItem(index)
                        self.device_map.pop(old_label, None)
                    self.device_labels[address] = label
                    self.device_map[label] = address
                    if likely_radar:
                        self.device_box.insertItem(0, label)
                        self.device_box.setCurrentIndex(0)
                        self._append_log(
                            f"Possible radar: {name}, {rssi} dBm, services: "
                            + (", ".join(services) if services else "not advertised")
                        )
                    else:
                        self.device_box.addItem(label)
                elif kind == "scan_done":
                    visible = len(self.device_map)
                    if visible:
                        noun = "radar device" if self.hlk_filter.isChecked() else "BLE device"
                        self._set_status(
                            f"Scan complete — showing {visible} {noun}(s); {value} total seen"
                        )
                    elif value and self.hlk_filter.isChecked():
                        self._set_status(
                            f"No LD2451/HLK advertisements — {value} other BLE device(s) seen"
                        )
                        self._append_log(
                            "No LD2451/HLK advertisement was found. Force-quit "
                            "HLKRadarTool, power-cycle the radar, wait five seconds, "
                            "and scan again."
                        )
                    else:
                        self._set_status(
                            "No BLE advertisements found — check macOS Bluetooth permission"
                        )
                        self._append_log(
                            "No devices were reported. In System Settings → Privacy & Security "
                            "→ Bluetooth, allow your terminal/Python, then restart the app."
                        )
                elif kind == "connected":
                    self._set_status(f"Connected: {value}")
                    self._append_log("Connected. Waiting for FFF1 radar notifications…")
                    self.rx_seen = False
                    QTimer.singleShot(350, lambda: self._config_sequence(
                        [(0x0012, b""), (0x0013, b""), (0x00A0, b"")]))
                    QTimer.singleShot(4500, self._check_for_silent_ble)
                elif kind == "status":
                    self._set_status(value)
                elif kind == "error":
                    self._set_status(value)
                    self._append_log(value)
                elif kind == "gatt":
                    self._append_log("GATT inventory:\n" + "\n".join(value))
                elif kind in ("tx", "bytes"):
                    self._append_log(f"{'TX' if kind == 'tx' else 'RX'} "
                                     f"{value.hex(' ').upper()}")
                    if kind == "bytes":
                        self.rx_seen = True
                        self._consume(value)
        except queue.Empty:
            pass

    def _check_for_silent_ble(self) -> None:
        if not self.rx_seen and self.status_label.text().startswith("Connected"):
            self._set_status("Connected, but no notifications — see protocol log")
            self._append_log("No FFF1 radar data received. Check the GATT inventory "
                             "for FFF0/FFF1/FFF2 and power-cycle the radar.")

    def _consume(self, data: bytes) -> None:
        for frame in self.stream.feed(data):
            try:
                if frame.startswith(REPORT_HEADER):
                    self._show_report(parse_report(frame))
                elif frame.startswith(COMMAND_HEADER):
                    self._show_ack(parse_ack(frame))
            except ValueError as exc:
                self._append_log(f"Parser: {exc}")

    def _show_report(self, report: Report) -> None:
        self.last_report = report
        self.radar.set_data(report, self.range_spin.value())
        if report.targets:
            self.live_label.setText("\n".join(
                f"T{i}: {t.distance_m} m, {t.speed_kmh} km/h, {t.angle_deg:+d}°, SNR {t.snr}"
                for i, t in enumerate(report.targets, 1)))
        else:
            self.live_label.setText("No targets")
        if self.csv_writer:
            stamp = datetime.now().isoformat(timespec="milliseconds")
            for i, target in enumerate(report.targets, 1):
                self.csv_writer.writerow((stamp, i, target.angle_deg,
                    target.distance_m, "approach" if target.approaching else "away",
                    target.speed_kmh, target.snr, int(report.alarm)))
            self.csv_file.flush()

    def _show_ack(self, ack) -> None:
        if ack.status:
            self._append_log(f"ACK 0x{ack.command:04X}: failed ({ack.status})")
            return
        self._append_log(f"ACK 0x{ack.command:04X}: OK, "
                         f"payload {ack.payload.hex(' ').upper()}")
        if ack.command == 0x12 and len(ack.payload) >= 4:
            distance, direction, speed, hold = ack.payload[:4]
            self.range_spin.setValue(distance)
            self.direction_box.setCurrentIndex(min(direction, 2))
            self.speed_spin.setValue(speed)
            self.hold_spin.setValue(hold)
            self.radar.set_data(self.last_report, distance)
        elif ack.command == 0x13 and len(ack.payload) >= 2:
            self.trigger_spin.setValue(ack.payload[0])
            self.snr_spin.setValue(ack.payload[1])
        elif ack.command == 0xA0 and len(ack.payload) >= 8:
            radar_type = int.from_bytes(ack.payload[:2], "little")
            major, minor = ack.payload[2:4]
            build = "".join(f"{byte:02X}" for byte in reversed(ack.payload[4:8]))
            self.firmware_label.setText(
                f"Firmware: 0x{radar_type:04X} V{major}.{minor:02d}.{build}")

    def _demo_tick(self) -> None:
        elapsed = time.monotonic() - self.demo_started
        targets = [Target(int(8 * math.sin(elapsed / 3)),
                          max(8, int(78 - elapsed * 2) % 75), True, 46, 21)]
        if int(elapsed) % 9 < 6:
            targets.append(Target(-14, 54, True, 82, 15))
        if int(elapsed) % 13 < 5:
            targets.append(Target(17, 35, False, 24, 11))
        self._show_report(Report(True, tuple(targets)))

    def _toggle_log(self) -> None:
        if self.csv_file:
            self.csv_file.close()
            self.csv_file = self.csv_writer = None
            self.log_button.setText("Start CSV log")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Save target log", "ld2451_targets.csv", "CSV files (*.csv)")
        if path:
            self.csv_file = open(path, "w", newline="", encoding="utf-8")
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(("timestamp", "target", "angle_deg",
                "distance_m", "direction", "speed_kmh", "snr", "alarm"))
            self.log_button.setText("Stop CSV log")

    def _append_log(self, text: str) -> None:
        self.raw_text.appendPlainText(text)

    def _set_status(self, text: str) -> None:
        self.status_label.setText(text)

    def closeEvent(self, event) -> None:
        if self.csv_file:
            self.csv_file.close()
        self.worker.submit(self.worker.disconnect())
        event.accept()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", action="store_true",
                        help="show simulated targets without a radar")
    args = parser.parse_args()
    QCoreApplication.addLibraryPath(str(_QT_PLUGIN_ROOT))
    qt_app = QApplication(sys.argv)
    window = App(demo=args.demo)
    window.show()
    raise SystemExit(qt_app.exec())


if __name__ == "__main__":
    main()
