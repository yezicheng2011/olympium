// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import LinkspanKit
import SwiftUI

enum AppPage: String, CaseIterable, Identifiable {
    case overview
    case programs
    case compatibility
    case limits

    var id: String { rawValue }

    var title: String {
        switch self {
        case .overview: return "Overview"
        case .programs: return "Programs"
        case .compatibility: return "Compatibility"
        case .limits: return "Limits"
        }
    }
}

struct ContentView: View {
    @AppStorage("linkspan.appearance") private var appearance = "system"
    @State private var page: AppPage? = .overview
    @StateObject private var model = LibraryModel()

    private var colorScheme: ColorScheme? {
        switch appearance {
        case "light": return .light
        case "dark": return .dark
        default: return nil
        }
    }

    var body: some View {
        NavigationSplitView {
            List(AppPage.allCases, selection: $page) { item in
                Text(item.title).tag(item)
            }
            .navigationTitle("Linkspan")
            .safeAreaInset(edge: .bottom) {
                Picker("Appearance", selection: $appearance) {
                    Text("Follow system").tag("system")
                    Text("Light").tag("light")
                    Text("Dark").tag("dark")
                }
                .pickerStyle(.menu)
                .padding()
            }
        } detail: {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    detail
                }
                .frame(maxWidth: 760, alignment: .leading)
                .padding(28)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .navigationTitle(page?.title ?? "Linkspan")
        }
        .preferredColorScheme(colorScheme)
        .onAppear { model.load() }
    }

    @ViewBuilder
    private var detail: some View {
        switch page ?? .overview {
        case .overview:
            Text("Programs from other systems, on this Mac.")
                .font(.title2)
            Text("Mac apps, disk images, and installer packages open with macOS. Scripts run here too. Other Linux programs use Lima when you have it. Windows programs use Wine, which opens a native window and is kept for Windows guests. Android APKs use adb or Waydroid if you installed one. OpenHarmony packages run in a real OpenHarmony runtime after the slim image is downloaded. iOS apps that are already on this Mac can be opened. Linkspan does not install IPA files.")
            Text(model.engineLine)
                .foregroundStyle(.secondary)
        case .programs:
            Text("Programs")
                .font(.title2)
            Text("Add a program with `linkspan add --file <path>` in Terminal, then run it with `linkspan run <id>`.")
            if model.programs.isEmpty {
                Text(model.programStatus)
                    .foregroundStyle(.secondary)
            } else {
                ForEach(model.programs, id: \.id) { program in
                    VStack(alignment: .leading, spacing: 4) {
                        Text(program.name)
                        Text(program.note)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        case .compatibility:
            Text("Compatibility")
                .font(.title2)
            ForEach(model.cells, id: \.identity) { cell in
                VStack(alignment: .leading, spacing: 4) {
                    Text("\(cell.hostLabel) / \(cell.guest) — \(cell.title)")
                    Text(cell.summary)
                        .foregroundStyle(.secondary)
                }
            }
        case .limits:
            Text("Limits")
                .font(.title2)
            ForEach(model.limits, id: \.id) { limit in
                VStack(alignment: .leading, spacing: 4) {
                    Text(limit.title).font(.headline)
                    Text(limit.body)
                }
            }
        }
    }
}

struct ProgramRow: Identifiable {
    let id: String
    let name: String
    let note: String
}

struct CellRow: Identifiable {
    var identity: String { host + guest }
    let host: String
    let hostLabel: String
    let guest: String
    let level: String
    let title: String
    let summary: String
}
