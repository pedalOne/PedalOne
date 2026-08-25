import CryptoKit
import Foundation

struct PreparedFirmware: Sendable {
    let version: String
    let data: Data
    let sourceURL: URL
}

enum FirmwareReleaseService {
    static var configuredManifestURL: URL? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: "RAC9000FirmwareManifestURL") as? String,
              !value.isEmpty, let url = URL(string: value), url.scheme?.lowercased() == "https" else { return nil }
        return url
    }

    nonisolated static func downloadUpdate(from manifestURL: URL) async throws -> PreparedFirmware {
        guard manifestURL.scheme?.lowercased() == "https" else { throw FirmwareReleaseError.httpsRequired }
        let delegate = HTTPSOnlyRedirectDelegate()
        let session = URLSession(configuration: .ephemeral, delegate: delegate, delegateQueue: nil)
        defer { session.finishTasksAndInvalidate() }

        let (manifestData, manifestResponse) = try await session.data(from: manifestURL)
        try validate(response: manifestResponse)
        let manifest = try JSONDecoder().decode(FirmwareManifest.self, from: manifestData)
        let sha256 = manifest.sha256.lowercased()
        guard !manifest.version.isEmpty, manifest.size > 0, manifest.size <= 3 * 1024 * 1024,
              sha256.count == 64, sha256.allSatisfy(\.isHexDigit) else {
            throw FirmwareReleaseError.invalidManifest
        }
        guard let firmwareURL = URL(string: manifest.url), firmwareURL.scheme?.lowercased() == "https" else {
            throw FirmwareReleaseError.httpsRequired
        }

        let (firmware, firmwareResponse) = try await session.data(from: firmwareURL)
        try validate(response: firmwareResponse)
        guard firmware.count == manifest.size else { throw FirmwareReleaseError.sizeMismatch }
        let actualSHA256 = SHA256.hash(data: firmware).map { String(format: "%02x", $0) }.joined()
        guard actualSHA256 == sha256 else { throw FirmwareReleaseError.sha256Mismatch }
        guard firmware.count >= 32, firmware.first == 0xE9 else { throw FirmwareReleaseError.invalidImage }
        return PreparedFirmware(version: manifest.version, data: firmware, sourceURL: firmwareURL)
    }

    nonisolated private static func validate(response: URLResponse) throws {
        guard let response = response as? HTTPURLResponse,
              (200..<300).contains(response.statusCode),
              response.url?.scheme?.lowercased() == "https" else {
            throw FirmwareReleaseError.invalidResponse
        }
    }
}

nonisolated private struct FirmwareManifest: Decodable, Sendable {
    let version: String
    let url: String
    let size: Int
    let sha256: String
}

private final class HTTPSOnlyRedirectDelegate: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    nonisolated func urlSession(
        _ session: URLSession,
        task: URLSessionTask,
        willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest,
        completionHandler: @escaping (URLRequest?) -> Void
    ) {
        completionHandler(request.url?.scheme?.lowercased() == "https" ? request : nil)
    }
}

private enum FirmwareReleaseError: LocalizedError {
    case httpsRequired
    case invalidManifest
    case invalidResponse
    case sizeMismatch
    case sha256Mismatch
    case invalidImage

    var errorDescription: String? {
        switch self {
        case .httpsRequired: "Firmware updates must use HTTPS."
        case .invalidManifest: "The firmware manifest is invalid."
        case .invalidResponse: "The firmware server returned an invalid response."
        case .sizeMismatch: "The downloaded firmware size does not match the trusted manifest."
        case .sha256Mismatch: "The downloaded firmware failed SHA-256 verification."
        case .invalidImage: "The downloaded file is not an ESP32 application image."
        }
    }
}
