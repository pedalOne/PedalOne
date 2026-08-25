import CoreLocation
import Foundation

/// Protocol v2 adds signed barometric relative altitude in centimeters.
/// All multibyte values are little-endian. Total size: 28 bytes.
struct LocationPacket {
    static let protocolVersion: UInt8 = 2
    let sequence: UInt16
    let location: CLLocation
    let barometricRelativeAltitudeMeters: Double?

    var encoded: Data {
        let speedValid = location.speed >= 0
        let gpsAltitudeValid = location.verticalAccuracy >= 0
        let courseValid = location.course >= 0
        let barometerValid = barometricRelativeAltitudeMeters != nil
        var flags: UInt8 = 0
        if speedValid { flags |= 1 << 0 }
        if gpsAltitudeValid { flags |= 1 << 1 }
        if courseValid { flags |= 1 << 2 }
        if barometerValid { flags |= 1 << 3 }

        var data = Data()
        data.append(Self.protocolVersion)
        data.appendLE(sequence)
        data.appendLE(UInt32(clamping: Int(location.timestamp.timeIntervalSince1970)))
        data.appendLE(Int32(clamping: Int((location.coordinate.latitude * 10_000_000).rounded())))
        data.appendLE(Int32(clamping: Int((location.coordinate.longitude * 10_000_000).rounded())))
        data.appendLE(Int16(clamping: Int((location.altitude * 10).rounded())))
        data.appendLE(UInt16(clamping: speedValid ? Int((location.speed * 100).rounded()) : 0))
        data.appendLE(UInt16(clamping: courseValid ? Int((location.course * 100).rounded()) : 65_535))
        data.appendLE(UInt16(clamping: Int((max(0, location.horizontalAccuracy) * 100).rounded())))
        data.append(flags)
        data.appendLE(Int32(clamping: Int(((barometricRelativeAltitudeMeters ?? 0) * 100).rounded())))
        return data
    }
}

private extension Data {
    mutating func appendLE<T: FixedWidthInteger>(_ value: T) {
        var value = value.littleEndian
        Swift.withUnsafeBytes(of: &value) { append(contentsOf: $0) }
    }
}
