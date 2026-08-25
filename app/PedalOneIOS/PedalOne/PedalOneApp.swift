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
                try? await Task.sleep(for: .seconds(2.45))
                withAnimation(.easeOut(duration: 0.35)) {
                    isShowingSplash = false
                }
            }
        }
    }
}

private struct PedalOneSplashView: View {
    @State private var isMarkRevealed = false
    @State private var isWordmarkRevealed = false

    var body: some View {
        GeometryReader { proxy in
            let width = proxy.size.width
            let markSize = min(width * 0.62, 290)

            ZStack {
                Color.black.ignoresSafeArea()

                VStack(spacing: 18) {
                    PedalOneMark()
                        .frame(width: markSize, height: markSize)
                        .scaleEffect(isMarkRevealed ? 1 : 0.72)
                        .rotationEffect(.degrees(isMarkRevealed ? 0 : -18))
                        .opacity(isMarkRevealed ? 1 : 0)

                    VStack(spacing: 1) {
                        Text("PEDAL")
                            .foregroundStyle(.white)
                        Text("ONE")
                            .foregroundStyle(
                                LinearGradient(
                                    colors: [Color(red: 0.75, green: 1, blue: 0), Color(red: 0, green: 0.84, blue: 0.82)],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                )
                            )
                    }
                    .font(.system(size: 34, weight: .semibold, design: .rounded))
                    .tracking(8)
                    .padding(.leading, 8)
                    .opacity(isWordmarkRevealed ? 1 : 0)
                    .offset(y: isWordmarkRevealed ? 0 : 12)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.bottom, proxy.size.height * 0.04)
            }
        }
        .accessibilityHidden(true)
        .onAppear {
            withAnimation(.smooth(duration: 0.8)) {
                isMarkRevealed = true
            }
            withAnimation(.easeOut(duration: 0.42).delay(0.58)) {
                isWordmarkRevealed = true
            }
        }
    }
}

private struct PedalOneMark: View {
    private let lime = Color(red: 0.75, green: 1, blue: 0)
    private let cyan = Color(red: 0, green: 0.84, blue: 0.82)

    private var ringGradient: AngularGradient {
        AngularGradient(
            colors: [lime, lime, Color(red: 0.35, green: 0.95, blue: 0.30), cyan, lime],
            center: .center,
            startAngle: .degrees(-90),
            endAngle: .degrees(270)
        )
    }

    var body: some View {
        GeometryReader { proxy in
            let stroke = proxy.size.width * 0.068
            let inset = stroke * 0.65

            ZStack {
                ForEach([0.105...0.235, 0.275...0.485, 0.525...0.735, 0.775...0.975], id: \.lowerBound) { segment in
                    Circle()
                        .trim(from: segment.lowerBound, to: segment.upperBound)
                        .stroke(ringGradient, style: StrokeStyle(lineWidth: stroke, lineCap: .butt))
                        .rotationEffect(.degrees(-90))
                        .padding(inset)
                }

                Circle()
                    .trim(from: 0.025, to: 0.075)
                    .stroke(.white, style: StrokeStyle(lineWidth: stroke, lineCap: .butt))
                    .rotationEffect(.degrees(-90))
                    .padding(inset)

                PedalOnePShape()
                    .stroke(
                        LinearGradient(colors: [lime, Color(red: 0.55, green: 1, blue: 0.03), cyan],
                                       startPoint: .topLeading, endPoint: .bottomTrailing),
                        style: StrokeStyle(lineWidth: proxy.size.width * 0.14,
                                           lineCap: .round, lineJoin: .round)
                    )

                PedalOneArrowhead()
                    .fill(LinearGradient(colors: [lime, Color(red: 0.6, green: 1, blue: 0.04)],
                                         startPoint: .leading, endPoint: .trailing))
            }
        }
        .aspectRatio(1, contentMode: .fit)
    }
}

private struct PedalOnePShape: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.width * 0.31, y: rect.height * 0.36))
        path.addLine(to: CGPoint(x: rect.width * 0.64, y: rect.height * 0.36))
        path.addCurve(
            to: CGPoint(x: rect.width * 0.64, y: rect.height * 0.56),
            control1: CGPoint(x: rect.width * 0.79, y: rect.height * 0.36),
            control2: CGPoint(x: rect.width * 0.79, y: rect.height * 0.56)
        )
        path.addLine(to: CGPoint(x: rect.width * 0.51, y: rect.height * 0.56))
        path.addCurve(
            to: CGPoint(x: rect.width * 0.40, y: rect.height * 0.68),
            control1: CGPoint(x: rect.width * 0.44, y: rect.height * 0.56),
            control2: CGPoint(x: rect.width * 0.40, y: rect.height * 0.61)
        )
        path.addLine(to: CGPoint(x: rect.width * 0.40, y: rect.height * 0.78))
        return path
    }
}

private struct PedalOneArrowhead: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.width * 0.18, y: rect.height * 0.36))
        path.addLine(to: CGPoint(x: rect.width * 0.36, y: rect.height * 0.23))
        path.addLine(to: CGPoint(x: rect.width * 0.36, y: rect.height * 0.49))
        path.closeSubpath()
        return path
    }
}

private struct PedalOneWordmark: View {
    var body: some View {
        HStack(spacing: 8) {
            Text("PEDAL")
                .tracking(4)

            ZStack {
                Circle().stroke(.white, lineWidth: 3)
                Circle()
                    .trim(from: 0.04, to: 0.22)
                    .stroke(Color(red: 0.75, green: 1, blue: 0), lineWidth: 4)
                    .rotationEffect(.degrees(-90))
                Circle()
                    .trim(from: 0.53, to: 0.72)
                    .stroke(Color(red: 0.33, green: 0.96, blue: 0.32), lineWidth: 4)
                    .rotationEffect(.degrees(-90))
            }
            .frame(width: 37, height: 37)

            Text("NE")
                .tracking(4)
        }
        .font(.system(size: 40, weight: .medium, design: .rounded))
        .foregroundStyle(.white)
        .minimumScaleFactor(0.72)
        .lineLimit(1)
    }
}

private struct PedalOneTerrainWave: View {
    var body: some View {
        Canvas { context, size in
            let rows = 22
            let columns = 48

            for row in 0..<rows {
                for column in 0..<columns {
                    let xAmount = Double(column) / Double(columns - 1)
                    let rowAmount = Double(row) / Double(rows - 1)
                    let x = size.width * xAmount
                    let crossingWave = sin(xAmount * .pi * 2.15 + rowAmount * 1.8)
                    let broadWave = sin(xAmount * .pi * 1.15 - 0.7)
                    let y = size.height * (0.25 + rowAmount * 0.68)
                        + size.height * 0.105 * crossingWave
                        + size.height * 0.07 * broadWave
                    let dotSize = 1.4 + 0.9 * rowAmount
                    let color = Color(
                        red: 0.72 * (1 - xAmount),
                        green: 0.92,
                        blue: 0.92 * xAmount
                    )
                    let dot = CGRect(x: x - dotSize / 2, y: y - dotSize / 2,
                                     width: dotSize, height: dotSize)
                    context.fill(Path(ellipseIn: dot), with: .color(color.opacity(0.92)))
                }
            }
        }
        .allowsHitTesting(false)
    }
}
