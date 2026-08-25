import Charts
import MapKit
import SwiftUI

struct RideDetailDashboard: View {
    let ride: SavedRide

    @State private var mapPosition: MapCameraPosition = .automatic
    @State private var sheetOffset = 0.0
    @State private var selectedTimestamp: Date?
    @GestureState private var sheetDrag = 0.0

    private let columns = [
        GridItem(.flexible(), spacing: 10),
        GridItem(.flexible(), spacing: 10),
        GridItem(.flexible(), spacing: 10)
    ]

    var body: some View {
        GeometryReader { geometry in
            let expandedTop = min(340.0, max(270.0, geometry.size.height * 0.42))
            let collapsedTop = max(expandedTop, geometry.size.height - 128)
            let maximumOffset = collapsedTop - expandedTop
            let currentOffset = min(maximumOffset, max(0, sheetOffset + sheetDrag))
            let sheetTop = expandedTop + currentOffset

            ZStack(alignment: .top) {
                Color(.secondarySystemBackground)
                    .ignoresSafeArea()

                routeMap
                    .frame(maxWidth: .infinity)
                    .frame(height: sheetTop + 1)
                    .clipped()

                detailsSheet(maximumOffset: maximumOffset)
                    .frame(maxWidth: .infinity)
                    .frame(height: geometry.size.height - expandedTop, alignment: .top)
                    .offset(y: sheetTop)
            }
        }
        .navigationTitle("Ride Details")
        .navigationBarTitleDisplayMode(.inline)
        .toolbarBackground(.visible, for: .navigationBar)
        .onAppear { frameRoute() }
    }

    private var routeMap: some View {
        Map(position: $mapPosition) {
            if ride.points.count >= 2 {
                MapPolyline(coordinates: ride.points.map(\.coordinate))
                    .stroke(.teal, style: StrokeStyle(lineWidth: 5, lineCap: .round, lineJoin: .round))
            }
            if let start = ride.points.first {
                Marker("Start", systemImage: "flag.fill", coordinate: start.coordinate)
                    .tint(.green)
            }
            if let finish = ride.points.last {
                Marker("Finish", systemImage: "flag.checkered", coordinate: finish.coordinate)
                    .tint(.red)
            }
            if let selectedPoint {
                Annotation("Selected point", coordinate: selectedPoint.coordinate, anchor: .center) {
                    Circle()
                        .fill(.orange)
                        .stroke(.white, lineWidth: 3)
                        .frame(width: 17, height: 17)
                        .shadow(color: .black.opacity(0.35), radius: 3, y: 1)
                }
            }
        }
        .mapStyle(.standard(elevation: .realistic))
        .mapControls {
            MapCompass()
            MapScaleView()
        }
    }

    private func detailsSheet(maximumOffset: Double) -> some View {
        VStack(spacing: 0) {
            VStack(spacing: 7) {
                Capsule()
                    .fill(.secondary.opacity(0.55))
                    .frame(width: 42, height: 5)
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Ride Summary").font(.title2.bold())
                        Text(ride.date.formatted(date: .long, time: .shortened))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    Image(systemName: "arrow.up.and.down")
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 9)
            .padding(.bottom, 10)
            .contentShape(Rectangle())
            .gesture(sheetGesture(maximumOffset: maximumOffset))

            Divider()

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    LazyVGrid(columns: columns, spacing: 10) {
                        metric("Distance", String(format: "%.1f", ride.distanceMiles), "mi")
                        metric("Time", formattedDuration, nil)
                        metric("Avg speed", String(format: "%.1f", ride.averageSpeedMilesPerHour), "mph")
                        metric("Max speed", String(format: "%.1f", ride.maximumSpeedMilesPerHour), "mph")
                        metric("Climb", String(format: "%.0f", ride.climbFeet), "ft")
                        metric("GPS points", "\(ride.points.count)", nil)
                    }

                    SynchronizedRideChart(
                        title: "Speed",
                        unit: "mph",
                        color: .blue,
                        samples: speedSamples,
                        selectedTimestamp: $selectedTimestamp
                    )

                    SynchronizedRideChart(
                        title: "Elevation",
                        unit: "ft",
                        color: .teal,
                        samples: elevationSamples,
                        selectedTimestamp: $selectedTimestamp
                    )

                    SynchronizedRideChart(
                        title: "Grade",
                        unit: "%",
                        color: .orange,
                        samples: gradeSamples,
                        selectedTimestamp: $selectedTimestamp
                    )

                    Text("Drag across any chart to inspect the same point on all charts and the map.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(16)
            }
        }
        .background(.regularMaterial)
        .clipShape(UnevenRoundedRectangle(topLeadingRadius: 22, topTrailingRadius: 22))
        .shadow(color: .black.opacity(0.16), radius: 12, y: -3)
    }

    private func sheetGesture(maximumOffset: Double) -> some Gesture {
        DragGesture(minimumDistance: 2)
            .updating($sheetDrag) { value, state, _ in
                state = value.translation.height
            }
            .onEnded { value in
                let projected = min(maximumOffset, max(0, sheetOffset + value.predictedEndTranslation.height))
                withAnimation(.snappy(duration: 0.32)) {
                    sheetOffset = projected > maximumOffset * 0.42 ? maximumOffset : 0
                }
            }
    }

    private func frameRoute() {
        guard !ride.points.isEmpty else { return }
        var rect = MKMapRect.null
        for point in ride.points {
            let mapPoint = MKMapPoint(point.coordinate)
            rect = rect.union(MKMapRect(x: mapPoint.x, y: mapPoint.y, width: 1, height: 1))
        }
        let horizontalPadding = max(rect.size.width * 0.15, 500)
        let verticalPadding = max(rect.size.height * 0.15, 500)
        mapPosition = .rect(rect.insetBy(dx: -horizontalPadding, dy: -verticalPadding))
    }

