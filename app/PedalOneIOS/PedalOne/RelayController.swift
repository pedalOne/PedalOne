import Combine
import CoreLocation
import Foundation

@MainActor
final class RelayController: ObservableObject {
    private static let transmissionInterval: TimeInterval = 1.0 / 5.0
    @Published private(set) var isRelaying = false
    @Published private(set) var lastSent: Date?
    @Published private(set) var relayMessage = "Not started"
    let location = LocationRelay()
    let barometer = BarometerRelay()
    let bluetooth = BLECentral()
    let rides = RideStore()

    private var sequence: UInt16 = 0
    private var lastTransmission = Date.distantPast
    private var cancellables = Set<AnyCancellable>()

    init() {
        location.onLocation = { [weak self] in self?.handle($0) }
        bluetooth.onLocationWriteAccepted = { [weak self] in
            guard let self else { return }
            let now = Date()
            self.lastTransmission = now
            self.lastSent = now
            self.relayMessage = "Sending"
        }
        bluetooth.onLocationWriteFailed = { [weak self] message in
            self?.relayMessage = "BLE write failed: \(message)"
        }
        bluetooth.configureRideSync(
            hasRide: { [weak rides] id in rides?.containsRide(id: id) ?? false },
            receiveRide: { [weak rides] ride in rides?.importSyncedRide(ride) }
        )
        for publisher in [location.objectWillChange.eraseToAnyPublisher(),
                          barometer.objectWillChange.eraseToAnyPublisher(),
                          bluetooth.objectWillChange.eraseToAnyPublisher(),
                          rides.objectWillChange.eraseToAnyPublisher()] {
            publisher.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &cancellables)
        }
        bluetooth.$bikeStatus
            .compactMap { $0 }
            .removeDuplicates()
            .sink { [weak self] status in self?.handleBikeStatus(status) }
            .store(in: &cancellables)
    }

    func toggleRelay() { isRelaying ? stop() : start() }
    func start() {
        isRelaying = true
        relayMessage = "Waiting for GPS fix"
        bluetooth.startScanning()
        barometer.start()
        location.start()
    }
    func stop() {
        isRelaying = false
        relayMessage = "Stopped"
        location.stop()
        barometer.stop()
    }

    private func handle(_ location: CLLocation) {
        guard isRelaying else { return }
        rides.record(location, barometricRelativeAltitudeMeters: barometer.relativeAltitudeMeters)
        guard
              Date().timeIntervalSince(lastTransmission) >= Self.transmissionInterval else { return }
        sequence &+= 1
        let packet = LocationPacket(sequence: sequence, location: location,
            barometricRelativeAltitudeMeters: barometer.relativeAltitudeMeters).encoded
        if bluetooth.send(packet) {
            relayMessage = "Sending…"
        } else {
            relayMessage = bluetooth.gpsWriteStatus
        }
    }

    private func handleBikeStatus(_ status: String) {
        switch status.lowercased() {
        case "riding": rides.startRide()
        case "summary": rides.finishRide()
        default: break
        }
    }
}
