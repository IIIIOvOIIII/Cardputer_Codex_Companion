import Foundation
import XCTest
@testable import ProductContracts

final class PairingMigrationTests: XCTestCase {
    func testWriterPreservesUnrelatedKeysAndRejectsOldRevision() throws {
        let directory = FileManager.default.temporaryDirectory
            .appending(path: UUID().uuidString)
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: false
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appending(path: "config.json")
        try Data(
            """
            {"device":"https://192.168.1.2","pairing":"12345678",
             "pin_revision":7,"unrelated":true}
            """.utf8
        ).write(to: url)
        try PairingConfigWriter.persist(
            PairingMigration(nextPairing: "87654321", pinRevision: 8),
            to: url
        )
        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(
                with: Data(contentsOf: url)
            ) as? [String: Any]
        )
        XCTAssertEqual(object["pairing"] as? String, "87654321")
        XCTAssertEqual(object["unrelated"] as? Bool, true)
        try PairingConfigWriter.persist(
            PairingMigration(nextPairing: "11111111", pinRevision: 8),
            to: url
        )
        object = try XCTUnwrap(
            JSONSerialization.jsonObject(
                with: Data(contentsOf: url)
            ) as? [String: Any]
        )
        XCTAssertEqual(object["pairing"] as? String, "87654321")
        let mode = try XCTUnwrap(
            FileManager.default.attributesOfItem(atPath: url.path)[
                .posixPermissions
            ] as? NSNumber
        )
        XCTAssertEqual(mode.intValue, 0o600)
        XCTAssertEqual(
            try FileManager.default.contentsOfDirectory(atPath: directory.path),
            ["config.json"]
        )
    }

    func testWriterRejectsUnicodeDigits() throws {
        let url = FileManager.default.temporaryDirectory
            .appending(path: UUID().uuidString)
        try Data("{}".utf8).write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        XCTAssertThrowsError(
            try PairingConfigWriter.persist(
                PairingMigration(
                    nextPairing: "１２３４５６７８",
                    pinRevision: 1
                ),
                to: url
            )
        )
    }
}
