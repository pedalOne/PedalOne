import SwiftUI

struct ContentView: View {
    var body: some View {
        PedalOneRootView()
    }
}

private struct ConnectView: View {
    @EnvironmentObject private var relay: RelayController
    @State private var isRenamingDevice = false
    @State private var proposedDeviceName = ""
    @State private var isShowingFirmwareUpdate = false
    @State private var selectedDevice: BLECentral.BikeDevice?

    var body: some View {
        NavigationStack {
            VStack(spacing: 22) {
                Spacer()
                Image(systemName: relay.isRelaying ? "location.fill" : "bicycle")
                    .font(.system(size: 48, weight: .medium)).foregroundStyle(.white)
                    .frame(width: 116, height: 116).background(statusColor, in: Circle())
                Text(statusTitle).font(.title2.weight(.semibold))
                VStack(spacing: 0) {
                    metric("Device", relay.bluetooth.deviceName); Divider()
                    metric("Firmware", relay.bluetooth.firmwareVersionText); Divider()
                    metric("GPS", gpsText); Divider()
                    metric("Speed", speedText); Divider()
                    metric("Barometer", barometerText); Divider()
                    metric("Relay", relay.relayMessage); Divider()
                    metric("Last sent", relay.lastSent?.formatted(date: .omitted, time: .standard) ?? "Not started")
                }.padding(.horizontal).background(.background.secondary, in: RoundedRectangle(cornerRadius: 18))
                if let error = relay.location.errorMessage { Text(error).font(.footnote).foregroundStyle(.red) }
                if let error = relay.bluetooth.renameError { Text(error).font(.footnote).foregroundStyle(.red) }
                Button(relay.isRelaying ? "Stop Relay" : "Start GPS Relay") { relay.toggleRelay() }
                    .buttonStyle(.borderedProminent).tint(relay.isRelaying ? .red : .teal).controlSize(.large)
                Text("Location and barometric altitude continue while the phone is locked.")
                    .font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center)
                Spacer()
            }
            .padding()
            .navigationTitle("Connect")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Menu {
                        if !relay.bluetooth.devices.isEmpty {
                            Section("PedalOne Devices") {
                                ForEach(relay.bluetooth.devices) { device in
                                    Button {
                                        selectedDevice = device
                                        relay.bluetooth.selectDevice(id: device.id)
                                    } label: {
                                        Label(
                                            device.name,
                                            systemImage: device.isConnected ? "checkmark.circle.fill" : "bicycle"
                                        )
                                    }
                                }
                            }
                        }

                        if relay.bluetooth.state == .connected {
                            Button {
                                proposedDeviceName = relay.bluetooth.deviceName
                                relay.bluetooth.clearRenameError()
                                isRenamingDevice = true
                            } label: {
                                Label("Rename \(relay.bluetooth.deviceName)", systemImage: "pencil")
                            }
                        }

                        Button {
                            isShowingFirmwareUpdate = true
                        } label: {
                            Label("Firmware Update", systemImage: "arrow.triangle.2.circlepath")
                        }
                        .disabled(relay.bluetooth.state != .connected)

                        Button {
                            relay.bluetooth.startScanning()
                        } label: {
                            Label("Scan for Devices", systemImage: "arrow.clockwise")
                        }
                    } label: {
                        Image(systemName: "bicycle.circle")
                    }
                    .accessibilityLabel("PedalOne devices")
                }
            }
            .alert("Rename PedalOne", isPresented: $isRenamingDevice) {
                TextField("Device name", text: $proposedDeviceName)
                Button("Cancel", role: .cancel) {}
                Button("Save") {
                    relay.bluetooth.renameConnectedDevice(to: proposedDeviceName)
                }
            } message: {
                Text("The name is saved on this iPhone and, with updated firmware, on the PedalOne itself.")
            }
            .sheet(isPresented: $isShowingFirmwareUpdate) {
                FirmwareUpdateView()
            }
            .sheet(item: $selectedDevice) { device in
                DeviceStatusSheet(deviceID: device.id, fallbackName: device.name)
                    .presentationDetents([.medium])
            }
        }
    }

    private func metric(_ label: String, _ value: String) -> some View {
        HStack { Text(label).foregroundStyle(.secondary); Spacer(); Text(value).fontWeight(.medium) }.frame(minHeight: 49)
    }
    private var statusTitle: String {
        relay.bluetooth.state == .connected
            ? "\(relay.bluetooth.deviceName) connected"
            : "Looking for PedalOne"
    }
    private var statusColor: Color { relay.bluetooth.state == .connected ? .teal : .gray }
    private var gpsText: String {
        guard let value = relay.location.latestLocation?.horizontalAccuracy else { return "Waiting…" }
        return String(format: "%.0f m accuracy", value)
    }
    private var speedText: String {
        guard let value = relay.location.latestLocation?.speed, value >= 0 else { return "—" }
        return String(format: "%.1f mph", value * 2.236_936)
    }
    private var barometerText: String {
        guard relay.barometer.isAvailable else { return "Unavailable · GPS fallback" }
        guard let meters = relay.barometer.relativeAltitudeMeters else { return "Waiting…" }
        return String(format: "%+.1f ft relative", meters * 3.28084)
    }
}

