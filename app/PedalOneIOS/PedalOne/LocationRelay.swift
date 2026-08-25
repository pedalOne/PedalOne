import CoreLocation
import Foundation

@MainActor
final class LocationRelay: NSObject, ObservableObject {
    @Published private(set) var latestLocation: CLLocation?
    @Published private(set) var authorization: CLAuthorizationStatus
    @Published private(set) var errorMessage: String?
    @Published private(set) var lastCallbackAt: Date?
    var onLocation: ((CLLocation) -> Void)?

    private let manager = CLLocationManager()
    private(set) var isRunning = false

    override init() {
        authorization = manager.authorizationStatus
        super.init()
        manager.delegate = self
        manager.activityType = .fitness
        manager.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        // Deliver every usable Core Location fix so the relay can forward at
        // up to 5 Hz. Core Location controls the hardware's actual cadence.
        manager.distanceFilter = kCLDistanceFilterNone
        manager.pausesLocationUpdatesAutomatically = false
        manager.showsBackgroundLocationIndicator = true
    }

    func start() {
        isRunning = true
        errorMessage = nil
        switch manager.authorizationStatus {
        case .notDetermined: manager.requestWhenInUseAuthorization()
        case .authorizedWhenInUse:
            manager.requestAlwaysAuthorization()
            beginUpdates()
        case .authorizedAlways: beginUpdates()
        case .denied, .restricted:
            errorMessage = "Enable Always location access in Settings."
        @unknown default: errorMessage = "Location authorization is unavailable."
        }
    }

    func stop() {
        isRunning = false
        manager.stopUpdatingLocation()
        manager.allowsBackgroundLocationUpdates = false
    }

    private func beginUpdates() {
        guard isRunning else { return }
        manager.allowsBackgroundLocationUpdates = true
        manager.startUpdatingLocation()
    }
}

extension LocationRelay: CLLocationManagerDelegate {
    nonisolated func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        Task { @MainActor in
            authorization = manager.authorizationStatus
            if isRunning && (authorization == .authorizedAlways || authorization == .authorizedWhenInUse) { beginUpdates() }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let location = locations.last else { return }
        Task { @MainActor in
            lastCallbackAt = Date()
            // Accuracy is carried in the packet so PedalOne can decide how to
            // present it. Do not silently stop the relay during GPS warm-up or
            // when iOS temporarily reports a coarse fix.
            guard location.timestamp.timeIntervalSinceNow > -10,
                  location.horizontalAccuracy >= 0 else { return }
            errorMessage = nil
            latestLocation = location
            onLocation?(location)
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        Task { @MainActor in errorMessage = error.localizedDescription }
    }
}
