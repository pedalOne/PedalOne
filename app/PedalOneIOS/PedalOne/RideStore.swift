import CoreLocation
import Foundation

struct RidePoint: Codable, Hashable, Identifiable {
    var id: Date { timestamp }
    let timestamp: Date
    let latitude: Double
    let longitude: Double
    let altitudeMeters: Double
    let speedMetersPerSecond: Double

    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }
}

struct SavedRide: Codable, Hashable, Identifiable {
    let id: UUID
    let date: Date
    let duration: TimeInterval
    let distanceMeters: Double
    let maximumSpeedMetersPerSecond: Double
    let climbMeters: Double
    let points: [RidePoint]

    var distanceMiles: Double { distanceMeters * 0.000_621_371 }
    var averageSpeedMilesPerHour: Double {
        duration > 0 ? distanceMeters / duration * 2.236_936 : 0
    }
    var maximumSpeedMilesPerHour: Double { maximumSpeedMetersPerSecond * 2.236_936 }
    var climbFeet: Double { climbMeters * 3.280_84 }
}

@MainActor
final class RideStore: ObservableObject {
    @Published private(set) var rides: [SavedRide] = []
    @Published private(set) var isRecording = false

    private static let logInterval: TimeInterval = 10
    private var rideID: UUID?
    private var startedAt: Date?
    private var points: [RidePoint] = []
    private var lastRecordedAt: Date?
    private var maximumSpeedMetersPerSecond = 0.0

    init() { load() }

    func containsRide(id: UUID) -> Bool {
        rides.contains { $0.id == id }
    }

    func importSyncedRide(_ ride: SavedRide) {
        if let exactIndex = rides.firstIndex(where: { $0.id == ride.id }) {
            rides[exactIndex] = ride
        } else if let localIndex = rides.firstIndex(where: {
            abs($0.date.timeIntervalSince(ride.date)) <= 90 &&
            abs($0.duration - ride.duration) <= 30
        }) {
            // Replace the phone's live copy with the authoritative RAC9000
            // record instead of displaying the same ride twice.
            rides[localIndex] = ride
        } else {
            rides.append(ride)
        }
        rides.sort { $0.date > $1.date }
        save()
    }

    func deleteRides(ids: Set<UUID>) {
        guard !ids.isEmpty else { return }
        rides.removeAll { ids.contains($0.id) }
        save()
    }

    func startRide() {
        guard !isRecording else { return }
        isRecording = true
        rideID = UUID()
        startedAt = nil
        points.removeAll(keepingCapacity: true)
        lastRecordedAt = nil
        maximumSpeedMetersPerSecond = 0
    }

    func record(_ location: CLLocation, barometricRelativeAltitudeMeters: Double?) {
        guard isRecording else { return }
        if startedAt == nil { startedAt = location.timestamp }
        if location.speed >= 0 {
            maximumSpeedMetersPerSecond = max(maximumSpeedMetersPerSecond, location.speed)
        }
        guard lastRecordedAt.map({ location.timestamp.timeIntervalSince($0) >= Self.logInterval }) ?? true else { return }

        points.append(RidePoint(
            timestamp: location.timestamp,
            latitude: location.coordinate.latitude,
            longitude: location.coordinate.longitude,
            altitudeMeters: barometricRelativeAltitudeMeters ?? location.altitude,
            speedMetersPerSecond: max(0, location.speed)
        ))
        lastRecordedAt = location.timestamp
    }

    func finishRide() {
        guard isRecording else { return }
        defer { resetActiveRide() }
        guard let id = rideID, let start = startedAt, let end = points.last?.timestamp,
              points.count >= 2 else { return }

        let ride = SavedRide(
            id: id,
            date: start,
            duration: max(0, end.timeIntervalSince(start)),
            distanceMeters: routeDistance,
            maximumSpeedMetersPerSecond: maximumSpeedMetersPerSecond,
            climbMeters: accumulatedClimb,
            points: points
        )
        rides.insert(ride, at: 0)
        save()
    }

    private var routeDistance: Double {
        zip(points, points.dropFirst()).reduce(0) { result, pair in
            let first = CLLocation(latitude: pair.0.latitude, longitude: pair.0.longitude)
            let second = CLLocation(latitude: pair.1.latitude, longitude: pair.1.longitude)
            return result + first.distance(from: second)
        }
    }

    private var accumulatedClimb: Double {
        zip(points, points.dropFirst()).reduce(0) { result, pair in
            let gain = pair.1.altitudeMeters - pair.0.altitudeMeters
            return result + (gain >= 1 ? gain : 0)
        }
    }

    private func resetActiveRide() {
        isRecording = false
        rideID = nil
        startedAt = nil
        points.removeAll(keepingCapacity: false)
        lastRecordedAt = nil
        maximumSpeedMetersPerSecond = 0
    }

    private var storageURL: URL? {
        guard let directory = try? FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        ) else { return nil }
        let folder = directory.appendingPathComponent("PedalOne", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.appendingPathComponent("rides.json")
    }

    private func load() {
        guard let url = storageURL,
              let data = try? Data(contentsOf: url),
              let decoded = try? JSONDecoder().decode([SavedRide].self, from: data) else { return }
        rides = decoded.sorted { $0.date > $1.date }
    }

    private func save() {
        guard let url = storageURL,
              let data = try? JSONEncoder().encode(rides) else { return }
        try? data.write(to: url, options: .atomic)
    }
}