private struct DeviceStatusSheet: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var relay: RelayController
    let deviceID: UUID
    let fallbackName: String

    private var device: BLECentral.BikeDevice? {
        relay.bluetooth.devices.first { $0.id == deviceID }
    }

    private var isReady: Bool {
        relay.bluetooth.connectedDeviceID == deviceID && relay.bluetooth.state == .connected
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Device Status") {
                    LabeledContent("Name", value: device?.name ?? fallbackName)
                    LabeledContent("FW Version", value: firmwareText)
                    LabeledContent("Mode", value: modeText)
                    LabeledContent("Connection", value: connectionText)
                    if let device {
                        LabeledContent("Signal", value: "\(device.rssi) dBm")
                    }
                }

                if !isReady, relay.bluetooth.connectionError == nil {
                    HStack(spacing: 10) {
                        ProgressView()
                        Text(relay.bluetooth.connectionStage)
                            .foregroundStyle(.secondary)
                    }
                }

                if let error = relay.bluetooth.connectionError {
                    Section("BLE Diagnostics") {
                        Label(error, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(.red)
                    }
                }
            }
            .navigationTitle("PedalOne")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
            .onChange(of: relay.bluetooth.connectedDeviceID) { _, connectedID in
                if connectedID == deviceID {
                    relay.bluetooth.refreshFirmwareInfo()
                }
            }
        }
    }

    private var firmwareText: String {
        guard relay.bluetooth.connectedDeviceID == deviceID else { return "Connecting…" }
        return relay.bluetooth.firmwareVersionText
    }

    private var connectionText: String {
        guard relay.bluetooth.connectedDeviceID == deviceID else { return "Discovered" }
        return relay.bluetooth.connectionStage
    }

    private var modeText: String {
        guard relay.bluetooth.connectedDeviceID == deviceID else { return "—" }
        guard let status = relay.bluetooth.bikeStatus, !status.isEmpty else { return "Checking…" }
        return status.prefix(1).uppercased() + status.dropFirst()
    }
}

