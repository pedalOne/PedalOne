import SwiftUI

enum PedalOnePalette {
    static let background = Color(red: 0.025, green: 0.035, blue: 0.033)
    static let surface = Color(red: 0.07, green: 0.095, blue: 0.09)
    static let raisedSurface = Color(red: 0.095, green: 0.125, blue: 0.118)
    static let lime = Color(red: 0.79, green: 1.0, blue: 0.0)
    static let cyan = Color(red: 0.0, green: 0.79, blue: 0.96)
    static let muted = Color(red: 0.60, green: 0.67, blue: 0.65)

    static let brandGradient = LinearGradient(
        colors: [lime, Color(red: 0.38, green: 0.96, blue: 0.34), cyan],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
}

private enum PedalOneTab: Hashable {
    case ride
    case history
    case device
}

struct PedalOneRootView: View {
    @State private var selectedTab: PedalOneTab = .ride
    @State private var isShowingSettings = false

    var body: some View {
        TabView(selection: $selectedTab) {
            PedalOneRideView(showSettings: { isShowingSettings = true })
                .tag(PedalOneTab.ride)
                .tabItem { Label("Ride", systemImage: "bicycle") }

            PedalOneHistoryView(showSettings: { isShowingSettings = true })
                .tag(PedalOneTab.history)
                .tabItem { Label("History", systemImage: "clock.arrow.circlepath") }

            PedalOneDeviceView(showSettings: { isShowingSettings = true })
                .tag(PedalOneTab.device)
                .tabItem { Label("Device", systemImage: "dot.radiowaves.left.and.right") }
        }
        .tint(PedalOnePalette.lime)
        .toolbarBackground(PedalOnePalette.background, for: .tabBar)
        .toolbarBackground(.visible, for: .tabBar)
        .preferredColorScheme(.dark)
        .sheet(isPresented: $isShowingSettings) {
            PedalOneSettingsView()
        }
    }
}

private struct PedalOneScreen<Content: View>: View {
    let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        ZStack {
            PedalOnePalette.background.ignoresSafeArea()
            RadialGradient(
                colors: [PedalOnePalette.cyan.opacity(0.12), .clear],
                center: .topTrailing,
                startRadius: 0,
                endRadius: 360
            )
            .ignoresSafeArea()
            content
        }
    }
}

private struct PedalOneHeader: View {
    let eyebrow: String
    let title: String
    let showSettings: () -> Void

    var body: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 4) {
                Text(eyebrow)
                    .font(.caption.weight(.medium))
                    .tracking(2.1)
                    .foregroundStyle(PedalOnePalette.muted)
                Text(title)
                    .font(.largeTitle.weight(.semibold))
                    .foregroundStyle(.white)
            }

            Spacer()

            Button(action: showSettings) {
                ZStack(alignment: .bottomTrailing) {
                    Circle()
                        .fill(PedalOnePalette.brandGradient)
                        .frame(width: 48, height: 48)
                    Text("P")
                        .font(.title3.weight(.bold))
                        .foregroundStyle(.black)
                        .frame(width: 48, height: 48)
                    Image(systemName: "gearshape.fill")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(.white)
                        .padding(5)
                        .background(PedalOnePalette.raisedSurface, in: Circle())
                        .offset(x: 3, y: 3)
                }
            }
            .buttonStyle(.plain)
            .accessibilityLabel("PedalOne settings")
        }
    }
}

private struct PedalOneCard<Content: View>: View {
    let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        content
            .padding(18)
            .background(PedalOnePalette.surface, in: RoundedRectangle(cornerRadius: 24, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .stroke(.white.opacity(0.06), lineWidth: 1)
            }
    }
}

private struct PedalOneStatusPill: View {
    let icon: String
    let text: String
    let color: Color

