import CoreBluetooth
import CryptoKit
import Foundation

@MainActor
final class BLECentral: NSObject, ObservableObject {
    nonisolated static let serviceUUID = CBUUID(string: "8E400001-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let locationUUID = CBUUID(string: "8E400002-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let statusUUID = CBUUID(string: "8E400003-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let rideControlUUID = CBUUID(string: "8E400004-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let rideHistoryUUID = CBUUID(string: "8E400005-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let deviceNameUUID = CBUUID(string: "8E400006-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let otaServiceUUID = CBUUID(string: "8E400010-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let otaControlUUID = CBUUID(string: "8E400011-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let otaDataUUID = CBUUID(string: "8E400012-F315-4F60-9FB8-838830DAEA50")
    nonisolated static let otaStatusUUID = CBUUID(string: "8E400013-F315-4F60-9FB8-838830DAEA50")

    struct BikeDevice: Identifiable, Equatable {
        let id: UUID
        let name: String
        let systemName: String
        let rssi: Int
        let isConnected: Bool
    }

    enum State: Equatable { case bluetoothOff, searching, connecting, connected, disconnected }
    enum RideSyncState: Equatable {
        case unavailable, checking, syncing(completed: Int, total: Int), upToDate, failed
    }
    enum FirmwareUpdateState: Equatable {
        case unavailable
        case checking
        case ready(version: String)
        case preparing(total: Int)
        case uploading(sent: Int, total: Int)
        case verifying
        case rebooting
        case failed(String)

        var isActive: Bool {
            switch self {
            case .preparing, .uploading, .verifying, .rebooting: true
            default: false
            }
        }
    }
    @Published private(set) var state: State = .disconnected
    @Published private(set) var deviceName = "PedalOne"
    @Published private(set) var bikeStatus: String?
    @Published private(set) var rideSyncState: RideSyncState = .unavailable
    @Published private(set) var devices: [BikeDevice] = []
    @Published private(set) var connectedDeviceID: UUID?
    @Published private(set) var renameError: String?
    @Published private(set) var firmwareUpdateState: FirmwareUpdateState = .unavailable
    @Published private(set) var firmwareVersion: String?
    @Published private(set) var connectionStage = "Disconnected"
    @Published private(set) var connectionError: String?
    @Published private(set) var locationWriteError: String?

    var onLocationWriteAccepted: (() -> Void)?
    var onLocationWriteFailed: ((String) -> Void)?

    var firmwareVersionText: String {
        if let firmwareVersion { return firmwareVersion }
        switch firmwareUpdateState {
        case .checking:
            return "Checking…"
        case .preparing, .uploading, .verifying:
            return "Updating…"
        case .rebooting:
            return "Restarting…"
        case let .ready(version):
            return version
        case .unavailable, .failed:
            return "Unavailable"
        }
    }

    var gpsWriteStatus: String {
        if firmwareUpdateState.isActive { return "Firmware update in progress" }
        guard let peripheral, peripheral.state == .connected else {
            return "Waiting for PedalOne"
        }
        guard let characteristic = locationCharacteristic else {
            return "GPS characteristic unavailable"
        }
        if let locationWriteError { return "BLE write failed: \(locationWriteError)" }
        if characteristic.properties.contains(.write) { return "Ready" }
        if characteristic.properties.contains(.writeWithoutResponse) {
            return peripheral.canSendWriteWithoutResponse ? "Ready" : "Bluetooth busy"
        }
        return "GPS characteristic is not writable"
    }

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var locationCharacteristic: CBCharacteristic?
    private var rideControlCharacteristic: CBCharacteristic?
    private var rideHistoryCharacteristic: CBCharacteristic?
    private var deviceNameCharacteristic: CBCharacteristic?
    private var otaControlCharacteristic: CBCharacteristic?
    private var otaDataCharacteristic: CBCharacteristic?
    private var otaStatusCharacteristic: CBCharacteristic?
    private var discoveredPeripherals: [UUID: CBPeripheral] = [:]
    private var discoveredRSSI: [UUID: Int] = [:]
    private var firmwareNames: [UUID: String] = [:]
    private var desiredPeripheralID: UUID?
    private var pendingPreviousName: String?
    private var latestPendingPacket: Data?
    private var hasRide: ((UUID) -> Bool)?
    private var receiveRide: ((SavedRide) -> Void)?
    private var pendingRideSummaries: [RideSyncSummary] = []
    private var currentRideSummary: RideSyncSummary?
    private var partialRideLogs: [UUID: Data] = [:]
    private var completedRideCount = 0
    private var totalRideCount = 0
    private var otaFirmware: Data?
    // Bytes queued into Core Bluetooth and bytes durably acknowledged by the
    // PedalOne are tracked separately. At most one 8 KiB credit window is in
    // flight, so write-without-response cannot overrun the receiver.
    private let otaCreditWindow = 8 * 1024
    private var otaOffset = 0
    private var otaAcknowledgedOffset = 0
    private var otaPendingChunkLength = 0
    private var otaCancelRequested = false
    private var otaDiscoveryRequested = false
    private var discoveryAttempt = 0
    private var discoveryRetryTask: Task<Void, Never>?
    private var firmwareInfoTimeoutTask: Task<Void, Never>?
    private var retrievalAttemptedIDs = Set<UUID>()
    private let defaults = UserDefaults.standard

    private var storedNames: [String: String] {
        get { defaults.dictionary(forKey: "pedalOne.deviceNames") as? [String: String] ?? [:] }
        set { defaults.set(newValue, forKey: "pedalOne.deviceNames") }
    }

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil,
            options: [CBCentralManagerOptionRestoreIdentifierKey: "com.byobike.BikeRelay.central"])
    }

    func startScanning() {
        guard central.state == .poweredOn else { return }

        // When ANCS notification sharing is enabled, iOS may already own the
        // physical connection and PedalOne stops advertising. Recover that
        // connection before scanning; the GPS discovery path below remains the
        // original Bike Relay implementation.
        let systemConnected = central.retrieveConnectedPeripherals(withServices: [Self.serviceUUID])
        if let target = systemConnected.first(where: {
            desiredPeripheralID == nil || $0.identifier == desiredPeripheralID
        }) {
            connect(target)
            return
        }

        var knownIDs: [UUID] = []
        if let value = defaults.string(forKey: "pedalOne.lastPeripheralID"),
           let id = UUID(uuidString: value) {
            knownIDs.append(id)
        }
        knownIDs.append(contentsOf: storedNames.keys.compactMap(UUID.init(uuidString:)))
        let untriedIDs = Array(Set(knownIDs)).filter { !retrievalAttemptedIDs.contains($0) }
        if !untriedIDs.isEmpty,
           let target = central.retrievePeripherals(withIdentifiers: untriedIDs).first {
            retrievalAttemptedIDs.insert(target.identifier)
            connect(target)
            return
        }

        state = .searching
        connectionStage = "Scanning"
        central.scanForPeripherals(withServices: [Self.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func selectDevice(id: UUID) {
        guard let target = discoveredPeripherals[id] else { return }
        desiredPeripheralID = id
        if peripheral?.identifier == id {
            connect(target)
            return
        }
        if let peripheral, peripheral.state == .connected || peripheral.state == .connecting {
            state = .connecting
            central.cancelPeripheralConnection(peripheral)
        } else {
            connect(target)
        }
    }

    func renameConnectedDevice(to proposedName: String) {
        guard let peripheral, peripheral.state == .connected else {
            renameError = "Connect to a PedalOne before renaming it."
            return
        }
        guard let name = Self.validatedDeviceName(proposedName) else {
            renameError = "Use 1–24 UTF-8 bytes without control characters."
            return
        }

        let id = peripheral.identifier
        pendingPreviousName = displayName(for: id, fallback: peripheral.name)
        store(name: name, for: id)
        firmwareNames[id] = name
        refreshDeviceList()
        renameError = nil

        guard let characteristic = deviceNameCharacteristic,
              characteristic.properties.contains(.write) else {
            // Older firmware still supports a persistent nickname on this phone.
            pendingPreviousName = nil
            return
        }
        peripheral.writeValue(Data(name.utf8), for: characteristic, type: .withResponse)
    }

    func clearRenameError() { renameError = nil }

    func refreshFirmwareInfo() {
        guard let peripheral, peripheral.state == .connected,
              let status = otaStatusCharacteristic else {
            firmwareUpdateState = .unavailable
            return
        }

        firmwareUpdateState = .checking
        if status.properties.contains(.read) {
            peripheral.readValue(for: status)
        }
        if status.isNotifying, otaControlCharacteristic != nil {
            writeOTAControl("INFO")
        }
        scheduleFirmwareInfoTimeout()
    }

    private func scheduleFirmwareInfoTimeout() {
        firmwareInfoTimeoutTask?.cancel()
        firmwareInfoTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(3))
            guard !Task.isCancelled, let self,
                  self.firmwareVersion == nil,
                  self.firmwareUpdateState == .checking else { return }
            self.firmwareUpdateState = .unavailable
        }
    }

    func startFirmwareUpdate(_ firmware: Data) {
        guard let peripheral, peripheral.state == .connected,
              otaControlCharacteristic != nil, otaDataCharacteristic != nil,
              otaStatusCharacteristic?.isNotifying == true else {
            firmwareUpdateState = .failed("This PedalOne does not support iPhone firmware updates.")
            return
        }
        guard firmware.count >= 32, firmware.first == 0xE9 else {
            firmwareUpdateState = .failed("Choose an ESP32 application .bin file, not a merged flash image.")
            return
        }
        guard firmware.count <= 3 * 1024 * 1024 else {
            firmwareUpdateState = .failed("The firmware image is larger than the PedalOne OTA slot.")
            return
        }
        guard !firmwareUpdateState.isActive else { return }

        otaFirmware = firmware
        otaOffset = 0
        otaAcknowledgedOffset = 0
        otaPendingChunkLength = 0
        otaCancelRequested = false
        let digest = Insecure.MD5.hash(data: firmware).map { String(format: "%02x", $0) }.joined()
        firmwareUpdateState = .preparing(total: firmware.count)
        writeOTAControl("BEGIN \(firmware.count) \(digest)")
    }

    func cancelFirmwareUpdate() {
        guard firmwareUpdateState.isActive, firmwareUpdateState != .rebooting else { return }
        otaCancelRequested = true
        writeOTAControl("ABORT")
    }

    func configureRideSync(
        hasRide: @escaping (UUID) -> Bool,
        receiveRide: @escaping (SavedRide) -> Void
    ) {
        self.hasRide = hasRide
        self.receiveRide = receiveRide
    }

    func send(_ data: Data) -> Bool {
        guard !firmwareUpdateState.isActive else {
            latestPendingPacket = data
            return false
        }
        guard let peripheral, let characteristic = locationCharacteristic,
              peripheral.state == .connected else {
            latestPendingPacket = data
            return false
        }

        guard peripheral.canSendWriteWithoutResponse else {
            latestPendingPacket = data
            return false
        }
        latestPendingPacket = nil
        locationWriteError = nil
        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
        onLocationWriteAccepted?()
        return true
    }

    private static func validatedDeviceName(_ value: String) -> String? {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed.utf8.count <= 24,
              trimmed.unicodeScalars.allSatisfy({ !CharacterSet.controlCharacters.contains($0) }) else { return nil }
        return trimmed
    }

    private func displayName(for id: UUID, fallback: String?) -> String {
        storedNames[id.uuidString] ?? firmwareNames[id] ?? fallback ?? "PedalOne"
    }

    private func store(name: String, for id: UUID) {
        var names = storedNames
        names[id.uuidString] = name
        storedNames = names
    }

    private func refreshDeviceList() {
        devices = discoveredPeripherals.values.map { peripheral in
            BikeDevice(
                id: peripheral.identifier,
                name: displayName(for: peripheral.identifier, fallback: peripheral.name),
                systemName: peripheral.name ?? "PedalOne",
                rssi: discoveredRSSI[peripheral.identifier] ?? 0,
                isConnected: peripheral.identifier == connectedDeviceID
            )
        }.sorted {
            if $0.isConnected != $1.isConnected { return $0.isConnected }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
        if let id = connectedDeviceID {
            deviceName = displayName(for: id, fallback: peripheral?.name)
        }
    }

    private func connect(_ peripheral: CBPeripheral) {
        if self.peripheral?.identifier != peripheral.identifier {
            firmwareVersion = nil
        }
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        desiredPeripheralID = peripheral.identifier
        defaults.set(peripheral.identifier.uuidString, forKey: "pedalOne.lastPeripheralID")
        discoveredPeripherals[peripheral.identifier] = peripheral
        deviceName = displayName(for: peripheral.identifier, fallback: peripheral.name)
        refreshDeviceList()
        state = .connecting
        connectionStage = "Connecting"
        connectionError = nil
        if peripheral.state == .connected {
            discoveryAttempt = 0
            beginServiceDiscovery(on: peripheral)
        } else if peripheral.state != .connecting {
            central.connect(peripheral)
        }
    }

    private func beginServiceDiscovery(on peripheral: CBPeripheral) {
        guard self.peripheral?.identifier == peripheral.identifier,
              peripheral.state == .connected else { return }
        discoveryRetryTask?.cancel()
        discoveryAttempt = 1
        otaDiscoveryRequested = false
        otaControlCharacteristic = nil
        otaDataCharacteristic = nil
        otaStatusCharacteristic = nil
        firmwareUpdateState = .unavailable
        connectionStage = "Discovering GPS service"
        connectionError = nil
        peripheral.discoverServices([Self.serviceUUID])
    }

    private func beginOptionalOTADiscovery(on peripheral: CBPeripheral) {
        guard self.peripheral?.identifier == peripheral.identifier,
              peripheral.state == .connected,
              !otaDiscoveryRequested else { return }
        otaDiscoveryRequested = true
        firmwareUpdateState = .checking
        peripheral.discoverServices([Self.otaServiceUUID])
        scheduleFirmwareInfoTimeout()
    }

    private func flushPending() {
        guard !firmwareUpdateState.isActive else { return }
        guard let packet = latestPendingPacket, send(packet) else { return }
        latestPendingPacket = nil
    }

    private func writeOTAControl(_ command: String) {
        guard let peripheral, let characteristic = otaControlCharacteristic,
              peripheral.state == .connected else {
            failFirmwareUpdate("The PedalOne disconnected before the update command was sent.")
            return
        }
        peripheral.writeValue(Data(command.utf8), for: characteristic, type: .withResponse)
    }

    private func sendFirmwareWindow() {
        guard !otaCancelRequested, let firmware = otaFirmware,
              let peripheral, let characteristic = otaDataCharacteristic,
              peripheral.state == .connected else { return }
        if characteristic.properties.contains(.writeWithoutResponse) {
            if otaAcknowledgedOffset >= firmware.count {
                firmwareUpdateState = .verifying
                writeOTAControl("END")
                return
            }
            let maximum = max(20, peripheral.maximumWriteValueLength(for: .withoutResponse))
            while otaOffset < firmware.count,
                  otaOffset - otaAcknowledgedOffset < otaCreditWindow,
                  peripheral.canSendWriteWithoutResponse {
                let availableCredit = otaCreditWindow - (otaOffset - otaAcknowledgedOffset)
                let end = min(otaOffset + min(maximum, availableCredit), firmware.count)
                peripheral.writeValue(firmware.subdata(in: otaOffset..<end),
                                      for: characteristic, type: .withoutResponse)
                otaOffset = end
            }
            return
        }

        // One-time compatibility path: firmware through 2.1.13 exposes only
        // acknowledged writes. It can install 2.1.14, after which subsequent
        // updates automatically use the faster credit-window transport.
        guard characteristic.properties.contains(.write) else {
            failFirmwareUpdate("The PedalOne OTA data characteristic is not writable.")
            return
        }
        guard otaPendingChunkLength == 0 else { return }
        if otaOffset >= firmware.count {
            firmwareUpdateState = .verifying
            writeOTAControl("END")
            return
        }
        let maximum = max(20, peripheral.maximumWriteValueLength(for: .withResponse))
        let end = min(otaOffset + maximum, firmware.count)
        let chunk = firmware.subdata(in: otaOffset..<end)
        otaPendingChunkLength = chunk.count
        peripheral.writeValue(chunk, for: characteristic, type: .withResponse)
    }

    private func handleFirmwareStatus(_ status: String) {
        firmwareInfoTimeoutTask?.cancel()
        let fields = status.split(separator: " ", omittingEmptySubsequences: true)
        guard let command = fields.first else { return }
        switch command {
        case "IDLE":
            if fields.count > 1 {
                firmwareVersion = String(fields[1])
            }
            clearFirmwareTransfer()
            firmwareUpdateState = .ready(version: firmwareVersion ?? "Unknown")
            flushPending()
        case "BUSY":
            if fields.count > 1 { firmwareVersion = String(fields[1]) }
            failFirmwareUpdate("The PedalOne is already processing a firmware update. Reconnect and try again.")
        case "READY":
            guard let firmware = otaFirmware else {
                failFirmwareUpdate("The PedalOne became ready, but no firmware image is loaded.")
                return
            }
            firmwareUpdateState = .uploading(sent: otaOffset, total: firmware.count)
            sendFirmwareWindow()
        case "PROGRESS":
            guard let firmware = otaFirmware else { return }
            guard fields.count > 1, let acknowledged = Int(fields[1]),
                  acknowledged >= otaAcknowledgedOffset,
                  acknowledged <= otaOffset else {
                failFirmwareUpdate("The PedalOne returned an invalid OTA acknowledgement.")
                return
            }
            otaAcknowledgedOffset = acknowledged
            firmwareUpdateState = .uploading(sent: acknowledged, total: firmware.count)
            sendFirmwareWindow()
        case "OK":
            clearFirmwareTransfer()
            firmwareUpdateState = .rebooting
        case "ABORTED":
            clearFirmwareTransfer()
            firmwareUpdateState = .ready(version: firmwareVersion ?? "Unknown")
            flushPending()
        case "ERROR":
            let code = fields.dropFirst().joined(separator: " ")
            failFirmwareUpdate(Self.firmwareErrorMessage(for: code))
        default:
            break
        }
    }

    private func failFirmwareUpdate(_ message: String) {
        clearFirmwareTransfer()
        firmwareUpdateState = .failed(message)
        flushPending()
    }

    private func handleLegacyFirmwareDataAcknowledgement(error: Error?) {
        if let error {
            failFirmwareUpdate("Firmware transfer failed: \(error.localizedDescription)")
            return
        }
        guard let firmware = otaFirmware, otaPendingChunkLength > 0 else { return }
        otaOffset += otaPendingChunkLength
        otaAcknowledgedOffset = otaOffset
        otaPendingChunkLength = 0
        firmwareUpdateState = .uploading(sent: otaOffset, total: firmware.count)
        sendFirmwareWindow()
    }

    private func clearFirmwareTransfer() {
        otaFirmware = nil
        otaOffset = 0
        otaAcknowledgedOffset = 0
        otaPendingChunkLength = 0
        otaCancelRequested = false
    }

    private static func firmwareErrorMessage(for code: String) -> String {
        switch code {
        case "NOT_SAFE": "Stop the active ride and charge the PedalOne above 25% before updating."
        case "NO_OTA_SPACE": "This firmware was not installed with the required OTA partition layout. Update once over USB first."
        case "BAD_MD5", "SIZE_MISMATCH", "VERIFY_FAILED": "The firmware image did not pass device verification."
        case "TOO_MUCH_DATA", "FLASH_WRITE": "The PedalOne could not write the firmware image."
        case "DISCONNECTED": "The Bluetooth connection was lost during the update."
        case "BUSY": "The PedalOne is busy with another update."
        default: "Firmware update failed (\(code))."
        }
    }

    private func beginRideHistorySync() {
        guard rideControlCharacteristic != nil, rideHistoryCharacteristic?.isNotifying == true else {
            rideSyncState = .unavailable
            return
        }
        pendingRideSummaries.removeAll(keepingCapacity: true)
        currentRideSummary = nil
        completedRideCount = 0
        totalRideCount = 0
        rideSyncState = .checking
        writeRideCommand(RideSyncProtocol.requestIndex())
    }

    private func writeRideCommand(_ data: Data) {
        guard !firmwareUpdateState.isActive else { return }
        guard let peripheral, let characteristic = rideControlCharacteristic,
              peripheral.state == .connected else { return }
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }

    private func handleRideHistoryEvent(_ data: Data) {
        guard data.count >= 2, data[0] == RideSyncProtocol.version,
              let event = RideSyncProtocol.Event(rawValue: data[1]) else {
            rideSyncState = .failed
            return
        }
        switch event {
        case .indexBegin:
            pendingRideSummaries.removeAll(keepingCapacity: true)
            currentRideSummary = nil
            completedRideCount = 0
            totalRideCount = 0
            rideSyncState = .checking
        case .rideSummary:
            guard let summary = RideSyncSummary(data: data) else {
                rideSyncState = .failed
                return
            }
            if hasRide?(summary.id) == true {
                partialRideLogs.removeValue(forKey: summary.id)
            } else {
                pendingRideSummaries.append(summary)
            }
        case .indexEnd:
            totalRideCount = pendingRideSummaries.count
            if totalRideCount == 0 {
                rideSyncState = .upToDate
            } else {
                rideSyncState = .syncing(completed: 0, total: totalRideCount)
                requestNextRide()
            }
        case .rideChunk:
            handleRideChunk(data)
        case .rideEnd:
            handleRideEnd(data)
        case .error:
            rideSyncState = .failed
        }
    }

    private func requestNextRide() {
        guard currentRideSummary == nil else { return }
        guard !pendingRideSummaries.isEmpty else {
            rideSyncState = .upToDate
            return
        }
        let summary = pendingRideSummaries.removeFirst()
        currentRideSummary = summary
        var partial = partialRideLogs[summary.id] ?? Data()
        if partial.count > Int(summary.logByteCount) {
            partial.removeAll(keepingCapacity: false)
            partialRideLogs[summary.id] = partial
        }
        rideSyncState = .syncing(completed: completedRideCount, total: totalRideCount)
        writeRideCommand(RideSyncProtocol.requestRide(
            id: summary.id,
            offset: UInt32(clamping: partial.count)
        ))
    }

    private func handleRideChunk(_ data: Data) {
        guard let chunk = RideSyncChunk(data: data),
              let summary = currentRideSummary, chunk.id == summary.id else {
            rideSyncState = .failed
            return
        }
        var partial = partialRideLogs[chunk.id] ?? Data()
        let expectedOffset = UInt32(clamping: partial.count)
        if chunk.offset == expectedOffset {
            partial.append(chunk.payload)
            guard partial.count <= Int(summary.logByteCount) else {
                partialRideLogs.removeValue(forKey: chunk.id)
                currentRideSummary = nil
                rideSyncState = .failed
                return
            }
            partialRideLogs[chunk.id] = partial
        } else if chunk.offset > expectedOffset {
            // Ask the peripheral to resume at the first missing byte.
            writeRideCommand(RideSyncProtocol.requestRide(id: chunk.id, offset: expectedOffset))
            return
        }
        writeRideCommand(RideSyncProtocol.acknowledge(
            id: chunk.id,
            nextOffset: UInt32(clamping: partial.count)
        ))
    }

    private func handleRideEnd(_ data: Data) {
        guard let end = RideSyncEnd(data: data),
              let summary = currentRideSummary, end.id == summary.id,
              end.byteCount == summary.logByteCount,
              end.crc32 == summary.logCRC32,
              let log = partialRideLogs[end.id],
              let ride = summary.decodeRide(log: log) else {
            if let id = currentRideSummary?.id { partialRideLogs.removeValue(forKey: id) }
            currentRideSummary = nil
            rideSyncState = .failed
            return
        }
        receiveRide?(ride)
        partialRideLogs.removeValue(forKey: end.id)
        currentRideSummary = nil
        completedRideCount += 1
        rideSyncState = .syncing(completed: completedRideCount, total: totalRideCount)
        requestNextRide()
    }
}

extension BLECentral: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in central.state == .poweredOn ? startScanning() : (state = .bluetoothOff) }
    }
    nonisolated func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        let restored = (dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral])?.first
        Task { @MainActor in if let restored { connect(restored) } }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        Task { @MainActor in
            discoveredPeripherals[peripheral.identifier] = peripheral
            discoveredRSSI[peripheral.identifier] = RSSI.intValue
            refreshDeviceList()
            if desiredPeripheralID == peripheral.identifier || desiredPeripheralID == nil {
                connect(peripheral)
            }
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            connectedDeviceID = peripheral.identifier
            discoveredPeripherals[peripheral.identifier] = peripheral
            deviceName = displayName(for: peripheral.identifier, fallback: peripheral.name)
            refreshDeviceList()
            discoveryAttempt = 0
            beginServiceDiscovery(on: peripheral)
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            state = .disconnected
            connectionStage = "Connection failed"
            connectionError = error?.localizedDescription ?? "PedalOne rejected the Bluetooth connection."
            startScanning()
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                                    timestamp: CFAbsoluteTime, isReconnecting: Bool, error: Error?) {
        Task { @MainActor in
            discoveryRetryTask?.cancel()
            firmwareInfoTimeoutTask?.cancel()
            state = .disconnected
            connectionStage = "Disconnected"
            connectionError = error?.localizedDescription
            connectedDeviceID = nil
            locationWriteError = nil
            locationCharacteristic = nil
            rideControlCharacteristic = nil
            rideHistoryCharacteristic = nil
            deviceNameCharacteristic = nil
            otaControlCharacteristic = nil
            otaDataCharacteristic = nil
            otaStatusCharacteristic = nil
            otaDiscoveryRequested = false
            rideSyncState = .unavailable
            if firmwareUpdateState.isActive, firmwareUpdateState != .rebooting {
                failFirmwareUpdate("The Bluetooth connection was lost during the firmware update.")
            } else if firmwareUpdateState != .rebooting {
                firmwareUpdateState = .unavailable
            }
            refreshDeviceList()
            if desiredPeripheralID == peripheral.identifier {
                connect(peripheral)
            } else if let desiredPeripheralID,
                      let desired = discoveredPeripherals[desiredPeripheralID] {
                connect(desired)
            } else {
                startScanning()
            }
        }
    }
}