private struct RideView: View {
    @EnvironmentObject private var rideStore: RideStore
    @EnvironmentObject private var relay: RelayController
    @State private var editMode: EditMode = .inactive
    @State private var selection: Set<UUID> = []
    @State private var showingDeleteConfirmation = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                RideHeader()
                    .padding(.horizontal)
                    .padding(.vertical, 10)
                    .background(.background.secondary)
                Divider()
                if let status = syncStatus {
                    HStack(spacing: 7) {
                        if isSyncActive { ProgressView().controlSize(.small) }
                        Image(systemName: syncStatusIcon)
                            .foregroundStyle(syncStatusColor)
                        Text(status).font(.footnote).foregroundStyle(.secondary)
                        Spacer()
                    }
                    .padding(.horizontal)
                    .padding(.vertical, 8)
                    Divider()
                }
                if rideStore.rides.isEmpty {
                    ContentUnavailableView(
                        "No Saved Rides",
                        systemImage: "bicycle",
                        description: Text("Complete a ride with your PedalOne to see it here.")
                    )
                } else {
                    List(selection: $selection) {
                        ForEach(rideStore.rides) { ride in
                            if editMode.isEditing {
                                RideRow(ride: ride)
                                    .tag(ride.id)
                            } else {
                                NavigationLink {
                                    RideDetailDashboard(ride: ride)
                                } label: {
                                    RideRow(ride: ride)
                                }
                            }
                        }
                        .onDelete(perform: delete)
                    }
                    .listStyle(.plain)
                    .environment(\.editMode, $editMode)
                }
            }
            .navigationTitle("Ride")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    if !rideStore.rides.isEmpty {
                        Button(editMode.isEditing ? "Done" : "Select") {
                            withAnimation {
                                if editMode.isEditing {
                                    editMode = .inactive
                                    selection.removeAll()
                                } else {
                                    editMode = .active
                                }
                            }
                        }
                    }
                }
            }
            .safeAreaInset(edge: .bottom, spacing: 0) {
                if editMode.isEditing {
                    VStack(spacing: 8) {
                        Button {
                            showingDeleteConfirmation = true
                        } label: {
                            HStack(spacing: 10) {
                                Image(systemName: "trash")
                                Text("Delete Selected")
                                if !selection.isEmpty {
                                    Text("\(selection.count)")
                                        .font(.caption.bold())
                                        .padding(.horizontal, 8)
                                        .padding(.vertical, 3)
                                        .background(.white.opacity(0.22), in: Capsule())
                                }
                            }
                            .font(.headline)
                            .frame(maxWidth: .infinity, minHeight: 32)
                        }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.large)
                        .tint(.red)
                        .disabled(selection.isEmpty)
                    }
                    .padding(.horizontal, 16)
                    .padding(.top, 10)
                    .padding(.bottom, 8)
                    .background(.bar)
                }
            }
            .confirmationDialog(
                "Delete Selected Rides?",
                isPresented: $showingDeleteConfirmation,
                titleVisibility: .visible
            ) {
                Button("Delete \(selection.count) Ride\(selection.count == 1 ? "" : "s")", role: .destructive) {
                    deleteSelection()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This removes the selected rides and their GPS logs from this iPhone.")
            }
        }
    }

    private func delete(at offsets: IndexSet) {
        let ids = Set(offsets.compactMap { index in
            rideStore.rides.indices.contains(index) ? rideStore.rides[index].id : nil
        })
        selection.subtract(ids)
        rideStore.deleteRides(ids: ids)
    }

    private func deleteSelection() {
        rideStore.deleteRides(ids: selection)
        selection.removeAll()
        editMode = .inactive
    }

    private var syncStatus: String? {
        switch relay.bluetooth.rideSyncState {
        case .unavailable: nil
        case .checking: "Checking PedalOne ride history…"
        case let .syncing(completed, total): "Syncing rides \(completed) of \(total)…"
        case .upToDate: "Ride history is up to date"
        case .failed: "Ride sync paused; it will retry on reconnect"
        }
    }

    private var isSyncActive: Bool {
        switch relay.bluetooth.rideSyncState {
        case .checking, .syncing: true
        default: false
        }
    }

    private var syncStatusIcon: String {
        switch relay.bluetooth.rideSyncState {
        case .upToDate: "checkmark.circle.fill"
        case .failed: "exclamationmark.triangle.fill"
        default: "arrow.triangle.2.circlepath"
        }
    }

    private var syncStatusColor: Color {
        switch relay.bluetooth.rideSyncState {
        case .upToDate: .green
        case .failed: .orange
        default: .teal
        }
    }
}

private struct RideHeader: View {
    var body: some View {
        HStack {
            Text("Date").frame(maxWidth: .infinity, alignment: .leading)
            Text("Distance").frame(width: 90, alignment: .trailing)
            Text("Time").frame(width: 70, alignment: .trailing)
        }
        .font(.caption.weight(.semibold))
        .foregroundStyle(.secondary)
    }
}

private struct RideRow: View {
    let ride: SavedRide

    var body: some View {
        HStack {
            Text(ride.date.formatted(date: .abbreviated, time: .omitted))
                .frame(maxWidth: .infinity, alignment: .leading)
            Text(ride.distanceMiles, format: .number.precision(.fractionLength(1)))
                .frame(width: 70, alignment: .trailing)
            Text("mi").foregroundStyle(.secondary).frame(width: 20, alignment: .leading)
            Text(formattedDuration).frame(width: 70, alignment: .trailing).monospacedDigit()
        }
    }

    private var formattedDuration: String {
        let seconds = max(0, Int(ride.duration))
        return String(format: "%d:%02d", seconds / 3600, (seconds / 60) % 60)
    }
}
