import Foundation
import UserNotifications

struct NavigationTestStep: Identifiable, Equatable {
    let id: String
    let number: Int
    let delay: TimeInterval
    let instruction: String
    let detail: String

    var scheduledDescription: String {
        let seconds = Int(delay.rounded())
        return seconds < 60 ? "in \(seconds)s" : "in \(seconds / 60)m \(seconds % 60)s"
    }
}

@MainActor
final class ANCSTestManager: NSObject, ObservableObject {
    enum PermissionState: Equatable {
        case unknown, denied, authorized
    }

    @Published private(set) var permissionState: PermissionState = .unknown
    @Published private(set) var scheduledSteps: [NavigationTestStep] = []
    @Published private(set) var statusText = "Ready to schedule a mock route"

    private static let identifierPrefix = "rac9000.google-maps-test."
    private let center = UNUserNotificationCenter.current()

    override init() {
        super.init()
        center.delegate = self
        refreshPermission()
    }

    func requestPermission() {
        Task {
            do {
                _ = try await center.requestAuthorization(options: [.alert, .sound, .badge])
                await updatePermissionState()
            } catch {
                statusText = "Notification permission request failed"
            }
        }
    }

    func scheduleRoute() {
        Task {
            let settings = await center.notificationSettings()
            guard settings.authorizationStatus == .authorized ||
                  settings.authorizationStatus == .provisional ||
                  settings.authorizationStatus == .ephemeral else {
                permissionState = settings.authorizationStatus == .denied ? .denied : .unknown
                statusText = "Allow notifications before starting the test"
                requestPermission()
                return
            }

            cancelTest(clearStatus: false)
            let steps = makeRoute()
            do {
                for step in steps {
                    let content = UNMutableNotificationContent()
                    content.title = step.instruction
                    content.subtitle = "Google Maps-style ANCS test"
                    content.body = step.detail
                    content.sound = .default
                    content.threadIdentifier = "rac9000-navigation-test"
                    content.categoryIdentifier = "RAC9000_NAVIGATION_TEST"
                    content.interruptionLevel = .timeSensitive
                    content.relevanceScore = 1

                    let trigger = UNTimeIntervalNotificationTrigger(
                        timeInterval: max(1, step.delay),
                        repeats: false
                    )
                    try await center.add(UNNotificationRequest(
                        identifier: step.id,
                        content: content,
                        trigger: trigger
                    ))
                }
                scheduledSteps = steps
                statusText = "16 directions scheduled"
            } catch {
                scheduledSteps = []
                statusText = "Could not schedule the test route"
            }
        }
    }

    func sendOneNow() {
        Task {
            let settings = await center.notificationSettings()
            guard settings.authorizationStatus == .authorized ||
                  settings.authorizationStatus == .provisional else {
                requestPermission()
                return
            }
            let choices = [
                "Turn left onto Market Street",
                "Turn right onto Valencia Street",
                "Make a U-turn at the next intersection"
            ]
            let content = UNMutableNotificationContent()
            content.title = choices.randomElement()!
            content.subtitle = "Google Maps-style ANCS test"
            content.body = "In 300 ft · Continue toward your destination"
            content.sound = .default
            content.threadIdentifier = "rac9000-navigation-test"
            content.categoryIdentifier = "RAC9000_NAVIGATION_TEST"
            content.interruptionLevel = .timeSensitive
            let id = Self.identifierPrefix + "now." + UUID().uuidString
            do {
                try await center.add(UNNotificationRequest(
                    identifier: id,
                    content: content,
                    trigger: UNTimeIntervalNotificationTrigger(timeInterval: 1, repeats: false)
                ))
                statusText = "Test direction queued"
            } catch {
                statusText = "Could not send test direction"
            }
        }
    }

    func cancelTest(clearStatus: Bool = true) {
        let identifiers = scheduledSteps.map(\.id)
        center.removePendingNotificationRequests(withIdentifiers: identifiers)
        center.removeDeliveredNotifications(withIdentifiers: identifiers)
        scheduledSteps = []
        if clearStatus { statusText = "Test route cancelled" }
    }

    private func makeRoute() -> [NavigationTestStep] {
        var maneuvers = Array(repeating: "left", count: 7) +
            Array(repeating: "right", count: 7) +
            Array(repeating: "u-turn", count: 2)
        maneuvers.shuffle()

        let streets = [
            "Market Street", "Valencia Street", "Mission Street", "Oak Street",
            "Fell Street", "Howard Street", "Bryant Street", "Townsend Street",
            "Embarcadero", "King Street", "3rd Street", "16th Street",
            "Duboce Avenue", "Page Street", "Castro Street", "Noe Street"
        ].shuffled()
        let distances = [150, 200, 300, 400, 500, 700, 900, 1_000]
        let burstIndexes: Set<Int> = [3, 4, 10, 14]
        var cumulativeDelay: TimeInterval = 2
        var results: [NavigationTestStep] = []

        for index in 0..<16 {
            if index > 0 {
                cumulativeDelay += TimeInterval(
                    burstIndexes.contains(index) ? Int.random(in: 3...7) : Int.random(in: 15...45)
                )
            }
            let maneuver = maneuvers[index]
            let street = streets[index]
            let instruction: String
            switch maneuver {
            case "left": instruction = "Turn left onto \(street)"
            case "right": instruction = "Turn right onto \(street)"
            default: instruction = "Make a U-turn at \(street)"
            }
            let nextDirection = index == 15
                ? "Destination will be on your right"
                : "Then continue toward \(streets[(index + 1) % streets.count])"
            let distance = distances.randomElement()!
            results.append(NavigationTestStep(
                id: Self.identifierPrefix + String(format: "%02d", index + 1),
                number: index + 1,
                delay: cumulativeDelay,
                instruction: instruction,
                detail: "In \(distance) ft · \(nextDirection)"
            ))
        }
        return results
    }

    private func refreshPermission() {
        Task { await updatePermissionState() }
    }

    private func updatePermissionState() async {
        let settings = await center.notificationSettings()
        switch settings.authorizationStatus {
        case .authorized, .provisional, .ephemeral: permissionState = .authorized
        case .denied: permissionState = .denied
        case .notDetermined: permissionState = .unknown
        @unknown default: permissionState = .unknown
        }
    }
}

extension ANCSTestManager: UNUserNotificationCenterDelegate {
    nonisolated func userNotificationCenter(
        _ center: UNUserNotificationCenter,
        willPresent notification: UNNotification
    ) async -> UNNotificationPresentationOptions {
        [.banner, .list, .sound]
    }
}