extension BLECentral: CBPeripheralDelegate {
    nonisolated func peripheralDidUpdateName(_ peripheral: CBPeripheral) {
        Task { @MainActor in refreshDeviceList() }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            guard error == nil else {
                if otaDiscoveryRequested, locationCharacteristic != nil {
                    firmwareUpdateState = .unavailable
                } else {
                    connectionStage = "GPS service unavailable"
                    connectionError = error?.localizedDescription ?? "PedalOne did not return its GPS data service."
                }
                return
            }

            if !otaDiscoveryRequested {
                guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
                    connectionStage = "GPS service unavailable"
                    connectionError = "PedalOne did not return its GPS data service."
                    return
                }
                // Preserve the proven Bike Relay recovery path. GPS, ride status,
                // ride history and the device name are fully ready before the
                // optional OTA service adds any more GATT traffic.
                peripheral.discoverCharacteristics([
                    Self.locationUUID, Self.statusUUID, Self.rideControlUUID,
                    Self.rideHistoryUUID, Self.deviceNameUUID
                ], for: service)
                return
            }

            guard let otaService = peripheral.services?.first(where: { $0.uuid == Self.otaServiceUUID }) else {
                firmwareUpdateState = .unavailable
                return
            }
            peripheral.discoverCharacteristics([
                Self.otaControlUUID, Self.otaDataUUID, Self.otaStatusUUID
            ], for: otaService)
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            if let error {
                if service.uuid == Self.otaServiceUUID {
                    firmwareUpdateState = .unavailable
                } else {
                    connectionStage = "Characteristic discovery delayed"
                    connectionError = error.localizedDescription
                }
                return
            }
            print("PedalOne BLE characteristics \(service.uuid.uuidString): \((service.characteristics ?? []).map { $0.uuid.uuidString }.joined(separator: ", "))")
            if service.uuid == Self.otaServiceUUID {
                for characteristic in service.characteristics ?? [] {
                    if characteristic.uuid == Self.otaControlUUID { otaControlCharacteristic = characteristic }
                    if characteristic.uuid == Self.otaDataUUID { otaDataCharacteristic = characteristic }
                    if characteristic.uuid == Self.otaStatusUUID {
                        otaStatusCharacteristic = characteristic
                        firmwareUpdateState = .checking
                        peripheral.setNotifyValue(true, for: characteristic)
                        peripheral.readValue(for: characteristic)
                        scheduleFirmwareInfoTimeout()
                    }
                }
                if otaControlCharacteristic == nil || otaDataCharacteristic == nil || otaStatusCharacteristic == nil {
                    firmwareUpdateState = .unavailable
                }
                return
            }
            for characteristic in service.characteristics ?? [] {
                if characteristic.uuid == Self.locationUUID { locationCharacteristic = characteristic }
                if characteristic.uuid == Self.rideControlUUID { rideControlCharacteristic = characteristic }
                if characteristic.uuid == Self.deviceNameUUID {
                    deviceNameCharacteristic = characteristic
                    peripheral.readValue(for: characteristic)
                    if characteristic.properties.contains(.notify) {
                        peripheral.setNotifyValue(true, for: characteristic)
                    }
                }
                if characteristic.uuid == Self.rideHistoryUUID {
                    rideHistoryCharacteristic = characteristic
                    peripheral.setNotifyValue(true, for: characteristic)
                }
                if characteristic.uuid == Self.statusUUID {
                    peripheral.setNotifyValue(true, for: characteristic)
                    // Recover the PedalOne's current ride state after a reconnect,
                    // even if its notification happened while the phone was away.
                    peripheral.readValue(for: characteristic)
                }
            }
            if rideControlCharacteristic == nil || rideHistoryCharacteristic == nil {
                rideSyncState = .unavailable
            }
            if locationCharacteristic != nil {
                discoveryRetryTask?.cancel()
                connectionStage = "Ready"
                connectionError = nil
                state = .connected
                flushPending()
                beginOptionalOTADiscovery(on: peripheral)
            } else {
                connectionStage = "GPS characteristic missing"
                connectionError = "The connected firmware did not expose the PedalOne GPS characteristic."
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        Task { @MainActor in
            guard self.peripheral?.identifier == peripheral.identifier else { return }
            locationCharacteristic = nil
            otaDiscoveryRequested = false
            otaControlCharacteristic = nil
            otaDataCharacteristic = nil
            otaStatusCharacteristic = nil
            state = .connecting
            discoveryAttempt = 0
            beginServiceDiscovery(on: peripheral)
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let data = characteristic.value else { return }
        Task { @MainActor in
            if characteristic.uuid == Self.statusUUID,
               let value = String(data: data, encoding: .utf8) {
                bikeStatus = value
            } else if characteristic.uuid == Self.deviceNameUUID,
                      let value = String(data: data, encoding: .utf8),
                      let name = Self.validatedDeviceName(value) {
                let savedName = storedNames[peripheral.identifier.uuidString]
                let isFactoryName = name == "PedalOne" || name == "RAC9000" || name == "Waveshare Bike"
                if isFactoryName, let savedName, savedName != name,
                   characteristic.properties.contains(.write) {
                    // Migrate an app-only nickname to newly updated firmware.
                    firmwareNames[peripheral.identifier] = savedName
                    refreshDeviceList()
                    peripheral.writeValue(Data(savedName.utf8), for: characteristic, type: .withResponse)
                } else {
                    // A custom name already stored by the device is authoritative,
                    // including changes made from another phone.
                    firmwareNames[peripheral.identifier] = name
                    store(name: name, for: peripheral.identifier)
                    pendingPreviousName = nil
                    refreshDeviceList()
                }
            } else if characteristic.uuid == Self.rideHistoryUUID {
                handleRideHistoryEvent(data)
            } else if characteristic.uuid == Self.otaStatusUUID,
                      let status = String(data: data, encoding: .utf8) {
                handleFirmwareStatus(status)
            }
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        Task { @MainActor in
            if characteristic.uuid == Self.locationUUID {
                locationWriteError = error?.localizedDescription
                if let locationWriteError {
                    onLocationWriteFailed?(locationWriteError)
                } else {
                    onLocationWriteAccepted?()
                }
                return
            }
            if characteristic.uuid == Self.otaControlUUID {
                if let error {
                    failFirmwareUpdate("Firmware command failed: \(error.localizedDescription)")
                }
                return
            }
            if characteristic.uuid == Self.otaDataUUID {
                handleLegacyFirmwareDataAcknowledgement(error: error)
                return
            }
            guard characteristic.uuid == Self.deviceNameUUID else { return }
            if let error {
                if let previous = pendingPreviousName {
                    store(name: previous, for: peripheral.identifier)
                    firmwareNames[peripheral.identifier] = previous
                }
                pendingPreviousName = nil
                renameError = "PedalOne could not save the name: \(error.localizedDescription)"
                refreshDeviceList()
            } else {
                pendingPreviousName = nil
                peripheral.readValue(for: characteristic)
            }
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateNotificationStateFor characteristic: CBCharacteristic,
                                error: Error?) {
        Task { @MainActor in
            if characteristic.uuid == Self.rideHistoryUUID {
                if error == nil && characteristic.isNotifying {
                    beginRideHistorySync()
                } else {
                    rideSyncState = .failed
                }
            } else if characteristic.uuid == Self.otaStatusUUID {
                if error == nil && characteristic.isNotifying {
                    refreshFirmwareInfo()
                } else if !firmwareUpdateState.isActive {
                    firmwareUpdateState = .unavailable
                }
            }
        }
    }
    nonisolated func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        Task { @MainActor in
            if firmwareUpdateState.isActive, otaFirmware != nil {
                sendFirmwareWindow()
            } else {
                flushPending()
            }
        }
    }
}