    private var selectedPoint: RidePoint? {
        guard let selectedTimestamp else { return nil }
        return ride.points.min {
            abs($0.timestamp.timeIntervalSince(selectedTimestamp)) <
                abs($1.timestamp.timeIntervalSince(selectedTimestamp))
        }
    }

    private var speedSamples: [RideChartSample] {
        ride.points.map {
            RideChartSample(timestamp: $0.timestamp, value: $0.speedMetersPerSecond * 2.236_936)
        }
    }

    private var elevationSamples: [RideChartSample] {
        ride.points.map {
            RideChartSample(timestamp: $0.timestamp, value: $0.altitudeMeters * 3.280_84)
        }
    }

    private var gradeSamples: [RideChartSample] {
        guard ride.points.count >= 2 else { return [] }
        return ride.points.indices.map { index in
            // A centered window smooths GPS/barometer noise while preserving
            // the slope changes a rider expects to see.
            let lowerIndex = max(0, index - 2)
            let upperIndex = min(ride.points.count - 1, index + 2)
            let lower = ride.points[lowerIndex]
            let upper = ride.points[upperIndex]
            let horizontalDistance = routeDistance(from: lowerIndex, through: upperIndex)
            let rawGrade = horizontalDistance >= 5
                ? (upper.altitudeMeters - lower.altitudeMeters) / horizontalDistance * 100
                : 0
            return RideChartSample(
                timestamp: ride.points[index].timestamp,
                value: min(40, max(-40, rawGrade))
            )
        }
    }

    private func routeDistance(from lowerIndex: Int, through upperIndex: Int) -> Double {
        guard upperIndex > lowerIndex else { return 0 }
        return (lowerIndex..<upperIndex).reduce(0) { result, index in
            let first = ride.points[index]
            let second = ride.points[index + 1]
            return result + CLLocation(latitude: first.latitude, longitude: first.longitude)
                .distance(from: CLLocation(latitude: second.latitude, longitude: second.longitude))
        }
    }

    private func metric(_ title: String, _ value: String, _ unit: String?) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title.uppercased())
                .font(.caption2.weight(.semibold))
                .foregroundStyle(.secondary)
                .lineLimit(1)
            HStack(alignment: .firstTextBaseline, spacing: 3) {
                Text(value).font(.title3.bold()).minimumScaleFactor(0.7).lineLimit(1)
                if let unit { Text(unit).font(.caption).foregroundStyle(.secondary) }
            }
        }
        .frame(maxWidth: .infinity, minHeight: 58, alignment: .leading)
        .padding(10)
        .background(.background, in: RoundedRectangle(cornerRadius: 13))
    }

    private var formattedDuration: String {
        let seconds = max(0, Int(ride.duration))
        return String(format: "%d:%02d", seconds / 3600, (seconds / 60) % 60)
    }
}

private struct RideChartSample: Identifiable {
    var id: Date { timestamp }
    let timestamp: Date
    let value: Double
}

private struct SynchronizedRideChart: View {
    let title: String
    let unit: String
    let color: Color
    let samples: [RideChartSample]
    @Binding var selectedTimestamp: Date?

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(title).font(.headline)
                Spacer()
                if let selectedSample {
                    Text(selectedSample.value, format: .number.precision(.fractionLength(1)))
                        .font(.subheadline.bold())
                        .monospacedDigit()
                }
                Text(unit).font(.caption).foregroundStyle(.secondary)
            }

            Chart {
                ForEach(samples) { sample in
                    LineMark(
                        x: .value("Time", sample.timestamp),
                        y: .value(title, sample.value)
                    )
                    .foregroundStyle(color)
                    .lineStyle(StrokeStyle(lineWidth: 2))
                }

                if let selectedSample {
                    RuleMark(x: .value("Selected time", selectedSample.timestamp))
                        .foregroundStyle(.secondary)
                        .lineStyle(StrokeStyle(lineWidth: 1, dash: [3, 3]))
                    PointMark(
                        x: .value("Selected time", selectedSample.timestamp),
                        y: .value(title, selectedSample.value)
                    )
                    .foregroundStyle(color)
                    .symbolSize(70)
                }
            }
            .frame(height: 150)
            .chartXAxis(.hidden)
            .chartOverlay { proxy in
                GeometryReader { geometry in
                    Rectangle()
                        .fill(.clear)
                        .contentShape(Rectangle())
                        .gesture(
                            DragGesture(minimumDistance: 0)
                                .onChanged { value in
                                    guard let plotFrame = proxy.plotFrame else { return }
                                    let frame = geometry[plotFrame]
                                    let x = value.location.x - frame.origin.x
                                    guard x >= 0, x <= frame.width,
                                          let timestamp: Date = proxy.value(atX: x) else { return }
                                    selectedTimestamp = nearestSample(to: timestamp)?.timestamp
                                }
                        )
                }
            }
        }
        .padding(12)
        .background(.background, in: RoundedRectangle(cornerRadius: 15))
    }

    private var selectedSample: RideChartSample? {
        guard let selectedTimestamp else { return nil }
        return nearestSample(to: selectedTimestamp)
    }

    private func nearestSample(to timestamp: Date) -> RideChartSample? {
        samples.min {
            abs($0.timestamp.timeIntervalSince(timestamp)) <
                abs($1.timestamp.timeIntervalSince(timestamp))
        }
    }
}
