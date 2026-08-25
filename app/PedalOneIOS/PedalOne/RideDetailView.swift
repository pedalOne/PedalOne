import Charts
import MapKit
import SwiftUI

struct RideDetailView: View {
    let ride: SavedRide
    @State private var mapPosition: MapCameraPosition = .automatic

    private let columns = [
        GridItem(.flexible(), spacing: 10),
        GridItem(.flexible(), spacing: 10),
        GridItem(.flexible(), spacing: 10)
    ]

    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                routeMap
                    .frame(height: 360)

                VStack(alignment: .leading, spacing: 16) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Ride Summary").font(.title2.bold())
                        Text(ride.date.formatted(date: .long, time: .shortened))
                            .font(.subheadline).foregroundStyle(.secondary)
                    }

                    LazyVGrid(columns: columns, spacing: 10) {
                        metric("Distance", String(format: "%.1f", ride.distanceMiles), "mi")
                        metric("Time", formattedDuration, nil)
                        metric("Avg speed", String(format: "%.1f", ride.averageSpeedMilesPerHour), "mph")
                        metric("Max speed", String(format: "%.1f", ride.maximumSpeedMilesPerHour), "mph")
                        metric("Climb", String(format: "%.0f", ride.climbFeet), "ft")
                        metric("GPS points", "\(ride.points.count)", nil)
                    }

                    chartSection("Elevation", unit: "ft") {
                        Chart(ride.points) { point in
                            AreaMark(
                                x: .value("Time", point.timestamp),
                                y: .value("Elevation", point.altitudeMeters * 3.280_84)
                            )
                            .foregroundStyle(.teal.opacity(0.16))
                            LineMark(
                                x: .value("Time", point.timestamp),
                                y: .value("Elevation", point.altitudeMeters * 3.280_84)
                            )
                            .foregroundStyle(.teal)
                            .lineStyle(StrokeStyle(lineWidth: 2))
                        }
                    }

                    chartSection("Speed", unit: "mph") {
                        Chart(ride.points) { point in
                            LineMark(
                                x: .value("Time", point.timestamp),
                                y: .value("Speed", point.speedMetersPerSecond * 2.236_936)
                            )
                            .foregroundStyle(.blue)
                            .lineStyle(StrokeStyle(lineWidth: 2))
                        }
                    }
                }
                .padding(16)
            }
        }
        .ignoresSafeArea(edges: .top)
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
        }
        .mapStyle(.standard(elevation: .realistic))
        .mapControls {
            MapCompass()
            MapScaleView()
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

    private func chartSection<Content: View>(
        _ title: String,
        unit: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(title).font(.headline)
                Spacer()
                Text(unit).font(.caption).foregroundStyle(.secondary)
            }
            content()
                .frame(height: 150)
                .chartXAxis(.hidden)
        }
        .padding(12)
        .background(.background, in: RoundedRectangle(cornerRadius: 15))
    }

    private var formattedDuration: String {
        let seconds = max(0, Int(ride.duration))
        return String(format: "%d:%02d", seconds / 3600, (seconds / 60) % 60)
    }
}
