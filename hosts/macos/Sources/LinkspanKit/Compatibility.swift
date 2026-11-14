// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Foundation

public struct MatrixCell: Decodable, Equatable {
    public let host: String
    public let guest: String
    public let level: String
    public let title: String
    public let summary: String
    public let legalNote: String
}

public struct LimitNote: Decodable, Equatable {
    public let id: String
    public let title: String
    public let body: String
}

public struct CompatibilityDocument: Decodable, Equatable {
    public let schema: Int
    public let product: String
    public let license: String
    public let cells: [MatrixCell]
    public let limits: [LimitNote]

    public static func load(from data: Data) throws -> CompatibilityDocument {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return try decoder.decode(CompatibilityDocument.self, from: data)
    }

    public func cell(host: String, guest: String) -> MatrixCell? {
        cells.first { $0.host == host && $0.guest == guest }
    }
}
