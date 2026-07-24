// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "CardputerPhase0MacProbe",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "Phase0Contracts", targets: ["Phase0Contracts"]),
        .library(name: "Phase0Ledger", targets: ["Phase0Ledger"]),
        .library(name: "Phase0Security", targets: ["Phase0Security"]),
        .library(name: "Phase0GATT", targets: ["Phase0GATT"]),
        .library(name: "Phase0Unicode", targets: ["Phase0Unicode"]),
        .executable(name: "cardputer-phase0-probe", targets: ["cardputer-phase0-probe"])
    ],
    targets: [
        .systemLibrary(name: "CSQLite", pkgConfig: "sqlite3"),
        .target(name: "Phase0Contracts"),
        .target(
            name: "Phase0Ledger",
            dependencies: ["Phase0Contracts", "CSQLite"]
        ),
        .target(
            name: "Phase0Security",
            dependencies: ["Phase0Contracts"],
            linkerSettings: [
                .linkedFramework("Network"),
                .linkedFramework("Security")
            ]
        ),
        .target(
            name: "Phase0GATT",
            dependencies: ["Phase0Contracts", "Phase0Ledger", "Phase0Security"],
            linkerSettings: [
                .linkedFramework("CoreBluetooth"),
                .linkedFramework("IOKit")
            ]
        ),
        .target(
            name: "Phase0Unicode",
            dependencies: ["Phase0Contracts", "Phase0Ledger"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("ApplicationServices"),
                .linkedFramework("Carbon")
            ]
        ),
        .executableTarget(
            name: "cardputer-phase0-probe",
            dependencies: [
                "Phase0Contracts",
                "Phase0Ledger",
                "Phase0Security",
                "Phase0GATT",
                "Phase0Unicode"
            ]
        ),
        .testTarget(name: "Phase0ContractsTests", dependencies: ["Phase0Contracts"]),
        .testTarget(name: "Phase0LedgerTests", dependencies: ["Phase0Ledger"]),
        .testTarget(name: "Phase0SecurityTests", dependencies: ["Phase0Security"]),
        .testTarget(name: "Phase0GATTTests", dependencies: ["Phase0GATT"]),
        .testTarget(name: "Phase0UnicodeTests", dependencies: ["Phase0Unicode"])
    ]
)
