import Foundation

/// RAC9000 ride-history protocol v1. Multibyte values are little-endian.
/// The firmware-facing wire format is documented here so both implementations
/// can share an exact, resumable contract.
enum RideSyncProtocol {
    static let version: UInt8 = 1

    enum Command: UInt8 {
        case requestIndex = 0x01
        case requestRide = 0x02
        case acknowledgeChunk = 0x03
    }

    enum Event: UInt8 {
        case indexBegin = 0x81
        case rideSummary = 0x82
        case indexEnd = 0x83
        case rideChunk = 0x84
        case rideEnd = 0x85
        case error = 0xFF
    }

    static func requestIndex() -> Data {
        Data([version, Command.requestIndex.rawValue])
    }

    static func requestRide(id: UUID, offset: UInt32) -> Data {
        var data = Data([version, Command.requestRide.rawValue])
        data.appendUUID(id)
        data.appendLE(offset)
        return data
    }

    static func acknowledge(id: UUID, nextOffset: UInt32) -> Data {
        var data = Data([version, Command.acknowledgeChunk.rawValue])
        data.appendUUID(id)
        data.appendLE(nextOffset)
        return data
    }

    static func crc32(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data {
            crc ^= UInt32(byte)
            for _ in 0..<8 {
                crc = (crc >> 1) ^ ((crc & 1) == 1 ? 0xEDB8_8320 : 0)
            }
        }
        return ~crc
    }
}

struct RideSyncSummary: Equatable {
    let id: UUID
    let startTimestamp: UInt32
    let durationSeconds: UInt32
    let distanceMeters: Float
    let maximumSpeedMetersPerSecond: Float
    let climbMeters: Float
    let pointCount: UInt16
    let logByteCount: UInt32
    let logCRC32: UInt32

    init?(data: Data) {
        var cursor = RideDataCursor(data)
        guard cursor.readUInt8() == RideSyncProtocol.version,
              cursor.readUInt8() == RideSyncProtocol.Event.rideSummary.rawValue,
              let id = cursor.readUUID(),
              let startTimestamp = cursor.readUInt32(),
              let durationSeconds = cursor.readUInt32(),
              let distanceMeters = cursor.readFloat32(),
              let maximumSpeed = cursor.readFloat32(),
              let climbMeters = cursor.readFloat32(),
              let pointCount = cursor.readUInt16(),
              let logByteCount = cursor.readUInt32(),
              let crc = cursor.readUInt32(),
              cursor.isAtEnd else { return nil }
        self.id = id
        self.startTimestamp = startTimestamp
        self.durationSeconds = durationSeconds
        self.distanceMeters = distanceMeters
        self.maximumSpeedMetersPerSecond = maximumSpeed
        self.climbMeters = climbMeters
        self.pointCount = pointCount
        self.logByteCount = logByteCount
        self.logCRC32 = crc
    }

    func decodeRide(log: Data) -> SavedRide? {
        let bytesPerPoint = 16
        guard log.count == Int(logByteCount),
              log.count == Int(pointCount) * bytesPerPoint,
              RideSyncProtocol.crc32(log) == logCRC32 else { return nil }

        var cursor = RideDataCursor(log)
        var points: [RidePoint] = []
        points.reserveCapacity(Int(pointCount))
        for _ in 0..<pointCount {
            guard let elapsedSeconds = cursor.readUInt16(),
                  let latitudeE7 = cursor.readInt32(),
                  let longitudeE7 = cursor.readInt32(),
                  let altitudeDm = cursor.readInt16(),
                  let speedCms = cursor.readUInt16(),
                  cursor.readUInt16() != nil else { return nil } // courseDeg100
            points.append(RidePoint(
                timestamp: Date(timeIntervalSince1970:
                    TimeInterval(startTimestamp) + TimeInterval(elapsedSeconds)),
                latitude: Double(latitudeE7) / 10_000_000,
                longitude: Double(longitudeE7) / 10_000_000,
                altitudeMeters: Double(altitudeDm) / 10,
                speedMetersPerSecond: Double(speedCms) / 100
            ))
        }
        guard cursor.isAtEnd else { return nil }
        return SavedRide(
            id: id,
            date: Date(timeIntervalSince1970: TimeInterval(startTimestamp)),
            duration: TimeInterval(durationSeconds),
            distanceMeters: Double(distanceMeters),
            maximumSpeedMetersPerSecond: Double(maximumSpeedMetersPerSecond),
            climbMeters: Double(climbMeters),
            points: points
        )
    }
}

struct RideSyncChunk {
    let id: UUID
    let offset: UInt32
    let payload: Data

    init?(data: Data) {
        var cursor = RideDataCursor(data)
        guard cursor.readUInt8() == RideSyncProtocol.version,
              cursor.readUInt8() == RideSyncProtocol.Event.rideChunk.rawValue,
              let id = cursor.readUUID(),
              let offset = cursor.readUInt32() else { return nil }
        self.id = id
        self.offset = offset
        self.payload = cursor.remainder
    }
}

struct RideSyncEnd {
    let id: UUID
    let byteCount: UInt32
    let crc32: UInt32

    init?(data: Data) {
        var cursor = RideDataCursor(data)
        guard cursor.readUInt8() == RideSyncProtocol.version,
              cursor.readUInt8() == RideSyncProtocol.Event.rideEnd.rawValue,
              let id = cursor.readUUID(),
              let byteCount = cursor.readUInt32(),
              let crc32 = cursor.readUInt32(), cursor.isAtEnd else { return nil }
        self.id = id
        self.byteCount = byteCount
        self.crc32 = crc32
    }
}

private struct RideDataCursor {
    private let data: Data
    private(set) var offset = 0
    var isAtEnd: Bool { offset == data.count }
    var remainder: Data { data.subdata(in: offset..<data.count) }

    init(_ data: Data) { self.data = data }

    mutating func readUInt8() -> UInt8? {
        guard offset < data.count else { return nil }
        defer { offset += 1 }
        return data[offset]
    }

    mutating func readUInt16() -> UInt16? {
        guard let bytes = read(count: 2) else { return nil }
        return UInt16(bytes[0]) | UInt16(bytes[1]) << 8
    }

    mutating func readInt16() -> Int16? { readUInt16().map { Int16(bitPattern: $0) } }

    mutating func readUInt32() -> UInt32? {
        guard let bytes = read(count: 4) else { return nil }
        return UInt32(bytes[0]) | UInt32(bytes[1]) << 8 |
            UInt32(bytes[2]) << 16 | UInt32(bytes[3]) << 24
    }

    mutating func readInt32() -> Int32? { readUInt32().map { Int32(bitPattern: $0) } }
    mutating func readFloat32() -> Float? { readUInt32().map { Float(bitPattern: $0) } }

    mutating func readUUID() -> UUID? {
        guard let bytes = read(count: 16) else { return nil }
        return UUID(uuid: (
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11],
            bytes[12], bytes[13], bytes[14], bytes[15]
        ))
    }

    private mutating func read(count: Int) -> [UInt8]? {
        guard count >= 0, offset + count <= data.count else { return nil }
        defer { offset += count }
        return Array(data[offset..<(offset + count)])
    }
}

private extension Data {
    mutating func appendLE<T: FixedWidthInteger>(_ value: T) {
        var value = value.littleEndian
        Swift.withUnsafeBytes(of: &value) { append(contentsOf: $0) }
    }

    mutating func appendUUID(_ value: UUID) {
        var bytes = value.uuid
        Swift.withUnsafeBytes(of: &bytes) { append(contentsOf: $0) }
    }
}
