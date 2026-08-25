import SwiftUI
import UniformTypeIdentifiers

struct FirmwareUpdateView: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var relay: RelayController
    @State private var isChoosingFile = false
    @State private var selectedFirmware: Data?
    @State private var selectedFileName: String?
    @State private var fileError: String?
    @State private var isConfirmingInstall = false
    @State private var onlineUpdate: PreparedFirmware?
    @State private var isCheckingOnline = false
    @State private var onlineError: String?

    private var updateState: BLECentral.FirmwareUpdateState {
        relay.bluetooth.firmwareUpdateState
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("PedalOne") {
                    LabeledContent("Device", value: relay.bluetooth.deviceName)
                    LabeledContent("Installed firmware", value: relay.bluetooth.firmwareVersionText)
                    statusView
                }

                if let manifestURL = FirmwareReleaseService.configuredManifestURL {
                    Section("Online update") {
                        if let onlineUpdate {
                            LabeledContent("Available version", value: onlineUpdate.version)
                            LabeledContent("Verified size", value: onlineUpdate.data.count.formatted(.byteCount(style: .file)))
                            Label("SHA-256 verified", systemImage: "checkmark.shield.fill")
                                .foregroundStyle(.green)
                            Button("Install \(onlineUpdate.version)") {
                                relay.bluetooth.startFirmwareUpdate(onlineUpdate.data)
                            }
                            .disabled(updateState.isActive || !canStartUpdate)
                        } else {
                            Button {
                                checkOnline(manifestURL: manifestURL)
                            } label: {
                                if isCheckingOnline {
                                    HStack { ProgressView(); Text("Checking…") }
                                } else {
                                    Label("Check for Update", systemImage: "network")
                                }
                            }
                            .disabled(isCheckingOnline || updateState.isActive)
                        }
                        if let onlineError {
                            Label(onlineError, systemImage: "exclamationmark.triangle.fill")
                                .foregroundStyle(.red)
                        }
                    }
                }

                Section("Manual firmware image") {
                    if let selectedFileName, let selectedFirmware {
                        LabeledContent("File", value: selectedFileName)
                        LabeledContent("Size", value: selectedFirmware.count.formatted(.byteCount(style: .file)))
                    }

                    Button {
                        isChoosingFile = true
                    } label: {
                        Label(selectedFirmware == nil ? "Choose Firmware .bin" : "Choose Different File",
                              systemImage: "doc.badge.plus")
                    }
                    .disabled(updateState.isActive)

                    Text("Choose the ordinary PedalOne Arduino application .bin. Merged flash and filesystem images are rejected.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                if let fileError {
                    Section {
                        Label(fileError, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(.red)
                    }
                }

                Section {
                    if updateState.isActive, updateState != .rebooting {
                        Button("Cancel Update", role: .destructive) {
                            relay.bluetooth.cancelFirmwareUpdate()
                        }
                    } else {
                        Button {
                            isConfirmingInstall = true
                        } label: {
                            Label("Install on PedalOne", systemImage: "arrow.down.circle.fill")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(selectedFirmware == nil || !canStartUpdate)
                    }
                } footer: {
                    Text("Keep the PedalOne charged, keep this app open, and stay near the device until it restarts.")
                }
            }
            .navigationTitle("Firmware Update")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Done") { dismiss() }
                        .disabled(updateState.isActive && updateState != .rebooting)
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        relay.bluetooth.refreshFirmwareInfo()
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .disabled(updateState.isActive)
                }
            }
            .interactiveDismissDisabled(updateState.isActive && updateState != .rebooting)
            .fileImporter(
                isPresented: $isChoosingFile,
                allowedContentTypes: [UTType(filenameExtension: "bin") ?? .data],
                allowsMultipleSelection: false
            ) { result in
                switch result {
                case let .success(urls):
                    guard let url = urls.first else { return }
                    loadFirmware(from: url)
                case let .failure(error):
                    fileError = error.localizedDescription
                }
            }
            .confirmationDialog(
                "Install this firmware?",
                isPresented: $isConfirmingInstall,
                titleVisibility: .visible
            ) {
                Button("Install Firmware") {
                    if let selectedFirmware {
                        relay.bluetooth.startFirmwareUpdate(selectedFirmware)
                    }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Do not close the app or power off the PedalOne during the update.")
            }
            .onAppear {
                relay.bluetooth.refreshFirmwareInfo()
            }
        }
    }

    @ViewBuilder
    private var statusView: some View {
        switch updateState {
        case .unavailable:
            Label("OTA is unavailable. Install firmware 2.1.0 once over USB first.",
                  systemImage: "cable.connector")
                .foregroundStyle(.secondary)
        case .checking:
            HStack { ProgressView(); Text("Checking update support…") }
        case let .ready(version):
            Label("Ready · \(version)", systemImage: "checkmark.circle.fill")
                .foregroundStyle(.green)
        case .preparing:
            HStack { ProgressView(); Text("Preparing PedalOne…") }
        case let .uploading(sent, total):
            VStack(alignment: .leading, spacing: 8) {
                ProgressView(value: Double(sent), total: Double(max(total, 1)))
                Text("Uploading \(Int((Double(sent) / Double(max(total, 1))) * 100))%")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        case .verifying:
            HStack { ProgressView(); Text("Verifying firmware…") }
        case .rebooting:
            Label("Update accepted. PedalOne is restarting…", systemImage: "checkmark.circle.fill")
                .foregroundStyle(.green)
        case let .failed(message):
            Label(message, systemImage: "exclamationmark.triangle.fill")
                .foregroundStyle(.red)
        }
    }

    private var canStartUpdate: Bool {
        if case .ready = updateState { return true }
        if case .failed = updateState { return relay.bluetooth.firmwareVersion != nil }
        return false
    }

    private func loadFirmware(from url: URL) {
        let hasAccess = url.startAccessingSecurityScopedResource()
        defer { if hasAccess { url.stopAccessingSecurityScopedResource() } }
        do {
            let data = try Data(contentsOf: url, options: .mappedIfSafe)
            guard data.count >= 32, data.first == 0xE9 else {
                throw FirmwareFileError.invalidImage
            }
            guard data.count <= 3 * 1024 * 1024 else {
                throw FirmwareFileError.tooLarge
            }
            selectedFirmware = data
            selectedFileName = url.lastPathComponent
            fileError = nil
        } catch {
            selectedFirmware = nil
            selectedFileName = nil
            fileError = error.localizedDescription
        }
    }

    private func checkOnline(manifestURL: URL) {
        isCheckingOnline = true
        onlineError = nil
        Task {
            do {
                onlineUpdate = try await FirmwareReleaseService.downloadUpdate(from: manifestURL)
            } catch {
                onlineUpdate = nil
                onlineError = error.localizedDescription
            }
            isCheckingOnline = false
        }
    }
}

private enum FirmwareFileError: LocalizedError {
    case invalidImage
    case tooLarge

    var errorDescription: String? {
        switch self {
        case .invalidImage: "This is not an ESP32 application firmware image."
        case .tooLarge: "This firmware image is larger than the 3 MiB OTA slot."
        }
    }
}
