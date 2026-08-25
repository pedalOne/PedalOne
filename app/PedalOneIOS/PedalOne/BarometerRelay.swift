import CoreMotion
import Foundation

@MainActor
final class BarometerRelay: ObservableObject {
    @Published private(set) var relativeAltitudeMeters: Double?
    @Published private(set) var pressureKPa: Double?
    @Published private(set) var isAvailable = CMAltimeter.isRelativeAltitudeAvailable()

    private let altimeter = CMAltimeter()
    private let queue: OperationQueue = {
        let queue = OperationQueue()
        queue.name = "com.byobike.PedalOne.altimeter"
        queue.qualityOfService = .userInitiated
        queue.maxConcurrentOperationCount = 1
        return queue
    }()

    func start() {
        guard CMAltimeter.isRelativeAltitudeAvailable() else {
            isAvailable = false
            return
        }
        altimeter.startRelativeAltitudeUpdates(to: queue) { [weak self] data, _ in
            guard let data else { return }
            Task { @MainActor in
                self?.relativeAltitudeMeters = data.relativeAltitude.doubleValue
                self?.pressureKPa = data.pressure.doubleValue
            }
        }
    }

    func stop() {
        altimeter.stopRelativeAltitudeUpdates()
        relativeAltitudeMeters = nil
        pressureKPa = nil
    }
}