    var body: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(color)
                .frame(width: 8, height: 8)
                .shadow(color: color.opacity(0.75), radius: 5)
            Image(systemName: icon)
            Text(text)
                .lineLimit(1)
            Spacer(minLength: 0)
        }
        .font(.subheadline.weight(.medium))
        .foregroundStyle(PedalOnePalette.muted)
        .padding(.horizontal, 14)
        .padding(.vertical, 11)
        .background(PedalOnePalette.surface, in: Capsule())
    }
}

private struct PedalOneRideView: View {
    @EnvironmentObject private var relay: RelayController
    @EnvironmentObject private var rideStore: RideStore
    let showSettings: () -> Void

    private var todayRides: [SavedRide] {
        rideStore.rides.filter { Calendar.current.isDateInToday($0.date) }
    }

    private var todayDistance: Double {
        todayRides.reduce(0) { $0 + $1.distanceMiles }
    }

    private var weekDistance: Double {
        guard let start = Calendar.current.date(byAdding: .day, value: -7, to: Date()) else { return 0 }
        return rideStore.rides.filter { $0.date >= start }.reduce(0) { $0 + $1.distanceMiles }
    }

    var body: some View {
        NavigationStack {
            PedalOneScreen {
                ScrollView {
                    VStack(spacing: 18) {
                        PedalOneHeader(
                            eyebrow: "PEDALONE",
                            title: rideStore.isRecording ? "Ride in progress" : "Ready to ride",
                            showSettings: showSettings
                        )

                        PedalOneStatusPill(
                            icon: relay.bluetooth.state == .connected ? "checkmark.circle.fill" : "antenna.radiowaves.left.and.right",
                            text: relay.bluetooth.state == .connected
                                ? "\(relay.bluetooth.deviceName) connected"
                                : relay.bluetooth.connectionStage,
                            color: relay.bluetooth.state == .connected ? PedalOnePalette.lime : .orange
                        )

                        PedalOneCard {
                            VStack(alignment: .leading, spacing: 12) {
                                Text("TODAY")
                                    .font(.caption.weight(.semibold))
                                    .tracking(1.8)
                                    .foregroundStyle(PedalOnePalette.muted)
                                HStack(alignment: .firstTextBaseline, spacing: 7) {
                                    Text(todayDistance, format: .number.precision(.fractionLength(1)))
                                        .font(.system(size: 56, weight: .semibold, design: .rounded))
                                        .foregroundStyle(.white)
                                    Text("mi")
                                        .font(.title3.weight(.medium))
                                        .foregroundStyle(PedalOnePalette.muted)
                                }
                                Text(todayRides.isEmpty ? "No rides yet" : "\(todayRides.count) ride\(todayRides.count == 1 ? "" : "s") recorded")
                                    .foregroundStyle(PedalOnePalette.muted)
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }

                        HStack(spacing: 12) {
                            compactMetric("THIS WEEK", value: String(format: "%.1f mi", weekDistance))
                            compactMetric("GPS", value: gpsSummary)
                        }

                        Button {
                            relay.toggleRelay()
                        } label: {
                            HStack(spacing: 10) {
                                Image(systemName: relay.isRelaying ? "stop.fill" : "location.fill")
                                Text(relay.isRelaying ? "Stop GPS Relay" : "Start GPS Relay")
                            }
                            .font(.headline)
                            .foregroundStyle(relay.isRelaying ? .white : .black)
                            .frame(maxWidth: .infinity, minHeight: 56)
                            .background(
                                relay.isRelaying
                                    ? AnyShapeStyle(Color.red)
                                    : AnyShapeStyle(PedalOnePalette.brandGradient),
                                in: RoundedRectangle(cornerRadius: 18, style: .continuous)
                            )
                        }
                        .buttonStyle(.plain)

                        Text("The phone supplies GPS speed and barometric altitude while PedalOne remains the authoritative ride recorder.")
                            .font(.footnote)
                            .foregroundStyle(PedalOnePalette.muted)
                            .multilineTextAlignment(.center)

                        if let error = relay.location.errorMessage {
                            Label(error, systemImage: "exclamationmark.triangle.fill")
                                .font(.footnote)
                                .foregroundStyle(.red)
                        }
                    }
                    .padding(.horizontal, 18)
                    .padding(.top, 12)
                    .padding(.bottom, 28)
                }
            }
            .toolbar(.hidden, for: .navigationBar)
        }
    }

    private func compactMetric(_ label: String, value: String) -> some View {
        PedalOneCard {
            VStack(alignment: .leading, spacing: 8) {
                Text(label)
                    .font(.caption2.weight(.semibold))
                    .tracking(1.2)
                    .foregroundStyle(PedalOnePalette.muted)
                Text(value)
                    .font(.headline.weight(.semibold))
                    .foregroundStyle(.white)
                    .lineLimit(1)
                    .minimumScaleFactor(0.75)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var gpsSummary: String {
        guard let accuracy = relay.location.latestLocation?.horizontalAccuracy else { return "Waiting…" }
        return "±\(Int(accuracy.rounded())) m"
    }
}

private struct PedalOneHistoryView: View {
    @EnvironmentObject private var rideStore: RideStore
    @EnvironmentObject private var relay: RelayController
    @State private var editMode: EditMode = .inactive
    @State private var selection: Set<UUID> = []
    @State private var showingDeleteConfirmation = false
    let showSettings: () -> Void

    private var totalDistance: Double {
        rideStore.rides.reduce(0) { $0 + $1.distanceMiles }
    }

    private var totalDuration: TimeInterval {
        rideStore.rides.reduce(0) { $0 + $1.duration }
    }

    var body: some View {
        NavigationStack {
            PedalOneScreen {
                VStack(spacing: 14) {
                    PedalOneHeader(eyebrow: "PEDALONE", title: "Ride history", showSettings: showSettings)
                        .padding(.horizontal, 18)
                        .padding(.top, 12)

                    if !rideStore.rides.isEmpty {
                        HStack {
                            historySummary("DISTANCE", value: String(format: "%.1f mi", totalDistance))
                            historySummary("TIME", value: formattedTotalDuration)
                            historySummary("RIDES", value: "\(rideStore.rides.count)")
                        }
                        .padding(.horizontal, 18)

                        HStack {
                            syncStatusView
                            Spacer()
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
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(PedalOnePalette.lime)
                        }
                        .padding(.horizontal, 18)
                    }

                    if rideStore.rides.isEmpty {
                        Spacer()
                        ContentUnavailableView(
                            "No Saved Rides",
                            systemImage: "bicycle",
                            description: Text("Complete a ride with PedalOne to see it here.")
                        )
                        .foregroundStyle(.white)
                        Spacer()
                    } else {
                        List(selection: $selection) {
                            ForEach(rideStore.rides) { ride in
                                if editMode.isEditing {
                                    PedalOneRideRow(ride: ride)
                                        .tag(ride.id)
                                } else {
                                    NavigationLink {
                                        RideDetailDashboard(ride: ride)
                                    } label: {
                                        PedalOneRideRow(ride: ride)
                                    }
                                }
                            }
                            .onDelete(perform: delete)
                            .listRowBackground(PedalOnePalette.surface)
                            .listRowSeparatorTint(.white.opacity(0.08))
                        }
                        .listStyle(.plain)
                        .scrollContentBackground(.hidden)
                        .environment(\.editMode, $editMode)
                    }
                }
            }
            .toolbar(.hidden, for: .navigationBar)
            .safeAreaInset(edge: .bottom, spacing: 0) {
                if editMode.isEditing {
                    Button {
                        showingDeleteConfirmation = true
                    } label: {
                        Label("Delete Selected (\(selection.count))", systemImage: "trash.fill")
                            .font(.headline)
                            .foregroundStyle(.white)
                            .frame(maxWidth: .infinity, minHeight: 50)
                            .background(.red, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                    }
                    .buttonStyle(.plain)
                    .disabled(selection.isEmpty)
                    .opacity(selection.isEmpty ? 0.45 : 1)
                    .padding(.horizontal, 18)
                    .padding(.vertical, 10)
                    .background(PedalOnePalette.background)
                }
            }
            .confirmationDialog(
                "Delete Selected Rides?",
                isPresented: $showingDeleteConfirmation,
                titleVisibility: .visible
            ) {
                Button("Delete \(selection.count) Ride\(selection.count == 1 ? "" : "s")", role: .destructive) {
                    rideStore.deleteRides(ids: selection)
                    selection.removeAll()
                    editMode = .inactive
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This removes the selected rides and their GPS logs from this iPhone.")
            }
        }
    }

    private func historySummary(_ label: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(label)
                .font(.caption2.weight(.semibold))
                .tracking(1)
                .foregroundStyle(PedalOnePalette.muted)
            Text(value)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.white)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder
    private var syncStatusView: some View {
        if let status = syncStatus {
            HStack(spacing: 6) {
                if isSyncActive { ProgressView().controlSize(.small) }
                Image(systemName: syncStatusIcon)
                    .foregroundStyle(syncStatusColor)
                Text(status)
                    .font(.caption)
                    .foregroundStyle(PedalOnePalette.muted)
                    .lineLimit(1)
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

    private var formattedTotalDuration: String {
        let minutes = Int(totalDuration) / 60
        return "\(minutes / 60)h \(minutes % 60)m"
    }

    private var syncStatus: String? {
        switch relay.bluetooth.rideSyncState {
        case .unavailable: nil
        case .checking: "Checking…"
        case let .syncing(completed, total): "Syncing \(completed)/\(total)"
        case .upToDate: "Synced"
        case .failed: "Sync paused"
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
        case .upToDate: PedalOnePalette.lime
        case .failed: .orange
        default: PedalOnePalette.cyan
        }
    }
}

private struct PedalOneRideRow: View {
    let ride: SavedRide

    var body: some View {
        HStack(spacing: 13) {
            ZStack {
                Circle().fill(PedalOnePalette.brandGradient)
                Image(systemName: "point.topleft.down.to.point.bottomright.curvepath")
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(.black)
            }
            .frame(width: 38, height: 38)

            VStack(alignment: .leading, spacing: 4) {
                Text(ride.date.formatted(date: .abbreviated, time: .omitted))
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.white)
                Text(ride.date.formatted(date: .omitted, time: .shortened))
                    .font(.caption)
                    .foregroundStyle(PedalOnePalette.muted)
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                Text("\(ride.distanceMiles, specifier: "%.1f") mi")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.white)
                Text(formattedDuration)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(PedalOnePalette.muted)
            }
        }
        .padding(.vertical, 7)
    }

    private var formattedDuration: String {
        let seconds = max(0, Int(ride.duration))
        return String(format: "%d:%02d", seconds / 3600, (seconds / 60) % 60)
    }
}

private struct PedalOneDeviceView: View {
    @EnvironmentObject private var relay: RelayController
    @State private var isRenamingDevice = false
    @State private var proposedDeviceName = ""
    @State private var isShowingFirmwareUpdate = false
    @State private var selectedDevice: BLECentral.BikeDevice?
    let showSettings: () -> Void

    private var connectedDevice: BLECentral.BikeDevice? {
        relay.bluetooth.devices.first { $0.id == relay.bluetooth.connectedDeviceID }
    }

    var body: some View {
        NavigationStack {
            PedalOneScreen {
                ScrollView {
                    VStack(spacing: 18) {
                        PedalOneHeader(eyebrow: "PEDALONE", title: "Your device", showSettings: showSettings)

                        PedalOneCard {
                            HStack(spacing: 16) {
                                Image("PedalOneLogo")
                                    .resizable()
                                    .scaledToFit()
                                    .frame(width: 78, height: 78)
                                    .clipShape(Circle())

                                VStack(alignment: .leading, spacing: 6) {
                                    Text(relay.bluetooth.state == .connected ? "CONNECTED" : "SEARCHING")
                                        .font(.caption.weight(.semibold))
                                        .tracking(1.5)
                                        .foregroundStyle(relay.bluetooth.state == .connected ? PedalOnePalette.lime : .orange)
                                    Text(relay.bluetooth.deviceName)
                                        .font(.title2.weight(.semibold))
                                        .foregroundStyle(.white)
                                    Text(relay.bluetooth.connectionStage)
                                        .font(.footnote)
                                        .foregroundStyle(PedalOnePalette.muted)
                                }
                                Spacer()
                            }
                        }

                        HStack(spacing: 12) {
                            deviceMetric("FIRMWARE", value: relay.bluetooth.firmwareVersionText, icon: "cpu")
                            deviceMetric("SIGNAL", value: signalText, icon: "antenna.radiowaves.left.and.right")
                        }

                        PedalOneCard {
                            VStack(spacing: 0) {
                                detailRow("GPS", value: gpsText, icon: "location.fill")
                                Divider().overlay(.white.opacity(0.08))
                                detailRow("Speed", value: speedText, icon: "speedometer")
                                Divider().overlay(.white.opacity(0.08))
                                detailRow("Barometer", value: barometerText, icon: "mountain.2.fill")
                                Divider().overlay(.white.opacity(0.08))
                                detailRow("Relay", value: relay.relayMessage, icon: "arrow.left.arrow.right")
                                Divider().overlay(.white.opacity(0.08))
                                detailRow(
                                    "Last sent",
                                    value: relay.lastSent?.formatted(date: .omitted, time: .standard) ?? "Not started",
                                    icon: "clock"
                                )
                            }
                        }

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
                            Label("Manage PedalOne", systemImage: "slider.horizontal.3")
                                .font(.headline)
                                .foregroundStyle(.black)
                                .frame(maxWidth: .infinity, minHeight: 54)
                                .background(PedalOnePalette.brandGradient, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                        }

                        if let error = relay.bluetooth.connectionError ?? relay.bluetooth.renameError {
                            Label(error, systemImage: "exclamationmark.triangle.fill")
                                .font(.footnote)
                                .foregroundStyle(.red)
                        }
                    }
                    .padding(.horizontal, 18)
                    .padding(.top, 12)
                    .padding(.bottom, 28)
                }
            }
            .toolbar(.hidden, for: .navigationBar)
            .alert("Rename PedalOne", isPresented: $isRenamingDevice) {
                TextField("Device name", text: $proposedDeviceName)
                Button("Cancel", role: .cancel) {}
                Button("Save") { relay.bluetooth.renameConnectedDevice(to: proposedDeviceName) }
            } message: {
                Text("The name is saved on this iPhone and, with updated firmware, on PedalOne itself.")
            }
            .sheet(isPresented: $isShowingFirmwareUpdate) {
                FirmwareUpdateView()
            }
            .sheet(item: $selectedDevice) { device in
                PedalOneDeviceStatusSheet(deviceID: device.id, fallbackName: device.name)
                    .presentationDetents([.medium])
            }
        }
    }

    private func deviceMetric(_ label: String, value: String, icon: String) -> some View {
        PedalOneCard {
            VStack(alignment: .leading, spacing: 8) {
                Image(systemName: icon).foregroundStyle(PedalOnePalette.cyan)
                Text(label)
                    .font(.caption2.weight(.semibold))
                    .tracking(1.1)
                    .foregroundStyle(PedalOnePalette.muted)
                Text(value)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.white)
                    .lineLimit(1)
                    .minimumScaleFactor(0.65)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func detailRow(_ title: String, value: String, icon: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .foregroundStyle(PedalOnePalette.cyan)
                .frame(width: 22)
            Text(title).foregroundStyle(PedalOnePalette.muted)
            Spacer()
            Text(value)
                .fontWeight(.medium)
                .foregroundStyle(.white)
                .multilineTextAlignment(.trailing)
        }
        .font(.subheadline)
        .frame(minHeight: 48)
    }

    private var signalText: String {
        connectedDevice.map { "\($0.rssi) dBm" } ?? "—"
    }

    private var gpsText: String {
        guard let accuracy = relay.location.latestLocation?.horizontalAccuracy else { return "Waiting…" }
        return String(format: "%.0f m accuracy", accuracy)
    }

    private var speedText: String {
        guard let speed = relay.location.latestLocation?.speed, speed >= 0 else { return "—" }
        return String(format: "%.1f mph", speed * 2.236_936)
    }

    private var barometerText: String {
        guard relay.barometer.isAvailable else { return "GPS fallback" }
        guard let meters = relay.barometer.relativeAltitudeMeters else { return "Waiting…" }
        return String(format: "%+.1f ft", meters * 3.28084)
    }
}

private struct PedalOneDeviceStatusSheet: View {
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
                    if let device { LabeledContent("Signal", value: "\(device.rssi) dBm") }
                }

                if !isReady, relay.bluetooth.connectionError == nil {
                    HStack(spacing: 10) {
                        ProgressView()
                        Text(relay.bluetooth.connectionStage).foregroundStyle(.secondary)
                    }
                }

                if let error = relay.bluetooth.connectionError {
                    Section("BLE Diagnostics") {
                        Label(error, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(PedalOnePalette.background)
            .navigationTitle("PedalOne")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) { Button("Done") { dismiss() } }
            }
            .onChange(of: relay.bluetooth.connectedDeviceID) { _, connectedID in
                if connectedID == deviceID { relay.bluetooth.refreshFirmwareInfo() }
            }
        }
        .preferredColorScheme(.dark)
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

private struct PedalOneSettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var isShowingANCSTest = false

    var body: some View {
        NavigationStack {
            ZStack {
                PedalOnePalette.background.ignoresSafeArea()
                Form {
                    Section {
                        HStack(spacing: 14) {
                            ZStack {
                                Circle().fill(PedalOnePalette.brandGradient)
                                Text("P")
                                    .font(.title2.weight(.bold))
                                    .foregroundStyle(.black)
                            }
                            .frame(width: 52, height: 52)

                            VStack(alignment: .leading, spacing: 4) {
                                Text("PedalOne settings").font(.headline)
                                Text("No account required").font(.subheadline).foregroundStyle(.secondary)
                            }
                        }
                    }

                    Section("Tools") {
                        Button {
                            isShowingANCSTest = true
                        } label: {
                            Label("ANCS Navigation Test", systemImage: "bell.badge")
                        }
                    }

                    Section("Ride Data") {
                        Label("Ride history is stored on this iPhone", systemImage: "iphone")
                        Label("PedalOne syncs saved rides over Bluetooth", systemImage: "arrow.triangle.2.circlepath")
                    }

                    Section("About") {
                        LabeledContent("App", value: "Pedal One")
                        LabeledContent("Version", value: appVersion)
                        Link(destination: URL(string: "https://github.com/pedalOne/PedalOne")!) {
                            Label("Open-source project", systemImage: "chevron.left.forwardslash.chevron.right")
                        }
                    }
                }
                .scrollContentBackground(.hidden)
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) { Button("Done") { dismiss() } }
            }
            .sheet(isPresented: $isShowingANCSTest) {
                ANCSTestView()
                    .preferredColorScheme(.dark)
            }
        }
        .tint(PedalOnePalette.lime)
        .preferredColorScheme(.dark)
    }

    private var appVersion: String {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0"
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"
        return "\(version) (\(build))"
    }
}
