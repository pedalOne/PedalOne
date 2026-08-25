import SwiftUI

struct ANCSTestView: View {
    @StateObject private var tester = ANCSTestManager()

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Label(permissionText, systemImage: permissionIcon)
                        .foregroundStyle(permissionColor)
                    Text(tester.statusText)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                } header: {
                    Text("Notification status")
                }

                Section {
                    Button {
                        tester.scheduleRoute()
                    } label: {
                        Label("Start 16-step route", systemImage: "point.topleft.down.to.point.bottomright.curvepath")
                    }
                    .disabled(tester.permissionState == .denied)

                    Button {
                        tester.sendOneNow()
                    } label: {
                        Label("Send one direction now", systemImage: "bell.badge.fill")
                    }
                    .disabled(tester.permissionState == .denied)

                    if !tester.scheduledSteps.isEmpty {
                        Button(role: .destructive) {
                            tester.cancelTest()
                        } label: {
                            Label("Cancel scheduled route", systemImage: "xmark.circle")
                        }
                    }

                    if tester.permissionState != .authorized {
                        Button("Allow notifications") { tester.requestPermission() }
                    }
                } header: {
                    Text("Controls")
                } footer: {
                    Text("Most turns are 15–45 seconds apart, with four short 3–7 second bursts.")
                }

                if !tester.scheduledSteps.isEmpty {
                    Section("Scheduled directions") {
                        ForEach(tester.scheduledSteps) { step in
                            HStack(alignment: .top, spacing: 12) {
                                Text("\(step.number)")
                                    .font(.caption.bold())
                                    .foregroundStyle(.white)
                                    .frame(width: 25, height: 25)
                                    .background(.teal, in: Circle())
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(step.instruction).font(.subheadline.weight(.medium))
                                    Text(step.detail).font(.caption).foregroundStyle(.secondary)
                                }
                                Spacer(minLength: 4)
                                Text(step.scheduledDescription)
                                    .font(.caption.monospacedDigit())
                                    .foregroundStyle(.secondary)
                            }
                            .padding(.vertical, 3)
                        }
                    }
                }

                Section("ANCS identity") {
                    Text("The wording and cadence imitate Google Maps, but iOS identifies these notifications as Pedal One (`com.byobike.PedalOne`). Apps cannot spoof another app’s ANCS bundle identifier or force the ANCS Location category.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Text("Open ANCS TESTING on PedalOne before starting the route. That screen accepts the Pedal One test identity; normal ride navigation remains filtered to map apps.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("ANCS Test")
        }
    }

    private var permissionText: String {
        switch tester.permissionState {
        case .authorized: "Notifications allowed"
        case .denied: "Notifications disabled in Settings"
        case .unknown: "Notification permission not requested"
        }
    }

    private var permissionIcon: String {
        switch tester.permissionState {
        case .authorized: "checkmark.circle.fill"
        case .denied: "xmark.circle.fill"
        case .unknown: "questionmark.circle.fill"
        }
    }

    private var permissionColor: Color {
        switch tester.permissionState {
        case .authorized: .green
        case .denied: .red
        case .unknown: .orange
        }
    }
}
