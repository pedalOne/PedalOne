import SwiftUI

@main
struct PedalOneApp: App {
    @StateObject private var relay = RelayController()
    @State private var isShowingSplash = true

    var body: some Scene {
        WindowGroup {
            ZStack {
                ContentView()
                    .environmentObject(relay)
                    .environmentObject(relay.rides)

                if isShowingSplash {
                    PedalOneSplashView()
                        .transition(.opacity)
                        .zIndex(1)
                }
            }
            .task {
                guard isShowingSplash else { return }
                try? await Task.sleep(for: .seconds(2.6))
                withAnimation(.easeOut(duration: 0.45)) {
                    isShowingSplash = false
                }
            }
        }
    }
}

private struct PedalOneSplashView: View {
    @State private var isLogoRevealed = false

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            Image("PedalOneLogo")
                .resizable()
                .scaledToFit()
                .frame(width: 280, height: 280)
                .scaleEffect(isLogoRevealed ? 1 : 0.18)
                .opacity(isLogoRevealed ? 1 : 0)
        }
        .accessibilityHidden(true)
        .onAppear {
            withAnimation(.smooth(duration: 1.7)) {
                isLogoRevealed = true
            }
        }
    }
}
