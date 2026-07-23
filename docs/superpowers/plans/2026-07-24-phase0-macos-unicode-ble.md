# Phase 0 macOS Unicode、BLE 与配对安全探针 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立一个可重复运行、可产生机器可校验证据的原生 macOS 探针，验证中文 Unicode 注入、真实 CoreBluetooth GATT 通知、HID/GATT 同设备身份、SQLite 崩溃账本、SAS/WSS channel binding、双通道 bind challenge 与指定局域网接口约束是否满足 Phase 0 硬门槛。

**Architecture:** 使用 SwiftPM 将契约、SQLite 账本、配对安全、CoreBluetooth GATT 和 Unicode 注入拆成独立 target，并由一个真实签名、`LSUIElement` 的 `.app` 承载所有需要 TCC、AX、CoreBluetooth 和 Network.framework 的真机路径。单元测试使用依赖注入验证顺序与失败语义；`scripts/run_macos_hil.py` 只编排真实目标应用、签名 A/B 升级和 Cardputer 真机流程，并将脱敏结果写成受 JSON Schema 约束的证据。

**Tech Stack:** Swift 6、Swift Package Manager、Foundation、AppKit、ApplicationServices、Carbon/HIToolbox、CoreBluetooth、IOKit HID、Network.framework、Security.framework、CryptoKit、SQLite3、Python 3.11+、JSON Schema Draft 2020-12、codesign。

## Global Constraints

- 本计划只实现和验证 Phase 0 macOS Gate 2，以及 Gate 1、Gate 6 中由 Companion 负责的部分；任一硬门槛失败时状态必须是 `fail` 并返回设计复核。
- 支持的目标平台仅为 macOS；探针的 SwiftPM deployment target 固定为 macOS 14。
- 单次 UTF-8 文本最多 1024 字节；分块不得切断 UTF-8 scalar 或 UTF-16 surrogate pair。
- Unicode 注入只允许 `CGEvent.keyboardSetUnicodeString`；禁止读取或写入剪贴板，禁止合成 Command-V，禁止根据键盘布局转换中文。
- 注入开始时绑定前台 PID 和聚焦 AX 元素；每块之前重新验证。焦点变化只允许返回 `partial`，已投递但无法证明结果的崩溃窗口只允许返回 `indeterminate`。
- `posted_prefix_length` 和 `verified_prefix_length` 均以原始 UTF-8 字节计；`posted` 不得被解释为目标应用已经接受。
- GATT 必须运行在已加密、已绑定的 BLE 连接上；每次连接使用新的 128-bit `connection_id`，计数器从 0 开始。
- GATT 接收顺序固定为：仅做有界定长解析、核对 frame 内 `connection_id`、验证 MAC、更新 32-entry replay window、重组、检查或写入账本、调用 Unicode Injector。MAC 失败不得改变 replay、重组或账本状态。
- Companion 配对使用长期 P-256 身份、临时 P-256 ECDH、SHA-256 transcript、HKDF-SHA256 和 6 位 SAS；具体字节序、字段顺序、label 和签名编码只以 `protocol/phase0/*.md` 及 `protocol/phase0/fixtures/*.json` 为准，Swift 不得另定义第二套协议。
- WSS 客户端证明必须签署 `TLS exporter + Companion instance ID + device ID + protocol version + 256-bit random challenge`，且签名验证使用配对时固定的设备长期 P-256 公钥。
- 初次 Companion 绑定必须在 60 秒内由同一设备身份经 WSS 与 GATT 返回相同一次性 challenge；任一通道缺失、超时、身份不同或 challenge 不同都不得建立绑定。
- Companion 只在用户显式选择的本地 LAN 接口发布和监听；接口消失或改变后必须转为 `blocked` 并要求 Mac 端重新确认，不能自动改用其他接口。
- v1 只有一个活动 HID 主机 bond。Phase 0 将 HID serial 解码为 base32 的原始 16-byte `device_id`，并与受保护 GATT Identity Characteristic 返回的原始 16 bytes 精确比较；不使用 Feature Report、设备名或随机 BLE 地址作身份依据。
- 所有 SQLite 账本只保存 payload SHA-256、目标 PID/AX 指纹、前缀长度、状态和时间，不保存 UTF-8 正文。
- 真机证据必须来自真实签名 `.app`。adhoc 签名、未签名二进制、模拟 BLE 回调或人工填写 JSON 均不能使硬门槛通过。
- 不自动调用 `tccutil reset`，不自动打开或修改 Secure Input，不自动批准 TCC 权限；这些步骤由 HIL runner 给出精确人工检查点。
- HIL 缺少 Apple Development/Developer ID 签名身份、Cardputer、BLE bond、目标应用、Accessibility 权限或被选 LAN 接口时，结果只能为 `blocked`。
- 证据不得记录注入正文、设备长期公钥、配对根密钥、`gatt-auth` 密钥、完整 HID serial 或完整 device ID；只记录 SHA-256 摘要和非敏感状态。
- 源规格为 `docs/superpowers/specs/2026-07-24-cardputer-codex-companion-design.md` 第 11、13、15.2、18.1、18.3、18.4 节。

---

## Scope Boundary

本计划产出的是可抛弃的 Phase 0 feasibility probe，不是 Phase 2/3 产品 Companion。它不会创建 LaunchAgent、菜单栏产品 UI、正式 Keychain 迁移、生产安装包或完整 Codex Adapter。只有本计划和同阶段固件探针都产生 `pass` 证据后，才编写后续产品实施计划。

## Gate Traceability

| 硬门槛 | 自动化证据 | 必须的真机证据 |
|---|---|---|
| Gate 1：HID 与 GATT 同设备 | base32 HID serial/GATT raw `device_id` 比较、GATT receiver 顺序测试 | macOS IOHID 真实 serial、CoreBluetooth 真实 notify、同一 16-byte `device_id` |
| Gate 2：中文注入 | 1024-byte 分块、AX focus、Secure Input、账本恢复测试 | TextEdit、VS Code、Chrome、Terminal、iTerm2 精确 AX 读回；焦点切换；Secure Input；Companion 崩溃 |
| Gate 6：SAS/WSS/GATT | transcript、HKDF label、TLS exporter material、replay、bind challenge 单元测试 | 双端 SAS 一致、真实 TLS exporter 签名验证、WSS/GATT 同 challenge、签名 `.app` TCC A/B 保持 |
| LAN-only 边界 | 子网与接口 policy 单元测试 | listener/mDNS 只绑定用户选定接口；接口切换后停止并要求确认 |

## Locked File Map

下列路径是本子计划的完整写入边界；执行者不得把探针逻辑散落到固件或 Web 目录。

```text
companion/
├── Package.swift
├── Sources/
│   ├── CSQLite/
│   │   ├── module.modulemap
│   │   └── shim.h
│   ├── Phase0Contracts/
│   │   ├── Errors.swift
│   │   ├── Evidence.swift
│   │   └── TextOperation.swift
│   ├── Phase0Ledger/
│   │   └── SQLiteTextOperationLedger.swift
│   ├── Phase0Security/
│   │   ├── PairingDerivation.swift
│   │   ├── TLSChannelBinding.swift
│   │   ├── BindChallengeCoordinator.swift
│   │   ├── LANInterfacePolicy.swift
│   │   └── WSSPairingProbe.swift
│   ├── Phase0GATT/
│   │   ├── BluetoothProbeSession.swift
│   │   ├── GATTFrame.swift
│   │   ├── ReplayWindow.swift
│   │   ├── GATTFrameReceiver.swift
│   │   ├── CoreBluetoothProbeClient.swift
│   │   └── HIDIdentityReader.swift
│   ├── Phase0Unicode/
│   │   ├── UTF8Chunker.swift
│   │   ├── AXFocusGuard.swift
│   │   ├── CGUnicodePoster.swift
│   │   └── UnicodeInjectionEngine.swift
│   └── cardputer-phase0-probe/
│       ├── ConcurrencyHILAgent.swift
│       ├── PairGATTHILCommand.swift
│       └── main.swift
├── Tests/
│   ├── Phase0ContractsTests/ContractTests.swift
│   ├── Phase0LedgerTests/SQLiteTextOperationLedgerTests.swift
│   ├── Phase0SecurityTests/
│   │   ├── PairingDerivationTests.swift
│   │   ├── TLSChannelBindingTests.swift
│   │   ├── BindChallengeCoordinatorTests.swift
│   │   ├── LANInterfacePolicyTests.swift
│   │   └── ConcurrencyHILAgentTests.swift
│   ├── Phase0GATTTests/
│   │   ├── ReplayWindowTests.swift
│   │   ├── GATTFrameReceiverTests.swift
│   │   └── IdentityBindingTests.swift
│   └── Phase0UnicodeTests/
│       ├── UTF8ChunkerTests.swift
│       └── UnicodeInjectionEngineTests.swift
├── AppBundle/
│   ├── Info.plist
│   └── CardputerPhase0Probe.entitlements
scripts/
├── build_signed_macos_probe.sh
├── check_macos_injection_policy.py
├── run_macos_hil.py
├── test_build_signed_macos_probe.py
└── test_run_macos_hil.py
docs/validation/phase0/
├── macos-hil.schema.json
├── macos-hil-operator.md
└── macos-hil.md
```

SwiftPM target 图固定如下，后续任务不得重命名：

```text
CSQLite (system library)
Phase0Contracts
Phase0Ledger -> Phase0Contracts, CSQLite
Phase0Security -> Phase0Contracts
Phase0GATT -> Phase0Contracts, Phase0Ledger, Phase0Security
Phase0Unicode -> Phase0Contracts, Phase0Ledger
cardputer-phase0-probe -> all five Swift library targets
Phase0ContractsTests -> Phase0Contracts
Phase0LedgerTests -> Phase0Ledger
Phase0SecurityTests -> Phase0Security
Phase0GATTTests -> Phase0GATT
Phase0UnicodeTests -> Phase0Unicode
```

## Fixed Probe Protocol

固件、macOS 与 Python 只共享 foundation Task 2 生成的协议文档和 fixtures。以下摘要便于实现者定位，但如有任何差异，必须停止并修正本计划，不能在 Swift 中“兼容”第二种编码：

```text
GATT service UUID:          7a100001-2c4d-4f20-9f20-434f44455831
GATT frame notify UUID:     7a100002-2c4d-4f20-9f20-434f44455831
GATT control write UUID:    7a100003-2c4d-4f20-9f20-434f44455831
GATT identity read UUID:    7a100004-2c4d-4f20-9f20-434f44455831
HID identity source:        base32 HID serial
Identity value:             raw 16-byte device_id
GATT protocol version:      1
GATT auth label:            "cardputer-codex/gatt-auth/v1"
TLS exporter label:         "EXPORTER-Cardputer-Codex-Companion-v1"
TLS exporter context:       empty
Pairing prefix/version:     "CCP-PAIR" || 0x0001
Pairing root label:         "cardputer-codex/pair-root/v1"
Pairing GATT label:         "cardputer-codex/gatt-auth/v1"
Pairing SAS label:          "cardputer-codex/sas/v1"
WSS protocol version:       "1.0"
WSS signature encoding:     64-byte fixed-width r || s
Bind challenge lifetime:    60 seconds
Replay window:              32 counters
Maximum GATT message:       1024 bytes
Maximum GATT fragments:     64
Maximum GATT frame:         512 bytes
```

GATT notify frame采用网络字节序：

```text
offset  size  field
0       1     version
1       1     flags
2       16    connection_id
18      16    operation_id
34      8     counter
42      2     fragment_index
44      2     fragment_count
46      4     total_utf8_length
50      32    full_message_sha256
82      2     fragment_length
84      N     fragment bytes
84+N    16    HMAC-SHA256 tag truncated to 16 bytes
```

HMAC 输入固定为：

```text
"cardputer-codex/gatt-auth/v1" UTF-8
|| frame bytes from offset 0 through fragment bytes
```

Pairing transcript 使用 `CCP-PAIR || 0x0001`，随后按 device→Companion 角色顺序编码 LP16 device ID、LP16 Companion instance ID、LP16 protocol version、双方长期/临时 SEC1 公钥及双方 32-byte nonce。WSS 签名材料是 exporter、Companion instance ID、device ID、protocol version 和 32-byte challenge 五个 LP16 字段；Swift 单元测试必须逐字节匹配 foundation fixtures。

## Task 1: 建立 SwiftPM target 图与稳定契约

**Files:**
- Create: `companion/Package.swift`
- Create: `companion/Sources/CSQLite/module.modulemap`
- Create: `companion/Sources/CSQLite/shim.h`
- Create: `companion/Sources/Phase0Contracts/Errors.swift`
- Create: `companion/Sources/Phase0Contracts/Evidence.swift`
- Create: `companion/Sources/Phase0Contracts/TextOperation.swift`
- Create: `companion/Tests/Phase0ContractsTests/ContractTests.swift`
- Create: `companion/Tests/Phase0LedgerTests/ModuleSurfaceTests.swift`
- Create: `companion/Tests/Phase0SecurityTests/ModuleSurfaceTests.swift`
- Create: `companion/Tests/Phase0GATTTests/ModuleSurfaceTests.swift`
- Create: `companion/Tests/Phase0UnicodeTests/ModuleSurfaceTests.swift`

**Interfaces:**
- Consumes: 本文 “Fixed Probe Protocol” 中的版本、长度和状态约束。
- Produces: `StableErrorCode`、`OperationStatus`、`TextOperationRecord`、`InjectionResult`、`GateStatus`、`HILEvidence`，供所有后续 target 与 Python schema 使用。

- [ ] **Step 1: 写入失败的契约测试**

创建 `companion/Tests/Phase0ContractsTests/ContractTests.swift`：

```swift
import XCTest
@testable import Phase0Contracts

final class ContractTests: XCTestCase {
    func testTerminalAndReplaySemanticsAreStable() throws {
        XCTAssertTrue(OperationStatus.completed.isTerminal)
        XCTAssertTrue(OperationStatus.failed.isTerminal)
        XCTAssertTrue(OperationStatus.partial.isTerminal)
        XCTAssertTrue(OperationStatus.indeterminate.isTerminal)
        XCTAssertFalse(OperationStatus.intent.isTerminal)
        XCTAssertFalse(OperationStatus.accepted.isTerminal)
        XCTAssertEqual(StableErrorCode.secureInputActive.rawValue, "secure_input_active")
        XCTAssertEqual(StableErrorCode.permissionDenied.rawValue, "permission_denied")
        XCTAssertEqual(StableErrorCode.invalidRequest.rawValue, "invalid_request")
    }

    func testTextRecordRejectsImpossiblePrefixLengths() throws {
        XCTAssertThrowsError(
            try TextOperationRecord(
                pairedDeviceID: "device-a",
                operationID: UUID(),
                payloadSHA256: Data(repeating: 1, count: 32),
                targetPID: 42,
                targetElementFingerprint: "ax:field",
                totalUTF8Length: 12,
                postedPrefixLength: 8,
                verifiedPrefixLength: 9,
                status: .intent,
                errorCode: nil,
                createdAt: Date(timeIntervalSince1970: 1),
                updatedAt: Date(timeIntervalSince1970: 1),
                expiresAt: Date(timeIntervalSince1970: 601)
            )
        )
    }

    func testEvidenceCannotPassWithBlockedChecks() throws {
        let evidence = HILEvidence(
            schemaVersion: "1.0",
            runID: UUID(),
            startedAt: Date(timeIntervalSince1970: 1),
            completedAt: Date(timeIntervalSince1970: 2),
            checks: [
                GateCheck(id: "signed_app", status: .pass, detail: "valid"),
                GateCheck(id: "cardputer", status: .blocked, detail: "not connected")
            ]
        )
        XCTAssertEqual(evidence.overallStatus, .blocked)
    }
}
```

- [ ] **Step 2: 运行测试确认契约尚不存在**

Run:

```bash
swift test --package-path companion --filter ContractTests
```

Expected: FAIL，错误包含 `Could not find Package.swift` 或 `no such module 'Phase0Contracts'`。

- [ ] **Step 3: 写入 Package manifest 和稳定 DTO**

创建 `companion/Package.swift`：

```swift
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
```

创建 `companion/Sources/CSQLite/module.modulemap`：

```text
module CSQLite [system] {
    header "shim.h"
    link "sqlite3"
    export *
}
```

创建 `companion/Sources/CSQLite/shim.h`：

```c
#include <sqlite3.h>
```

创建 `companion/Sources/Phase0Contracts/Errors.swift`：

```swift
import Foundation

public enum StableErrorCode: String, Codable, Sendable, Error {
    case invalidRequest = "invalid_request"
    case unauthenticated
    case forbidden
    case permissionDenied = "permission_denied"
    case secureInputActive = "secure_input_active"
    case partial
    case indeterminate
    case resultExpired = "result_expired"
    case timeout
    case focusChanged = "focus_changed"
    case replay
    case malformedFrame = "malformed_frame"
    case identityMismatch = "identity_mismatch"
    case interfaceChanged = "interface_changed"
}

public enum GateStatus: String, Codable, Sendable {
    case pass
    case fail
    case blocked
}
```

创建 `companion/Sources/Phase0Contracts/TextOperation.swift`：

```swift
import Foundation

public enum OperationStatus: String, Codable, Sendable {
    case intent
    case accepted
    case completed
    case failed
    case partial
    case indeterminate

    public var isTerminal: Bool {
        switch self {
        case .completed, .failed, .partial, .indeterminate:
            return true
        case .intent, .accepted:
            return false
        }
    }
}

public struct TextOperationRecord: Codable, Equatable, Sendable {
    public let pairedDeviceID: String
    public let operationID: UUID
    public let payloadSHA256: Data
    public let targetPID: pid_t
    public let targetElementFingerprint: String
    public let totalUTF8Length: Int
    public let postedPrefixLength: Int
    public let verifiedPrefixLength: Int
    public let status: OperationStatus
    public let errorCode: StableErrorCode?
    public let createdAt: Date
    public let updatedAt: Date
    public let expiresAt: Date

    public init(
        pairedDeviceID: String,
        operationID: UUID,
        payloadSHA256: Data,
        targetPID: pid_t,
        targetElementFingerprint: String,
        totalUTF8Length: Int,
        postedPrefixLength: Int,
        verifiedPrefixLength: Int,
        status: OperationStatus,
        errorCode: StableErrorCode?,
        createdAt: Date,
        updatedAt: Date,
        expiresAt: Date
    ) throws {
        guard payloadSHA256.count == 32,
              (0...1024).contains(totalUTF8Length),
              (0...totalUTF8Length).contains(postedPrefixLength),
              (0...postedPrefixLength).contains(verifiedPrefixLength),
              expiresAt > createdAt else {
            throw StableErrorCode.invalidRequest
        }
        self.pairedDeviceID = pairedDeviceID
        self.operationID = operationID
        self.payloadSHA256 = payloadSHA256
        self.targetPID = targetPID
        self.targetElementFingerprint = targetElementFingerprint
        self.totalUTF8Length = totalUTF8Length
        self.postedPrefixLength = postedPrefixLength
        self.verifiedPrefixLength = verifiedPrefixLength
        self.status = status
        self.errorCode = errorCode
        self.createdAt = createdAt
        self.updatedAt = updatedAt
        self.expiresAt = expiresAt
    }
}

public struct InjectionResult: Codable, Equatable, Sendable {
    public let operationID: UUID
    public let status: OperationStatus
    public let postedPrefixLength: Int
    public let verifiedPrefixLength: Int
    public let errorCode: StableErrorCode?

    public init(
        operationID: UUID,
        status: OperationStatus,
        postedPrefixLength: Int,
        verifiedPrefixLength: Int,
        errorCode: StableErrorCode?
    ) {
        self.operationID = operationID
        self.status = status
        self.postedPrefixLength = postedPrefixLength
        self.verifiedPrefixLength = verifiedPrefixLength
        self.errorCode = errorCode
    }
}
```

创建 `companion/Sources/Phase0Contracts/Evidence.swift`：

```swift
import Foundation

public struct GateCheck: Codable, Equatable, Sendable {
    public let id: String
    public let status: GateStatus
    public let detail: String

    public init(id: String, status: GateStatus, detail: String) {
        self.id = id
        self.status = status
        self.detail = detail
    }
}

public struct HILEvidence: Codable, Equatable, Sendable {
    public let schemaVersion: String
    public let runID: UUID
    public let startedAt: Date
    public let completedAt: Date
    public let checks: [GateCheck]

    public init(
        schemaVersion: String,
        runID: UUID,
        startedAt: Date,
        completedAt: Date,
        checks: [GateCheck]
    ) {
        self.schemaVersion = schemaVersion
        self.runID = runID
        self.startedAt = startedAt
        self.completedAt = completedAt
        self.checks = checks
    }

    public var overallStatus: GateStatus {
        if checks.contains(where: { $0.status == .fail }) {
            return .fail
        }
        if checks.isEmpty || checks.contains(where: { $0.status == .blocked }) {
            return .blocked
        }
        return .pass
    }
}
```

为后续四个 library target 分别创建一个具有真实接口的源文件，使 target 图从第一次提交开始即可构建：

```swift
// companion/Sources/Phase0Ledger/SQLiteTextOperationLedger.swift
import Foundation
import Phase0Contracts

public protocol TextOperationLedger: Sendable {
    func fetch(pairedDeviceID: String, operationID: UUID) throws -> TextOperationRecord?
}
```

```swift
// companion/Sources/Phase0Security/PairingDerivation.swift
import Foundation

public enum Phase0SecurityVersion {
    public static let protocolVersion = "1.0"
}
```

```swift
// companion/Sources/Phase0GATT/GATTFrame.swift
import Foundation

public enum Phase0GATTConstants {
    public static let protocolVersion: UInt8 = 1
    public static let maximumMessageBytes = 1024
}
```

```swift
// companion/Sources/Phase0Unicode/UTF8Chunker.swift
import Foundation

public enum Phase0UnicodeConstants {
    public static let maximumUTF8Bytes = 1024
}
```

创建 `companion/Sources/cardputer-phase0-probe/main.swift`：

```swift
import Foundation

if CommandLine.arguments == ["cardputer-phase0-probe", "--version"] {
    print("cardputer-phase0-probe 0.1.0")
} else {
    FileHandle.standardError.write(
        Data("usage: cardputer-phase0-probe --version\n".utf8)
    )
    exit(64)
}
```

同时创建四个初始 target surface tests，确保 Task 1 的完整 manifest 可立即构建：

```swift
// companion/Tests/Phase0LedgerTests/ModuleSurfaceTests.swift
import XCTest
@testable import Phase0Ledger

final class LedgerModuleSurfaceTests: XCTestCase {
    func testLedgerProtocolIsVisible() {
        XCTAssertNotNil(TextOperationLedger.self)
    }
}
```

```swift
// companion/Tests/Phase0SecurityTests/ModuleSurfaceTests.swift
import XCTest
@testable import Phase0Security

final class SecurityModuleSurfaceTests: XCTestCase {
    func testProtocolVersionIsStable() {
        XCTAssertEqual(Phase0SecurityVersion.protocolVersion, "1.0")
    }
}
```

```swift
// companion/Tests/Phase0GATTTests/ModuleSurfaceTests.swift
import XCTest
@testable import Phase0GATT

final class GATTModuleSurfaceTests: XCTestCase {
    func testProbeLimitsAreStable() {
        XCTAssertEqual(Phase0GATTConstants.protocolVersion, 1)
        XCTAssertEqual(Phase0GATTConstants.maximumMessageBytes, 1024)
    }
}
```

```swift
// companion/Tests/Phase0UnicodeTests/ModuleSurfaceTests.swift
import XCTest
@testable import Phase0Unicode

final class UnicodeModuleSurfaceTests: XCTestCase {
    func testProbeLimitIsStable() {
        XCTAssertEqual(Phase0UnicodeConstants.maximumUTF8Bytes, 1024)
    }
}
```

- [ ] **Step 4: 运行契约测试与全包构建**

Run:

```bash
swift test --package-path companion --filter ContractTests
swift build --package-path companion
companion/.build/debug/cardputer-phase0-probe --version
```

Expected: 测试输出 `3 tests` 且无 failure；构建成功；最后一行严格为 `cardputer-phase0-probe 0.1.0`。

- [ ] **Step 5: 提交**

```bash
git add companion/Package.swift companion/Sources companion/Tests
git commit -m "test: establish macos phase zero contracts"
```

## Task 2: 建立不保存正文的 SQLite 注入账本

**Files:**
- Modify: `companion/Sources/Phase0Ledger/SQLiteTextOperationLedger.swift`
- Create: `companion/Tests/Phase0LedgerTests/SQLiteTextOperationLedgerTests.swift`

**Interfaces:**
- Consumes: `TextOperationRecord`、`OperationStatus`、`StableErrorCode`。
- Produces:
  - `SQLiteTextOperationLedger.init(path: String) throws`
  - `begin(_ intent: TextOperationIntent, now: Date) throws -> BeginDisposition`
  - `markAccepted(key: OperationKey, now: Date) throws`
  - `markPosted(key: OperationKey, utf8PrefixLength: Int, now: Date) throws`
  - `markVerified(key: OperationKey, utf8PrefixLength: Int, now: Date) throws`
  - `finish(key: OperationKey, status: OperationStatus, errorCode: StableErrorCode?, now: Date) throws`
  - `recoverInterrupted(now: Date) throws -> [OperationKey]`
  - `fetch(pairedDeviceID: String, operationID: UUID) throws -> TextOperationRecord?`
- `begin` 必须在第一块 `CGEvent` 之前提交；`recoverInterrupted` 将所有 `intent`/`accepted` 转成 `indeterminate`，不重放正文。

- [ ] **Step 1: 写入失败的账本测试**

创建 `companion/Tests/Phase0LedgerTests/SQLiteTextOperationLedgerTests.swift`：

```swift
import CryptoKit
import Foundation
import XCTest
@testable import Phase0Ledger
import Phase0Contracts

final class SQLiteTextOperationLedgerTests: XCTestCase {
    private func temporaryPath() -> String {
        FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("sqlite3")
            .path
    }

    private func intent(
        operationID: UUID = UUID(),
        payload: Data = Data("中文".utf8)
    ) -> TextOperationIntent {
        TextOperationIntent(
            key: OperationKey(pairedDeviceID: "device-a", operationID: operationID),
            payloadSHA256: Data(SHA256.hash(data: payload)),
            targetPID: 101,
            targetElementFingerprint: "sha256:element",
            totalUTF8Length: payload.count
        )
    }

    func testBeginIsIdempotentAndRejectsHashConflict() throws {
        let ledger = try SQLiteTextOperationLedger(path: temporaryPath())
        let operationID = UUID()
        let first = intent(operationID: operationID)
        XCTAssertEqual(try ledger.begin(first, now: Date(timeIntervalSince1970: 10)), .created)
        XCTAssertEqual(try ledger.begin(first, now: Date(timeIntervalSince1970: 11)), .existing(.intent))

        let conflict = intent(operationID: operationID, payload: Data("不同".utf8))
        XCTAssertThrowsError(try ledger.begin(conflict, now: Date(timeIntervalSince1970: 12))) {
            XCTAssertEqual($0 as? StableErrorCode, .invalidRequest)
        }
    }

    func testPrefixUpdatesAreMonotonicAndBounded() throws {
        let ledger = try SQLiteTextOperationLedger(path: temporaryPath())
        let value = intent()
        _ = try ledger.begin(value, now: Date(timeIntervalSince1970: 10))
        try ledger.markAccepted(key: value.key, now: Date(timeIntervalSince1970: 11))
        try ledger.markPosted(key: value.key, utf8PrefixLength: 3, now: Date(timeIntervalSince1970: 12))
        try ledger.markVerified(key: value.key, utf8PrefixLength: 3, now: Date(timeIntervalSince1970: 13))

        XCTAssertThrowsError(
            try ledger.markPosted(key: value.key, utf8PrefixLength: 2, now: Date(timeIntervalSince1970: 14))
        )
        let record = try XCTUnwrap(
            ledger.fetch(
                pairedDeviceID: value.key.pairedDeviceID,
                operationID: value.key.operationID
            )
        )
        XCTAssertEqual(record.postedPrefixLength, 3)
        XCTAssertEqual(record.verifiedPrefixLength, 3)
    }

    func testRestartConvertsNonterminalRowsToIndeterminateWithoutPlaintext() throws {
        let path = temporaryPath()
        let operation = intent(payload: Data("不能写进数据库".utf8))
        do {
            let ledger = try SQLiteTextOperationLedger(path: path)
            _ = try ledger.begin(operation, now: Date(timeIntervalSince1970: 10))
            try ledger.markAccepted(key: operation.key, now: Date(timeIntervalSince1970: 11))
            try ledger.markPosted(key: operation.key, utf8PrefixLength: 6, now: Date(timeIntervalSince1970: 12))
        }

        let reopened = try SQLiteTextOperationLedger(path: path)
        XCTAssertEqual(
            try reopened.recoverInterrupted(now: Date(timeIntervalSince1970: 20)),
            [operation.key]
        )
        let record = try XCTUnwrap(
            reopened.fetch(
                pairedDeviceID: operation.key.pairedDeviceID,
                operationID: operation.key.operationID
            )
        )
        XCTAssertEqual(record.status, .indeterminate)
        XCTAssertEqual(record.errorCode, .indeterminate)
        let databaseBytes = try Data(contentsOf: URL(fileURLWithPath: path))
        XCTAssertNil(String(data: databaseBytes, encoding: .utf8)?.range(of: "不能写进数据库"))
    }
}
```

- [ ] **Step 2: 运行测试确认 API 尚不存在**

Run:

```bash
swift test --package-path companion --filter SQLiteTextOperationLedgerTests
```

Expected: FAIL，错误包含 `cannot find 'TextOperationIntent' in scope`。

- [ ] **Step 3: 实现 schema、幂等 begin、单调前缀与恢复**

将 `companion/Sources/Phase0Ledger/SQLiteTextOperationLedger.swift` 替换为以下核心实现。所有 SQL 参数必须使用 bind；正文不出现在任何参数中：

```swift
import CSQLite
import Foundation
import Phase0Contracts

public struct OperationKey: Hashable, Codable, Sendable {
    public let pairedDeviceID: String
    public let operationID: UUID

    public init(pairedDeviceID: String, operationID: UUID) {
        self.pairedDeviceID = pairedDeviceID
        self.operationID = operationID
    }
}

public struct TextOperationIntent: Sendable {
    public let key: OperationKey
    public let payloadSHA256: Data
    public let targetPID: pid_t
    public let targetElementFingerprint: String
    public let totalUTF8Length: Int

    public init(
        key: OperationKey,
        payloadSHA256: Data,
        targetPID: pid_t,
        targetElementFingerprint: String,
        totalUTF8Length: Int
    ) {
        self.key = key
        self.payloadSHA256 = payloadSHA256
        self.targetPID = targetPID
        self.targetElementFingerprint = targetElementFingerprint
        self.totalUTF8Length = totalUTF8Length
    }
}

public enum BeginDisposition: Equatable, Sendable {
    case created
    case existing(OperationStatus)
}

public protocol TextOperationLedger: Sendable {
    func begin(_ intent: TextOperationIntent, now: Date) throws -> BeginDisposition
    func markAccepted(key: OperationKey, now: Date) throws
    func markPosted(
        key: OperationKey,
        utf8PrefixLength: Int,
        now: Date
    ) throws
    func markVerified(
        key: OperationKey,
        utf8PrefixLength: Int,
        now: Date
    ) throws
    func finish(
        key: OperationKey,
        status: OperationStatus,
        errorCode: StableErrorCode?,
        now: Date
    ) throws
    func recoverInterrupted(now: Date) throws -> [OperationKey]
    func fetch(
        pairedDeviceID: String,
        operationID: UUID
    ) throws -> TextOperationRecord?
}

private let SQLITE_TRANSIENT = unsafeBitCast(
    -1,
    to: sqlite3_destructor_type.self
)

public final class SQLiteTextOperationLedger: TextOperationLedger, @unchecked Sendable {
    private var db: OpaquePointer?
    private let lock = NSLock()
    private let retentionSeconds: TimeInterval = 600

    public init(path: String) throws {
        guard sqlite3_open_v2(
            path,
            &db,
            SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
            nil
        ) == SQLITE_OK else {
            throw StableErrorCode.invalidRequest
        }
        try execute("PRAGMA journal_mode=WAL")
        try execute("PRAGMA synchronous=FULL")
        try execute("""
        CREATE TABLE IF NOT EXISTS text_operations (
            paired_device_id TEXT NOT NULL,
            operation_id TEXT NOT NULL,
            payload_sha256 BLOB NOT NULL CHECK(length(payload_sha256) = 32),
            target_pid INTEGER NOT NULL,
            target_element_fingerprint TEXT NOT NULL,
            total_utf8_length INTEGER NOT NULL CHECK(total_utf8_length BETWEEN 0 AND 1024),
            posted_prefix_length INTEGER NOT NULL DEFAULT 0,
            verified_prefix_length INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL CHECK(status IN (
                'intent','accepted','completed','failed','partial','indeterminate'
            )),
            error_code TEXT,
            created_at REAL NOT NULL,
            updated_at REAL NOT NULL,
            expires_at REAL NOT NULL,
            PRIMARY KEY (paired_device_id, operation_id),
            CHECK(verified_prefix_length <= posted_prefix_length),
            CHECK(posted_prefix_length <= total_utf8_length)
        )
        """)
    }

    deinit {
        sqlite3_close(db)
    }

    public func begin(_ intent: TextOperationIntent, now: Date) throws -> BeginDisposition {
        guard intent.payloadSHA256.count == 32,
              (0...1024).contains(intent.totalUTF8Length) else {
            throw StableErrorCode.invalidRequest
        }
        return try locked {
            if let existing = try fetchUnlocked(
                pairedDeviceID: intent.key.pairedDeviceID,
                operationID: intent.key.operationID
            ) {
                guard existing.payloadSHA256 == intent.payloadSHA256 else {
                    throw StableErrorCode.invalidRequest
                }
                return .existing(existing.status)
            }
            let sql = """
            INSERT INTO text_operations (
                paired_device_id, operation_id, payload_sha256, target_pid,
                target_element_fingerprint, total_utf8_length,
                posted_prefix_length, verified_prefix_length, status,
                error_code, created_at, updated_at, expires_at
            ) VALUES (?, ?, ?, ?, ?, ?, 0, 0, 'intent', NULL, ?, ?, ?)
            """
            try statement(sql) { stmt in
                bindText(stmt, 1, intent.key.pairedDeviceID)
                bindText(stmt, 2, intent.key.operationID.uuidString.lowercased())
                intent.payloadSHA256.withUnsafeBytes {
                    sqlite3_bind_blob(stmt, 3, $0.baseAddress, 32, SQLITE_TRANSIENT)
                }
                sqlite3_bind_int64(stmt, 4, Int64(intent.targetPID))
                bindText(stmt, 5, intent.targetElementFingerprint)
                sqlite3_bind_int64(stmt, 6, Int64(intent.totalUTF8Length))
                sqlite3_bind_double(stmt, 7, now.timeIntervalSince1970)
                sqlite3_bind_double(stmt, 8, now.timeIntervalSince1970)
                sqlite3_bind_double(stmt, 9, now.addingTimeInterval(retentionSeconds).timeIntervalSince1970)
                try expectDone(stmt)
            }
            return .created
        }
    }

    public func markAccepted(key: OperationKey, now: Date) throws {
        try updateStatus(
            key: key,
            from: [.intent],
            to: .accepted,
            errorCode: nil,
            now: now
        )
    }

    public func markPosted(
        key: OperationKey,
        utf8PrefixLength: Int,
        now: Date
    ) throws {
        try locked {
            try statement("""
            UPDATE text_operations
            SET posted_prefix_length = ?, updated_at = ?
            WHERE paired_device_id = ? AND operation_id = ?
              AND status IN ('intent','accepted')
              AND posted_prefix_length <= ?
              AND verified_prefix_length <= ?
              AND ? <= total_utf8_length
            """) { stmt in
                sqlite3_bind_int64(stmt, 1, Int64(utf8PrefixLength))
                sqlite3_bind_double(stmt, 2, now.timeIntervalSince1970)
                bindText(stmt, 3, key.pairedDeviceID)
                bindText(stmt, 4, key.operationID.uuidString.lowercased())
                sqlite3_bind_int64(stmt, 5, Int64(utf8PrefixLength))
                sqlite3_bind_int64(stmt, 6, Int64(utf8PrefixLength))
                sqlite3_bind_int64(stmt, 7, Int64(utf8PrefixLength))
                try expectOneChangedRow(stmt)
            }
        }
    }

    public func markVerified(
        key: OperationKey,
        utf8PrefixLength: Int,
        now: Date
    ) throws {
        try locked {
            try statement("""
            UPDATE text_operations
            SET verified_prefix_length = ?, updated_at = ?
            WHERE paired_device_id = ? AND operation_id = ?
              AND status IN ('intent','accepted')
              AND verified_prefix_length <= ?
              AND ? <= posted_prefix_length
            """) { stmt in
                sqlite3_bind_int64(stmt, 1, Int64(utf8PrefixLength))
                sqlite3_bind_double(stmt, 2, now.timeIntervalSince1970)
                bindText(stmt, 3, key.pairedDeviceID)
                bindText(stmt, 4, key.operationID.uuidString.lowercased())
                sqlite3_bind_int64(stmt, 5, Int64(utf8PrefixLength))
                sqlite3_bind_int64(stmt, 6, Int64(utf8PrefixLength))
                try expectOneChangedRow(stmt)
            }
        }
    }

    public func finish(
        key: OperationKey,
        status: OperationStatus,
        errorCode: StableErrorCode?,
        now: Date
    ) throws {
        guard status.isTerminal else {
            throw StableErrorCode.invalidRequest
        }
        try updateStatus(
            key: key,
            from: [.intent, .accepted],
            to: status,
            errorCode: errorCode,
            now: now
        )
    }

    public func recoverInterrupted(now: Date) throws -> [OperationKey] {
        try locked {
            var keys: [OperationKey] = []
            try statement("""
            SELECT paired_device_id, operation_id
            FROM text_operations
            WHERE status IN ('intent','accepted')
            ORDER BY paired_device_id, operation_id
            """) { stmt in
                while sqlite3_step(stmt) == SQLITE_ROW {
                    let device = String(cString: sqlite3_column_text(stmt, 0))
                    let id = UUID(uuidString: String(cString: sqlite3_column_text(stmt, 1)))
                    if let id {
                        keys.append(OperationKey(pairedDeviceID: device, operationID: id))
                    }
                }
            }
            try statement("""
            UPDATE text_operations
            SET status = 'indeterminate', error_code = 'indeterminate', updated_at = ?
            WHERE status IN ('intent','accepted')
            """) { stmt in
                sqlite3_bind_double(stmt, 1, now.timeIntervalSince1970)
                try expectDone(stmt)
            }
            return keys
        }
    }

    public func fetch(
        pairedDeviceID: String,
        operationID: UUID
    ) throws -> TextOperationRecord? {
        try locked {
            try fetchUnlocked(pairedDeviceID: pairedDeviceID, operationID: operationID)
        }
    }

    private func fetchUnlocked(
        pairedDeviceID: String,
        operationID: UUID
    ) throws -> TextOperationRecord? {
        var result: TextOperationRecord?
        try statement("""
        SELECT payload_sha256, target_pid, target_element_fingerprint,
               total_utf8_length, posted_prefix_length, verified_prefix_length,
               status, error_code, created_at, updated_at, expires_at
        FROM text_operations
        WHERE paired_device_id = ? AND operation_id = ?
        """) { stmt in
            bindText(stmt, 1, pairedDeviceID)
            bindText(stmt, 2, operationID.uuidString.lowercased())
            guard sqlite3_step(stmt) == SQLITE_ROW else {
                return
            }
            let hash = Data(
                bytes: sqlite3_column_blob(stmt, 0),
                count: Int(sqlite3_column_bytes(stmt, 0))
            )
            let status = OperationStatus(
                rawValue: String(cString: sqlite3_column_text(stmt, 6))
            )
            let error = sqlite3_column_type(stmt, 7) == SQLITE_NULL
                ? nil
                : StableErrorCode(rawValue: String(cString: sqlite3_column_text(stmt, 7)))
            guard let status else {
                throw StableErrorCode.invalidRequest
            }
            result = try TextOperationRecord(
                pairedDeviceID: pairedDeviceID,
                operationID: operationID,
                payloadSHA256: hash,
                targetPID: pid_t(sqlite3_column_int64(stmt, 1)),
                targetElementFingerprint: String(cString: sqlite3_column_text(stmt, 2)),
                totalUTF8Length: Int(sqlite3_column_int64(stmt, 3)),
                postedPrefixLength: Int(sqlite3_column_int64(stmt, 4)),
                verifiedPrefixLength: Int(sqlite3_column_int64(stmt, 5)),
                status: status,
                errorCode: error,
                createdAt: Date(timeIntervalSince1970: sqlite3_column_double(stmt, 8)),
                updatedAt: Date(timeIntervalSince1970: sqlite3_column_double(stmt, 9)),
                expiresAt: Date(timeIntervalSince1970: sqlite3_column_double(stmt, 10))
            )
        }
        return result
    }

    private func updateStatus(
        key: OperationKey,
        from allowed: [OperationStatus],
        to status: OperationStatus,
        errorCode: StableErrorCode?,
        now: Date
    ) throws {
        let allowedSQL = allowed.map { "'\($0.rawValue)'" }.joined(separator: ",")
        try locked {
            try statement("""
            UPDATE text_operations SET status = ?, error_code = ?, updated_at = ?
            WHERE paired_device_id = ? AND operation_id = ?
              AND status IN (\(allowedSQL))
            """) { stmt in
                bindText(stmt, 1, status.rawValue)
                if let errorCode {
                    bindText(stmt, 2, errorCode.rawValue)
                } else {
                    sqlite3_bind_null(stmt, 2)
                }
                sqlite3_bind_double(stmt, 3, now.timeIntervalSince1970)
                bindText(stmt, 4, key.pairedDeviceID)
                bindText(stmt, 5, key.operationID.uuidString.lowercased())
                try expectOneChangedRow(stmt)
            }
        }
    }

    private func execute(_ sql: String) throws {
        guard sqlite3_exec(db, sql, nil, nil, nil) == SQLITE_OK else {
            throw StableErrorCode.invalidRequest
        }
    }

    private func statement(
        _ sql: String,
        body: (OpaquePointer) throws -> Void
    ) throws {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK,
              let stmt else {
            throw StableErrorCode.invalidRequest
        }
        defer { sqlite3_finalize(stmt) }
        try body(stmt)
    }

    private func bindText(_ stmt: OpaquePointer, _ index: Int32, _ value: String) {
        sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT)
    }

    private func expectDone(_ stmt: OpaquePointer) throws {
        guard sqlite3_step(stmt) == SQLITE_DONE else {
            throw StableErrorCode.invalidRequest
        }
    }

    private func expectOneChangedRow(_ stmt: OpaquePointer) throws {
        try expectDone(stmt)
        guard sqlite3_changes(db) == 1 else {
            throw StableErrorCode.invalidRequest
        }
    }

    private func locked<T>(_ body: () throws -> T) throws -> T {
        lock.lock()
        defer { lock.unlock() }
        return try body()
    }
}
```

- [ ] **Step 4: 运行账本测试并检查数据库不含正文**

Run:

```bash
swift test --package-path companion --filter SQLiteTextOperationLedgerTests
```

Expected: `3 tests` 全部通过；冲突 payload、前缀回退和非终态恢复均被测试覆盖。

- [ ] **Step 5: 提交**

```bash
git add companion/Sources/Phase0Ledger companion/Tests/Phase0LedgerTests
git commit -m "feat: add durable unicode operation ledger"
```

## Task 3: 实现 transcript-bound SAS 与 WSS TLS exporter 客户端证明

**Files:**
- Modify: `companion/Sources/Phase0Security/PairingDerivation.swift`
- Create: `companion/Sources/Phase0Security/TLSChannelBinding.swift`
- Create: `companion/Tests/Phase0SecurityTests/PairingDerivationTests.swift`
- Create: `companion/Tests/Phase0SecurityTests/TLSChannelBindingTests.swift`
- Consume read-only: `protocol/phase0/fixtures/pairing-v1.json`, `protocol/phase0/fixtures/wss-auth-v1.json`

**Interfaces:**
- Consumes: 固定 label、双方 instance/device ID、双方长期公钥、双方临时公钥和双方 32-byte nonce。
- Produces:
  - `PairingTranscript.canonicalBytes() throws -> Data`
  - `PairingDerivation.derive(sharedSecret: SharedSecret, transcript: PairingTranscript) throws -> PairingKeys`
  - `TLSExporter.export(label: String, context: Data, length: Int) throws -> Data`
  - `WSSChannelBinding.material(exporter: Data, companionInstanceID: UUID, deviceID: UUID, protocolVersion: String, challenge: Data) throws -> Data`
  - `WSSChannelBinding.sign(material: Data, privateKey: P256.Signing.PrivateKey) throws -> Data`
  - `WSSChannelBinding.verify(signatureRawRS: Data, material: Data, publicKey: P256.Signing.PublicKey) -> Bool`
- SAS 始终为零填充 6 位数字并按协议使用 rejection sampling；长期和临时密钥位置按 `device`、`companion` 角色固定，禁止依赖字典迭代顺序。
- Swift 结果必须逐字节匹配 foundation 生成的 pairing/WSS fixture；fixture 不存在或 hash 不符时测试失败。

- [ ] **Step 1: 写入失败的配对与 channel-binding 测试**

创建 `companion/Tests/Phase0SecurityTests/PairingDerivationTests.swift`：

```swift
import CryptoKit
import Foundation
import XCTest
@testable import Phase0Security

final class PairingDerivationTests: XCTestCase {
    private func transcript(deviceNonceByte: UInt8 = 0x22) throws -> PairingTranscript {
        let companionLongTerm = P256.Signing.PrivateKey()
        let deviceLongTerm = P256.Signing.PrivateKey()
        let companionEphemeral = P256.KeyAgreement.PrivateKey()
        let deviceEphemeral = P256.KeyAgreement.PrivateKey()
        return try PairingTranscript(
            protocolVersion: "1.0",
            companionInstanceID: UUID(uuidString: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")!,
            deviceID: UUID(uuidString: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb")!,
            companionLongTermPublicKey: companionLongTerm.publicKey.x963Representation,
            deviceLongTermPublicKey: deviceLongTerm.publicKey.x963Representation,
            companionEphemeralPublicKey: companionEphemeral.publicKey.x963Representation,
            deviceEphemeralPublicKey: deviceEphemeral.publicKey.x963Representation,
            companionNonce: Data(repeating: 0x11, count: 32),
            deviceNonce: Data(repeating: deviceNonceByte, count: 32)
        )
    }

    func testBothSidesDeriveIdenticalSeparatedKeysAndSixDigitSAS() throws {
        let companionEphemeral = P256.KeyAgreement.PrivateKey()
        let deviceEphemeral = P256.KeyAgreement.PrivateKey()
        let base = try transcript()
        let value = try PairingTranscript(
            protocolVersion: base.protocolVersion,
            companionInstanceID: base.companionInstanceID,
            deviceID: base.deviceID,
            companionLongTermPublicKey: base.companionLongTermPublicKey,
            deviceLongTermPublicKey: base.deviceLongTermPublicKey,
            companionEphemeralPublicKey: companionEphemeral.publicKey.x963Representation,
            deviceEphemeralPublicKey: deviceEphemeral.publicKey.x963Representation,
            companionNonce: base.companionNonce,
            deviceNonce: base.deviceNonce
        )
        let companionSecret = try companionEphemeral.sharedSecretFromKeyAgreement(
            with: deviceEphemeral.publicKey
        )
        let deviceSecret = try deviceEphemeral.sharedSecretFromKeyAgreement(
            with: companionEphemeral.publicKey
        )
        let companionKeys = try PairingDerivation.derive(
            sharedSecret: companionSecret,
            transcript: value
        )
        let deviceKeys = try PairingDerivation.derive(
            sharedSecret: deviceSecret,
            transcript: value
        )

        XCTAssertEqual(companionKeys, deviceKeys)
        XCTAssertNotEqual(companionKeys.pairingRoot, companionKeys.gattAuth)
        XCTAssertEqual(companionKeys.sas.count, 6)
        XCTAssertNotNil(Int(companionKeys.sas))
    }

    func testTranscriptMutationChangesDerivedRoot() throws {
        let privateKey = P256.KeyAgreement.PrivateKey()
        let peer = P256.KeyAgreement.PrivateKey()
        let secret = try privateKey.sharedSecretFromKeyAgreement(with: peer.publicKey)
        let first = try PairingDerivation.derive(
            sharedSecret: secret,
            transcript: transcript(deviceNonceByte: 0x22)
        )
        let changed = try PairingDerivation.derive(
            sharedSecret: secret,
            transcript: transcript(deviceNonceByte: 0x23)
        )
        XCTAssertNotEqual(first.pairingRoot, changed.pairingRoot)
        XCTAssertNotEqual(first.transcriptSHA256, changed.transcriptSHA256)
    }

    func testTranscriptRejectsWrongNonceOrPublicKeyLength() throws {
        XCTAssertThrowsError(
            try PairingTranscript(
                protocolVersion: "1.0",
                companionInstanceID: UUID(),
                deviceID: UUID(),
                companionLongTermPublicKey: Data(repeating: 1, count: 64),
                deviceLongTermPublicKey: Data(repeating: 2, count: 65),
                companionEphemeralPublicKey: Data(repeating: 3, count: 65),
                deviceEphemeralPublicKey: Data(repeating: 4, count: 65),
                companionNonce: Data(repeating: 5, count: 31),
                deviceNonce: Data(repeating: 6, count: 32)
            )
        )
    }
}
```

创建 `companion/Tests/Phase0SecurityTests/TLSChannelBindingTests.swift`：

```swift
import CryptoKit
import Foundation
import XCTest
@testable import Phase0Security

private struct FixedExporter: TLSExporter {
    let bytes: Data

    func export(label: String, context: Data, length: Int) throws -> Data {
        XCTAssertEqual(label, "EXPORTER-Cardputer-Codex-Companion-v1")
        XCTAssertEqual(context, Data())
        XCTAssertEqual(length, 32)
        return bytes
    }
}

final class TLSChannelBindingTests: XCTestCase {
    func testSignatureBindsEveryRequiredField() throws {
        let exporter = FixedExporter(bytes: Data(repeating: 0x31, count: 32))
        let secret = try exporter.export(
            label: WSSChannelBinding.exporterLabel,
            context: Data(),
            length: 32
        )
        let instanceID = UUID(uuidString: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")!
        let deviceID = UUID(uuidString: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb")!
        let challenge = Data(repeating: 0x41, count: 32)
        let key = P256.Signing.PrivateKey()
        let material = try WSSChannelBinding.material(
            exporter: secret,
            companionInstanceID: instanceID,
            deviceID: deviceID,
            protocolVersion: "1.0",
            challenge: challenge
        )
        let signature = try WSSChannelBinding.sign(material: material, privateKey: key)

        XCTAssertTrue(
            WSSChannelBinding.verify(
                signatureRawRS: signature,
                material: material,
                publicKey: key.publicKey
            )
        )
        let changed = try WSSChannelBinding.material(
            exporter: secret,
            companionInstanceID: instanceID,
            deviceID: deviceID,
            protocolVersion: "1.1",
            challenge: challenge
        )
        XCTAssertFalse(
            WSSChannelBinding.verify(
                signatureRawRS: signature,
                material: changed,
                publicKey: key.publicKey
            )
        )
    }

    func testMaterialRejectsNon256BitExporterAndChallenge() throws {
        XCTAssertThrowsError(
            try WSSChannelBinding.material(
                exporter: Data(repeating: 0, count: 31),
                companionInstanceID: UUID(),
                deviceID: UUID(),
                protocolVersion: "1.0",
                challenge: Data(repeating: 0, count: 32)
            )
        )
        XCTAssertThrowsError(
            try WSSChannelBinding.material(
                exporter: Data(repeating: 0, count: 32),
                companionInstanceID: UUID(),
                deviceID: UUID(),
                protocolVersion: "1.0",
                challenge: Data(repeating: 0, count: 31)
            )
        )
    }
}
```

- [ ] **Step 2: 运行测试确认 transcript 和 exporter API 尚不存在**

Run:

```bash
swift test --package-path companion --filter Phase0SecurityTests
```

Expected: FAIL，错误包含 `cannot find type 'PairingTranscript'` 和 `cannot find type 'TLSExporter'`。

- [ ] **Step 3: 实现确定性 transcript、HKDF label 隔离和 TLS exporter**

将 `companion/Sources/Phase0Security/PairingDerivation.swift` 替换为：

```swift
import CryptoKit
import Foundation
import Phase0Contracts

public struct PairingTranscript: Sendable {
    public let protocolVersion: String
    public let companionInstanceID: UUID
    public let deviceID: UUID
    public let companionLongTermPublicKey: Data
    public let deviceLongTermPublicKey: Data
    public let companionEphemeralPublicKey: Data
    public let deviceEphemeralPublicKey: Data
    public let companionNonce: Data
    public let deviceNonce: Data

    public init(
        protocolVersion: String,
        companionInstanceID: UUID,
        deviceID: UUID,
        companionLongTermPublicKey: Data,
        deviceLongTermPublicKey: Data,
        companionEphemeralPublicKey: Data,
        deviceEphemeralPublicKey: Data,
        companionNonce: Data,
        deviceNonce: Data
    ) throws {
        guard !protocolVersion.isEmpty,
              companionLongTermPublicKey.count == 65,
              deviceLongTermPublicKey.count == 65,
              companionEphemeralPublicKey.count == 65,
              deviceEphemeralPublicKey.count == 65,
              companionNonce.count == 32,
              deviceNonce.count == 32 else {
            throw StableErrorCode.invalidRequest
        }
        self.protocolVersion = protocolVersion
        self.companionInstanceID = companionInstanceID
        self.deviceID = deviceID
        self.companionLongTermPublicKey = companionLongTermPublicKey
        self.deviceLongTermPublicKey = deviceLongTermPublicKey
        self.companionEphemeralPublicKey = companionEphemeralPublicKey
        self.deviceEphemeralPublicKey = deviceEphemeralPublicKey
        self.companionNonce = companionNonce
        self.deviceNonce = deviceNonce
    }

    public func canonicalBytes() throws -> Data {
        var output = Data("CCP-PAIR".utf8)
        output.append(contentsOf: [0x00, 0x01])
        try output.appendLengthPrefixed(
            Data(deviceID.uuidString.lowercased().utf8)
        )
        try output.appendLengthPrefixed(
            Data(companionInstanceID.uuidString.lowercased().utf8)
        )
        try output.appendLengthPrefixed(Data(protocolVersion.utf8))
        output.append(deviceLongTermPublicKey)
        output.append(companionLongTermPublicKey)
        output.append(deviceEphemeralPublicKey)
        output.append(companionEphemeralPublicKey)
        output.append(deviceNonce)
        output.append(companionNonce)
        return output
    }
}

public struct PairingKeys: Equatable, Sendable {
    public let pairingRoot: Data
    public let gattAuth: Data
    public let sas: String
    public let transcriptSHA256: Data
}

public enum PairingDerivation {
    public static func derive(
        sharedSecret: SharedSecret,
        transcript: PairingTranscript
    ) throws -> PairingKeys {
        let digest = Data(SHA256.hash(data: try transcript.canonicalBytes()))
        let salt = SymmetricKey(data: digest)
        let root = sharedSecret.hkdfDerivedSymmetricKey(
            using: SHA256.self,
            salt: salt,
            sharedInfo: Data("cardputer-codex/pair-root/v1".utf8),
            outputByteCount: 32
        )
        let gatt = sharedSecret.hkdfDerivedSymmetricKey(
            using: SHA256.self,
            salt: salt,
            sharedInfo: Data("cardputer-codex/gatt-auth/v1".utf8),
            outputByteCount: 32
        )
        let sas = try deriveSixDigitSAS(
            sharedSecret: sharedSecret,
            salt: digest,
            label: Data("cardputer-codex/sas/v1".utf8)
        )
        return PairingKeys(
            pairingRoot: root.withUnsafeBytes { Data($0) },
            gattAuth: gatt.withUnsafeBytes { Data($0) },
            sas: sas,
            transcriptSHA256: digest
        )
    }
}

private func deriveSixDigitSAS(
    sharedSecret: SharedSecret,
    salt: Data,
    label: Data
) throws -> String {
    let limit = UInt32.max - (UInt32.max % 1_000_000)
    for attempt in UInt32(0)..<UInt32(256) {
        var info = label
        var counter = attempt.bigEndian
        info.append(withUnsafeBytes(of: &counter) { Data($0) })
        let key = sharedSecret.hkdfDerivedSymmetricKey(
            using: SHA256.self,
            salt: SymmetricKey(data: salt),
            sharedInfo: info,
            outputByteCount: 4
        )
        let bytes = key.withUnsafeBytes { Data($0) }
        let word = bytes.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        if word < limit {
            return String(format: "%06u", word % 1_000_000)
        }
    }
    throw StableErrorCode.indeterminate
}

private extension UUID {
    var data: Data {
        var value = uuid
        return withUnsafeBytes(of: &value) { Data($0) }
    }
}

private extension Data {
    mutating func appendLengthPrefixed(_ value: Data) throws {
        guard value.count <= Int(UInt16.max) else {
            throw StableErrorCode.invalidRequest
        }
        var count = UInt16(value.count).bigEndian
        append(withUnsafeBytes(of: &count) { Data($0) })
        append(value)
    }
}
```

创建 `companion/Sources/Phase0Security/TLSChannelBinding.swift`：

```swift
import CryptoKit
import Dispatch
import Foundation
import Network
import Phase0Contracts
import Security

public protocol TLSExporter: Sendable {
    func export(label: String, context: Data, length: Int) throws -> Data
}

public struct SecurityTLSExporter: TLSExporter, @unchecked Sendable {
    private let metadata: sec_protocol_metadata_t

    public init(metadata: sec_protocol_metadata_t) {
        self.metadata = metadata
    }

    public func export(label: String, context: Data, length: Int) throws -> Data {
        guard !label.isEmpty, context.isEmpty, length > 0 else {
            throw StableErrorCode.invalidRequest
        }
        let result: dispatch_data_t? = label.withCString { labelPointer in
            context.withUnsafeBytes { contextPointer in
                sec_protocol_metadata_create_secret_with_context(
                    metadata,
                    label.utf8.count,
                    labelPointer,
                    context.count,
                    contextPointer.bindMemory(to: UInt8.self).baseAddress,
                    length
                )
            }
        }
        guard let result else {
            throw StableErrorCode.unauthenticated
        }
        let bytes = Data(result)
        guard bytes.count == length else {
            throw StableErrorCode.unauthenticated
        }
        return bytes
    }
}

public enum WSSChannelBinding {
    public static let exporterLabel = "EXPORTER-Cardputer-Codex-Companion-v1"

    public static func material(
        exporter: Data,
        companionInstanceID: UUID,
        deviceID: UUID,
        protocolVersion: String,
        challenge: Data
    ) throws -> Data {
        guard exporter.count == 32,
              challenge.count == 32,
              !protocolVersion.isEmpty else {
            throw StableErrorCode.invalidRequest
        }
        var bytes = Data()
        try bytes.appendLengthPrefixed(exporter)
        try bytes.appendLengthPrefixed(
            Data(companionInstanceID.uuidString.lowercased().utf8)
        )
        try bytes.appendLengthPrefixed(
            Data(deviceID.uuidString.lowercased().utf8)
        )
        try bytes.appendLengthPrefixed(Data(protocolVersion.utf8))
        try bytes.appendLengthPrefixed(challenge)
        return bytes
    }

    public static func sign(
        material: Data,
        privateKey: P256.Signing.PrivateKey
    ) throws -> Data {
        try privateKey.signature(for: material).rawRepresentation
    }

    public static func verify(
        signatureRawRS: Data,
        material: Data,
        publicKey: P256.Signing.PublicKey
    ) -> Bool {
        guard signatureRawRS.count == 64 else {
            return false
        }
        guard let signature = try? P256.Signing.ECDSASignature(
            rawRepresentation: signatureRawRS
        ) else {
            return false
        }
        return publicKey.isValidSignature(signature, for: material)
    }
}

private extension UUID {
    var data: Data {
        var value = uuid
        return withUnsafeBytes(of: &value) { Data($0) }
    }
}
```

真实 WSS 路径必须从已建立 `NWConnection` 获取 metadata，再构造 exporter；不得从随机数或 TLS 证书 hash 代替 exporter：

```swift
guard let tls = connection.metadata(
    definition: NWProtocolTLS.definition
) as? NWProtocolTLS.Metadata else {
    throw StableErrorCode.unauthenticated
}
let exporter = SecurityTLSExporter(
    metadata: tls.securityProtocolMetadata
)
let exporterBytes = try exporter.export(
    label: WSSChannelBinding.exporterLabel,
    context: Data(),
    length: 32
)
```

- [ ] **Step 4: 运行安全单元测试**

Run:

```bash
swift test --package-path companion --filter PairingDerivationTests
swift test --package-path companion --filter TLSChannelBindingTests
```

Expected: 两组测试全部通过；任意 transcript 字段变化产生不同 root，protocol version 变化使既有 WSS 签名验证失败。

- [ ] **Step 5: 提交**

```bash
git add companion/Sources/Phase0Security companion/Tests/Phase0SecurityTests
git commit -m "feat: add transcript and tls channel binding probes"
```

## Task 4: 将 WSS/mDNS 限制在选定接口并实现双通道 bind challenge

**Files:**
- Create: `companion/Sources/Phase0Security/LANInterfacePolicy.swift`
- Create: `companion/Sources/Phase0Security/BindChallengeCoordinator.swift`
- Create: `companion/Sources/Phase0Security/WSSPairingProbe.swift`
- Create: `companion/Tests/Phase0SecurityTests/LANInterfacePolicyTests.swift`
- Create: `companion/Tests/Phase0SecurityTests/BindChallengeCoordinatorTests.swift`

**Interfaces:**
- Consumes: 用户选择的 BSD interface name、首次确认时的 interface address/netmask fingerprint、已配对 device ID。
- Produces:
  - `LANInterfacePolicy.confirmed(name: snapshots:) throws -> LANInterfacePolicy`
  - `LANInterfacePolicy.revalidate(snapshots:) throws`
  - `LANInterfacePolicy.allows(remoteIPv4: String) -> Bool`
  - `NWSelectedInterfaceResolver.resolve(name: String) async throws -> NWInterface`
  - `BindChallengeCoordinator.begin(deviceID: UUID, now: Date) throws -> Data`
  - `BindChallengeCoordinator.observe(deviceID: UUID, channel: BindChannel, challenge: Data, now: Date) throws -> BindState`
  - `WSSPairingProbe.run(config: WSSPairingConfig, bindCoordinator: BindChallengeCoordinator) async throws -> WSSPairingEvidence`
- `WSSPairingConfig` 的字段固定为 `policy: LANInterfacePolicy`、`tlsIdentityLabel: String`、`protocolVersion: String`。
- `WSSPairingEvidence` 的字段固定为 `transcriptSHA256: Data`、`sasConfirmedOnMac: Bool`、`sasConfirmedOnCardputer: Bool`、`tlsExporterLength: Int`、`clientSignatureValid: Bool`、`wssChallengeSeen: Bool`、`bindChallengeComplete: Bool`、`listenerInterface: String`、`mdnsInterface: String`、`interfaceFingerprintUnchanged: Bool`、`remoteInSelectedSubnet: Bool`、`publicListenerDetected: Bool`。
- `BindState.complete` 只有在 WSS 和 GATT 对同一 device ID、同一 32-byte challenge、同一未过期 attempt 都已观察到时返回。

- [ ] **Step 1: 写入失败的接口与 bind 测试**

创建 `companion/Tests/Phase0SecurityTests/LANInterfacePolicyTests.swift`：

```swift
import XCTest
@testable import Phase0Security

final class LANInterfacePolicyTests: XCTestCase {
    private let en0 = InterfaceSnapshot(
        name: "en0",
        ipv4Address: "192.168.40.12",
        ipv4Netmask: "255.255.255.0"
    )

    func testOnlySelectedLocalSubnetIsAllowed() throws {
        let policy = try LANInterfacePolicy.confirmed(name: "en0", snapshots: [en0])
        XCTAssertTrue(policy.allows(remoteIPv4: "192.168.40.99"))
        XCTAssertFalse(policy.allows(remoteIPv4: "192.168.41.1"))
        XCTAssertFalse(policy.allows(remoteIPv4: "8.8.8.8"))
        XCTAssertFalse(policy.allows(remoteIPv4: "127.0.0.1"))
    }

    func testInterfaceAddressChangeRequiresConfirmation() throws {
        let policy = try LANInterfacePolicy.confirmed(name: "en0", snapshots: [en0])
        XCTAssertThrowsError(
            try policy.revalidate(
                snapshots: [
                    InterfaceSnapshot(
                        name: "en0",
                        ipv4Address: "10.0.0.12",
                        ipv4Netmask: "255.255.255.0"
                    )
                ]
            )
        )
    }

    func testMissingSelectedInterfaceDoesNotFallBack() throws {
        let policy = try LANInterfacePolicy.confirmed(name: "en0", snapshots: [en0])
        XCTAssertThrowsError(try policy.revalidate(snapshots: []))
    }
}
```

创建 `companion/Tests/Phase0SecurityTests/BindChallengeCoordinatorTests.swift`：

```swift
import Foundation
import XCTest
@testable import Phase0Security
import Phase0Contracts

final class BindChallengeCoordinatorTests: XCTestCase {
    func testBothChannelsForSameIdentityCompleteBinding() throws {
        let coordinator = BindChallengeCoordinator()
        let device = UUID(uuidString: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb")!
        let started = Date(timeIntervalSince1970: 100)
        let challenge = try coordinator.begin(deviceID: device, now: started)
        XCTAssertEqual(
            try coordinator.observe(
                deviceID: device,
                channel: .wss,
                challenge: challenge,
                now: started.addingTimeInterval(1)
            ),
            .waitingForGATT
        )
        XCTAssertEqual(
            try coordinator.observe(
                deviceID: device,
                channel: .gatt,
                challenge: challenge,
                now: started.addingTimeInterval(2)
            ),
            .complete
        )
    }

    func testMismatchIdentityChallengeAndExpiryAreRejected() throws {
        let coordinator = BindChallengeCoordinator()
        let device = UUID()
        let challenge = try coordinator.begin(
            deviceID: device,
            now: Date(timeIntervalSince1970: 100)
        )
        XCTAssertThrowsError(
            try coordinator.observe(
                deviceID: UUID(),
                channel: .wss,
                challenge: challenge,
                now: Date(timeIntervalSince1970: 101)
            )
        )
        XCTAssertThrowsError(
            try coordinator.observe(
                deviceID: device,
                channel: .gatt,
                challenge: Data(repeating: 0x44, count: 32),
                now: Date(timeIntervalSince1970: 102)
            )
        )
        XCTAssertThrowsError(
            try coordinator.observe(
                deviceID: device,
                channel: .gatt,
                challenge: challenge,
                now: Date(timeIntervalSince1970: 161)
            )
        )
    }
}
```

- [ ] **Step 2: 运行测试确认 policy 与 coordinator 尚不存在**

Run:

```bash
swift test --package-path companion --filter LANInterfacePolicyTests
swift test --package-path companion --filter BindChallengeCoordinatorTests
```

Expected: FAIL，错误包含 `cannot find 'InterfaceSnapshot' in scope` 和 `cannot find 'BindChallengeCoordinator' in scope`。

- [ ] **Step 3: 实现 CIDR 约束、interface fingerprint 和 challenge 状态机**

创建 `companion/Sources/Phase0Security/LANInterfacePolicy.swift`：

```swift
import Foundation
import Network
import Phase0Contracts

public struct InterfaceSnapshot: Equatable, Sendable {
    public let name: String
    public let ipv4Address: String
    public let ipv4Netmask: String

    public init(name: String, ipv4Address: String, ipv4Netmask: String) {
        self.name = name
        self.ipv4Address = ipv4Address
        self.ipv4Netmask = ipv4Netmask
    }
}

public struct LANInterfacePolicy: Sendable {
    public let selectedName: String
    public let confirmedSnapshot: InterfaceSnapshot
    private let network: UInt32
    private let mask: UInt32

    public static func confirmed(
        name: String,
        snapshots: [InterfaceSnapshot]
    ) throws -> LANInterfacePolicy {
        guard let snapshot = snapshots.first(where: { $0.name == name }),
              let address = IPv4.parse(snapshot.ipv4Address),
              let mask = IPv4.parse(snapshot.ipv4Netmask),
              IPv4.isContiguous(mask: mask),
              (address & mask) != IPv4.parse("127.0.0.0") else {
            throw StableErrorCode.interfaceChanged
        }
        return LANInterfacePolicy(
            selectedName: name,
            confirmedSnapshot: snapshot,
            network: address & mask,
            mask: mask
        )
    }

    private init(
        selectedName: String,
        confirmedSnapshot: InterfaceSnapshot,
        network: UInt32,
        mask: UInt32
    ) {
        self.selectedName = selectedName
        self.confirmedSnapshot = confirmedSnapshot
        self.network = network
        self.mask = mask
    }

    public func revalidate(snapshots: [InterfaceSnapshot]) throws {
        guard snapshots.contains(confirmedSnapshot) else {
            throw StableErrorCode.interfaceChanged
        }
    }

    public func allows(remoteIPv4: String) -> Bool {
        guard let remote = IPv4.parse(remoteIPv4) else {
            return false
        }
        return (remote & mask) == network && (remote >> 24) != 127
    }
}

public enum NWSelectedInterfaceResolver {
    public static func resolve(name: String) async throws -> NWInterface {
        try await withCheckedThrowingContinuation { continuation in
            let monitor = NWPathMonitor()
            let queue = DispatchQueue(label: "phase0.interface.resolve")
            monitor.pathUpdateHandler = { path in
                monitor.cancel()
                if let value = path.availableInterfaces.first(where: { $0.name == name }) {
                    continuation.resume(returning: value)
                } else {
                    continuation.resume(throwing: StableErrorCode.interfaceChanged)
                }
            }
            monitor.start(queue: queue)
        }
    }
}

private enum IPv4 {
    static func parse(_ value: String) -> UInt32? {
        let parts = value.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 4 else {
            return nil
        }
        var output: UInt32 = 0
        for part in parts {
            guard let byte = UInt8(part) else {
                return nil
            }
            output = (output << 8) | UInt32(byte)
        }
        return output
    }

    static func isContiguous(mask: UInt32) -> Bool {
        let inverse = ~mask
        return (inverse & (inverse &+ 1)) == 0
    }
}
```

创建 `companion/Sources/Phase0Security/BindChallengeCoordinator.swift`：

```swift
import Foundation
import Phase0Contracts
import Security

public enum BindChannel: Sendable {
    case wss
    case gatt
}

public enum BindState: Equatable, Sendable {
    case waitingForWSS
    case waitingForGATT
    case complete
}

public final class BindChallengeCoordinator: @unchecked Sendable {
    private struct Attempt {
        let deviceID: UUID
        let challenge: Data
        let expiresAt: Date
        var sawWSS: Bool
        var sawGATT: Bool
    }

    private let lock = NSLock()
    private var attempt: Attempt?

    public init() {}

    public func begin(deviceID: UUID, now: Date) throws -> Data {
        var challenge = Data(repeating: 0, count: 32)
        let status = challenge.withUnsafeMutableBytes {
            SecRandomCopyBytes(kSecRandomDefault, 32, $0.baseAddress!)
        }
        guard status == errSecSuccess else {
            throw StableErrorCode.indeterminate
        }
        lock.lock()
        attempt = Attempt(
            deviceID: deviceID,
            challenge: challenge,
            expiresAt: now.addingTimeInterval(60),
            sawWSS: false,
            sawGATT: false
        )
        lock.unlock()
        return challenge
    }

    public func observe(
        deviceID: UUID,
        channel: BindChannel,
        challenge: Data,
        now: Date
    ) throws -> BindState {
        lock.lock()
        defer { lock.unlock() }
        guard var current = attempt,
              now <= current.expiresAt,
              current.deviceID == deviceID,
              constantTimeEqual(current.challenge, challenge) else {
            throw StableErrorCode.unauthenticated
        }
        switch channel {
        case .wss:
            current.sawWSS = true
        case .gatt:
            current.sawGATT = true
        }
        attempt = current
        if current.sawWSS && current.sawGATT {
            attempt = nil
            return .complete
        }
        return current.sawWSS ? .waitingForGATT : .waitingForWSS
    }

    private func constantTimeEqual(_ left: Data, _ right: Data) -> Bool {
        guard left.count == right.count else {
            return false
        }
        var difference: UInt8 = 0
        for (a, b) in zip(left, right) {
            difference |= a ^ b
        }
        return difference == 0
    }
}
```

WSS listener 的真实构造必须复用已经配置好本地 `sec_identity_t` 的 TLS options，并把 `requiredInterface` 设置为刚解析出的 `NWInterface`：

```swift
let selected = try await NWSelectedInterfaceResolver.resolve(
    name: policy.selectedName
)
let parameters = NWParameters(tls: configuredTLSOptions)
parameters.requiredInterface = selected
parameters.includePeerToPeer = false
let webSocket = NWProtocolWebSocket.Options()
webSocket.autoReplyPing = true
parameters.defaultProtocolStack.applicationProtocols.insert(webSocket, at: 0)
let listener = try NWListener(using: parameters, on: .any)
listener.service = NWListener.Service(
    name: instanceName,
    type: "_codex-companion._tcp",
    domain: "local",
    txtRecord: txtRecord
)
```

每个 `newConnectionHandler` 在 `start` 前必须从 endpoint 提取远端 IPv4 并调用 `policy.allows(remoteIPv4:)`；不允许时直接 `connection.cancel()`。`NWPathMonitor` 报告选定接口消失或 address/netmask fingerprint 改变时立即 cancel listener、撤销未完成 challenge，并向 HIL 结果写 `interface_changed`。

- [ ] **Step 4: 运行接口与 bind 测试**

Run:

```bash
swift test --package-path companion --filter LANInterfacePolicyTests
swift test --package-path companion --filter BindChallengeCoordinatorTests
```

Expected: 两组测试全部通过；外部地址、loopback、接口漂移、身份不一致、challenge 不一致和第 61 秒响应均被拒绝。

- [ ] **Step 5: 提交**

```bash
git add companion/Sources/Phase0Security companion/Tests/Phase0SecurityTests
git commit -m "feat: confine pairing probe to selected lan"
```

## Task 5: 实现先认证后 replay 的 GATT receiver、真实 notify 路径与 HID 身份证明

**Files:**
- Create: `companion/Sources/Phase0GATT/GATTFrame.swift`
- Create: `companion/Sources/Phase0GATT/BluetoothProbeSession.swift`
- Create: `companion/Sources/Phase0GATT/ReplayWindow.swift`
- Create: `companion/Sources/Phase0GATT/GATTFrameReceiver.swift`
- Create: `companion/Sources/Phase0GATT/CoreBluetoothProbeClient.swift`
- Create: `companion/Sources/Phase0GATT/HIDIdentityReader.swift`
- Create: `companion/Tests/Phase0GATTTests/ReplayWindowTests.swift`
- Create: `companion/Tests/Phase0GATTTests/GATTFrameReceiverTests.swift`
- Create: `companion/Tests/Phase0GATTTests/IdentityBindingTests.swift`
- Consume read-only: `protocol/phase0/fixtures/gatt-auth-v1.json`

**Interfaces:**
- Consumes:
  - 32-byte `gatt-auth` key；
  - 每次连接新生成的 16-byte `GATTConnectionContext.connectionID`；
  - 固件按本文固定 frame layout 发出的 notify；
  - HID serial 解码出的原始 16-byte `device_id` 和 GATT Identity Characteristic 的原始 16 bytes。
- Produces:
  - `GATTFrame.parseBounded(_:) throws -> GATTFrame`
  - `GATTFrame.authenticate(key:connectionID:) -> Bool`
  - `ReplayWindow.accept(_ counter: UInt64) throws`
  - `GATTFrameReceiver.receive(_ bytes: Data, context: GATTConnectionContext) async throws -> GATTReceiveResult`
  - `AuthenticatedTextSink.beginAuthenticatedText(_:) async throws -> InjectionResult`
  - `CoreBluetoothProbeClient.start(expectedPeripheralIdentifier:expectedDeviceID:gattAuthKey:)`
  - `CoreBluetoothProbeClient.run(config: BluetoothProbeConfig, timeout: Duration) async throws -> BluetoothProbeEvidence`
- `BluetoothProbeConfig` 的字段固定为 `peripheralIdentifier: UUID`、`expectedDeviceID: Data`、`pairedDeviceID: String`、`gattAuthKey: SymmetricKey`、`bindCoordinator: BindChallengeCoordinator`。
- `BluetoothProbeEvidence` 的字段固定为 `notifyCallbacks: Int`、`authenticatedFrames: Int`、`protectedCharacteristicAccess: Bool`、`hidDeviceIDs: [Data]`、`gattDeviceID: Data`、`badMACAdvancedCounter: Bool`、`replayRejected: Bool`、`gattChallengeSeen: Bool`、`bindChallengeComplete: Bool`。
  - `HIDIdentityReader.readKeyboardDeviceIDs() throws -> [Data]`
  - `IdentityBinder.bind(hidDeviceIDs:gattDeviceID:) throws -> Data`
- Receiver 调用 sink 的时点就是进入 SQLite `intent` 与 Unicode Injector 的边界；坏 MAC、replay、未完成分片和 hash 错误均不得调用 sink。
- Swift parser/HMAC/replay tests must decode the exact foundation `gatt-auth-v1.json` vector and match its bytes/tag. A locally generated Swift-only fixture is forbidden.

- [ ] **Step 1: 写入失败的 replay、认证顺序和身份测试**

创建 `companion/Tests/Phase0GATTTests/ReplayWindowTests.swift`：

```swift
import XCTest
@testable import Phase0GATT
import Phase0Contracts

final class ReplayWindowTests: XCTestCase {
    func testAcceptsFirstZeroForwardAndLimitedOutOfOrderCounters() throws {
        var window = ReplayWindow(width: 32)
        try window.accept(0)
        try window.accept(2)
        try window.accept(1)
        XCTAssertEqual(window.highestAccepted, 2)
    }

    func testRejectsDuplicateTooOldAndNonzeroFirstCounter() throws {
        var first = ReplayWindow(width: 32)
        XCTAssertThrowsError(try first.accept(1))

        var window = ReplayWindow(width: 32)
        try window.accept(0)
        try window.accept(32)
        XCTAssertThrowsError(try window.accept(32))
        XCTAssertThrowsError(try window.accept(0))

        var jump = ReplayWindow(width: 32)
        try jump.accept(0)
        XCTAssertThrowsError(try jump.accept(33))
    }
}
```

创建 `companion/Tests/Phase0GATTTests/GATTFrameReceiverTests.swift`：

```swift
import CryptoKit
import Foundation
import XCTest
@testable import Phase0GATT
import Phase0Contracts

private actor RecordingSink: AuthenticatedTextSink {
    private(set) var operations: [AuthenticatedTextOperation] = []

    func beginAuthenticatedText(
        _ operation: AuthenticatedTextOperation
    ) async throws -> InjectionResult {
        operations.append(operation)
        return InjectionResult(
            operationID: operation.operationID,
            status: .accepted,
            postedPrefixLength: 0,
            verifiedPrefixLength: 0,
            errorCode: nil
        )
    }

    func count() -> Int {
        operations.count
    }
}

final class GATTFrameReceiverTests: XCTestCase {
    private let key = SymmetricKey(data: Data(repeating: 0x19, count: 32))
    private let context = GATTConnectionContext(
        pairedDeviceID: "device-a",
        connectionID: Data(repeating: 0x27, count: 16)
    )

    func testBadMACDoesNotConsumeCounterReassemblyOrSink() async throws {
        let sink = RecordingSink()
        let receiver = GATTFrameReceiver(authKey: key, sink: sink)
        let operationID = UUID()
        let payload = Data("中文".utf8)
        var bytes = try GATTFrame.encodeAuthenticated(
            counter: 0,
            operationID: operationID,
            fragmentIndex: 0,
            fragmentCount: 1,
            totalUTF8Length: payload.count,
            fragment: payload,
            fullMessageSHA256: Data(SHA256.hash(data: payload)),
            key: key,
            connectionID: context.connectionID
        )
        bytes[40] ^= 0x01

        do {
            _ = try await receiver.receive(bytes, context: context)
            XCTFail("bad MAC was accepted")
        } catch {
            XCTAssertEqual(error as? StableErrorCode, .unauthenticated)
        }
        XCTAssertNil(await receiver.highestAcceptedCounter())
        XCTAssertEqual(await receiver.pendingOperationCount(), 0)
        XCTAssertEqual(await sink.count(), 0)

        let valid = try GATTFrame.encodeAuthenticated(
            counter: 0,
            operationID: operationID,
            fragmentIndex: 0,
            fragmentCount: 1,
            totalUTF8Length: payload.count,
            fragment: payload,
            fullMessageSHA256: Data(SHA256.hash(data: payload)),
            key: key,
            connectionID: context.connectionID
        )
        XCTAssertEqual(
            try await receiver.receive(valid, context: context),
            .accepted(.accepted)
        )
        XCTAssertEqual(await sink.count(), 1)
    }

    func testFragmentsReassembleByIndexAndDuplicateCounterIsRejected() async throws {
        let sink = RecordingSink()
        let receiver = GATTFrameReceiver(authKey: key, sink: sink)
        let operationID = UUID()
        let payload = Data("Cardputer中文".utf8)
        let split = 5
        let first = payload.prefix(split)
        let second = payload.dropFirst(split)
        let hash = Data(SHA256.hash(data: payload))
        let frame1 = try GATTFrame.encodeAuthenticated(
            counter: 0,
            operationID: operationID,
            fragmentIndex: 1,
            fragmentCount: 2,
            totalUTF8Length: payload.count,
            fragment: Data(second),
            fullMessageSHA256: hash,
            key: key,
            connectionID: context.connectionID
        )
        let frame0 = try GATTFrame.encodeAuthenticated(
            counter: 1,
            operationID: operationID,
            fragmentIndex: 0,
            fragmentCount: 2,
            totalUTF8Length: payload.count,
            fragment: Data(first),
            fullMessageSHA256: hash,
            key: key,
            connectionID: context.connectionID
        )

        XCTAssertEqual(try await receiver.receive(frame1, context: context), .waiting)
        XCTAssertEqual(
            try await receiver.receive(frame0, context: context),
            .accepted(.accepted)
        )
        XCTAssertEqual(await sink.count(), 1)
        do {
            _ = try await receiver.receive(frame0, context: context)
            XCTFail("replayed frame was accepted")
        } catch {
            XCTAssertEqual(error as? StableErrorCode, .replay)
        }
        XCTAssertEqual(await sink.count(), 1)
    }

    func testDisconnectDiscardsConnectionCounterAndFragments() async throws {
        let sink = RecordingSink()
        let receiver = GATTFrameReceiver(authKey: key, sink: sink)
        await receiver.disconnect(connectionID: context.connectionID)
        XCTAssertNil(await receiver.highestAcceptedCounter())
        XCTAssertEqual(await receiver.pendingOperationCount(), 0)
    }
}
```

创建 `companion/Tests/Phase0GATTTests/IdentityBindingTests.swift`：

```swift
import Foundation
import XCTest
@testable import Phase0GATT
import Phase0Contracts

final class IdentityBindingTests: XCTestCase {
    func testExactlyOneDecodedHIDSerialMustMatchProtectedGATTIdentity() throws {
        let expected = Data(repeating: 0x51, count: 16)
        let other = Data(repeating: 0x52, count: 16)
        XCTAssertEqual(
            try IdentityBinder.bind(
                hidDeviceIDs: [other, expected],
                gattDeviceID: expected
            ),
            expected
        )
        XCTAssertThrowsError(
            try IdentityBinder.bind(
                hidDeviceIDs: [other],
                gattDeviceID: expected
            )
        ) {
            XCTAssertEqual($0 as? StableErrorCode, .identityMismatch)
        }
        XCTAssertThrowsError(
            try IdentityBinder.bind(
                hidDeviceIDs: [expected, expected],
                gattDeviceID: expected
            )
        )
    }
}
```

- [ ] **Step 2: 运行测试确认 GATT API 尚不存在**

Run:

```bash
swift test --package-path companion --filter ReplayWindowTests
swift test --package-path companion --filter GATTFrameReceiverTests
swift test --package-path companion --filter IdentityBindingTests
```

Expected: FAIL，错误至少包含 `cannot find 'ReplayWindow' in scope`、`cannot find type 'AuthenticatedTextSink'` 和 `cannot find 'IdentityBinder' in scope`。

- [ ] **Step 3: 实现有界 frame 解析、MAC 和 replay window**

将 `companion/Sources/Phase0GATT/GATTFrame.swift` 写为：

```swift
import CryptoKit
import Foundation
import Phase0Contracts

public struct GATTFrame: Sendable {
    public static let minimumSize = 100
    public static let maximumSize = 512
    public static let authenticationLabel = Data(
        "cardputer-codex/gatt-auth/v1".utf8
    )

    public let connectionID: Data
    public let counter: UInt64
    public let operationID: UUID
    public let fragmentIndex: Int
    public let fragmentCount: Int
    public let totalUTF8Length: Int
    public let fragment: Data
    public let fullMessageSHA256: Data
    private let authenticatedBytes: Data
    private let tag: Data

    public static func parseBounded(_ bytes: Data) throws -> GATTFrame {
        guard (minimumSize...maximumSize).contains(bytes.count),
              bytes[0] == 1 else {
            throw StableErrorCode.malformedFrame
        }
        let fragmentLength = Int(bytes.uint16(at: 82))
        guard fragmentLength <= 412,
              bytes.count == 100 + fragmentLength else {
            throw StableErrorCode.malformedFrame
        }
        let count = Int(bytes.uint16(at: 44))
        let index = Int(bytes.uint16(at: 42))
        let total = Int(bytes.uint32(at: 46))
        guard (1...64).contains(count),
              index < count,
              (0...1024).contains(total),
              fragmentLength <= total else {
            throw StableErrorCode.malformedFrame
        }
        let uuidBytes = Array(bytes[18..<34])
        let uuid = UUID(uuid: (
            uuidBytes[0], uuidBytes[1], uuidBytes[2], uuidBytes[3],
            uuidBytes[4], uuidBytes[5], uuidBytes[6], uuidBytes[7],
            uuidBytes[8], uuidBytes[9], uuidBytes[10], uuidBytes[11],
            uuidBytes[12], uuidBytes[13], uuidBytes[14], uuidBytes[15]
        ))
        let bodyEnd = 84 + fragmentLength
        return GATTFrame(
            connectionID: bytes.subdata(in: 2..<18),
            counter: bytes.uint64(at: 34),
            operationID: uuid,
            fragmentIndex: index,
            fragmentCount: count,
            totalUTF8Length: total,
            fragment: bytes.subdata(in: 84..<bodyEnd),
            fullMessageSHA256: bytes.subdata(in: 50..<82),
            authenticatedBytes: bytes.subdata(in: 0..<bodyEnd),
            tag: bytes.suffix(16)
        )
    }

    public func authenticate(
        key: SymmetricKey,
        connectionID: Data
    ) -> Bool {
        guard connectionID.count == 16 else {
            return false
        }
        guard constantTimeEqual(self.connectionID, connectionID) else {
            return false
        }
        var input = Self.authenticationLabel
        input.append(authenticatedBytes)
        let expected = Data(HMAC<SHA256>.authenticationCode(for: input, using: key))
            .prefix(16)
        return constantTimeEqual(Data(expected), tag)
    }

    public static func encodeAuthenticated(
        counter: UInt64,
        operationID: UUID,
        fragmentIndex: Int,
        fragmentCount: Int,
        totalUTF8Length: Int,
        fragment: Data,
        fullMessageSHA256: Data,
        key: SymmetricKey,
        connectionID: Data
    ) throws -> Data {
        guard fragmentIndex >= 0,
              fragmentIndex < fragmentCount,
              (1...64).contains(fragmentCount),
              (0...1024).contains(totalUTF8Length),
              fragment.count <= 412,
              fullMessageSHA256.count == 32,
              connectionID.count == 16 else {
            throw StableErrorCode.invalidRequest
        }
        var bytes = Data([1, 0])
        bytes.append(connectionID)
        var uuid = operationID.uuid
        bytes.append(withUnsafeBytes(of: &uuid) { Data($0) })
        bytes.appendBigEndian(counter)
        bytes.appendBigEndian(UInt16(fragmentIndex))
        bytes.appendBigEndian(UInt16(fragmentCount))
        bytes.appendBigEndian(UInt32(totalUTF8Length))
        bytes.append(fullMessageSHA256)
        bytes.appendBigEndian(UInt16(fragment.count))
        bytes.append(fragment)
        var authenticated = authenticationLabel
        authenticated.append(bytes)
        bytes.append(
            Data(HMAC<SHA256>.authenticationCode(for: authenticated, using: key))
                .prefix(16)
        )
        return bytes
    }

    private func constantTimeEqual(_ left: Data, _ right: Data) -> Bool {
        guard left.count == right.count else {
            return false
        }
        var difference: UInt8 = 0
        for (a, b) in zip(left, right) {
            difference |= a ^ b
        }
        return difference == 0
    }
}

private extension Data {
    func uint16(at offset: Int) -> UInt16 {
        (UInt16(self[offset]) << 8) | UInt16(self[offset + 1])
    }

    func uint32(at offset: Int) -> UInt32 {
        (UInt32(self[offset]) << 24)
            | (UInt32(self[offset + 1]) << 16)
            | (UInt32(self[offset + 2]) << 8)
            | UInt32(self[offset + 3])
    }

    func uint64(at offset: Int) -> UInt64 {
        (0..<8).reduce(UInt64(0)) {
            ($0 << 8) | UInt64(self[offset + $1])
        }
    }

    mutating func appendBigEndian<T: FixedWidthInteger>(_ value: T) {
        var bigEndian = value.bigEndian
        append(withUnsafeBytes(of: &bigEndian) { Data($0) })
    }
}
```

创建 `companion/Sources/Phase0GATT/ReplayWindow.swift`：

```swift
import Foundation
import Phase0Contracts

public struct ReplayWindow: Sendable {
    private let width: UInt64
    private var bitmap: UInt64 = 0
    public private(set) var highestAccepted: UInt64?

    public init(width: UInt64) {
        precondition((1...64).contains(width))
        self.width = width
    }

    public mutating func accept(_ counter: UInt64) throws {
        guard let highest = highestAccepted else {
            guard counter == 0 else {
                throw StableErrorCode.replay
            }
            highestAccepted = 0
            bitmap = 1
            return
        }
        if counter > highest {
            let advance = counter - highest
            guard advance <= width else {
                throw StableErrorCode.replay
            }
            bitmap = advance >= width ? 1 : (bitmap << advance) | 1
            highestAccepted = counter
            return
        }
        let distance = highest - counter
        guard distance < width else {
            throw StableErrorCode.replay
        }
        let bit = UInt64(1) << distance
        guard bitmap & bit == 0 else {
            throw StableErrorCode.replay
        }
        bitmap |= bit
    }
}
```

- [ ] **Step 4: 实现重组器和严格顺序的 receiver**

创建 `companion/Sources/Phase0GATT/GATTFrameReceiver.swift`：

```swift
import CryptoKit
import Foundation
import Phase0Contracts

public struct GATTConnectionContext: Equatable, Sendable {
    public let pairedDeviceID: String
    public let connectionID: Data

    public init(pairedDeviceID: String, connectionID: Data) {
        self.pairedDeviceID = pairedDeviceID
        self.connectionID = connectionID
    }
}

public struct AuthenticatedTextOperation: Sendable {
    public let pairedDeviceID: String
    public let operationID: UUID
    public let payload: Data
    public let payloadSHA256: Data
}

public protocol AuthenticatedTextSink: Sendable {
    func beginAuthenticatedText(
        _ operation: AuthenticatedTextOperation
    ) async throws -> InjectionResult
}

public enum GATTReceiveResult: Equatable, Sendable {
    case waiting
    case accepted(OperationStatus)
}

public actor GATTFrameReceiver {
    private struct Pending {
        let fragmentCount: Int
        let totalLength: Int
        let hash: Data
        var fragments: [Int: Data]
    }

    private let authKey: SymmetricKey
    private let sink: AuthenticatedTextSink
    private var activeConnectionID: Data?
    private var replay = ReplayWindow(width: 32)
    private var pending: [UUID: Pending] = [:]

    public init(authKey: SymmetricKey, sink: AuthenticatedTextSink) {
        self.authKey = authKey
        self.sink = sink
    }

    public func receive(
        _ bytes: Data,
        context: GATTConnectionContext
    ) async throws -> GATTReceiveResult {
        if activeConnectionID == nil {
            activeConnectionID = context.connectionID
        }
        guard context.connectionID.count == 16,
              activeConnectionID == context.connectionID else {
            throw StableErrorCode.unauthenticated
        }

        let frame = try GATTFrame.parseBounded(bytes)
        guard frame.authenticate(key: authKey, connectionID: context.connectionID) else {
            throw StableErrorCode.unauthenticated
        }

        try replay.accept(frame.counter)
        let complete = try addAuthenticated(frame)
        guard let complete else {
            return .waiting
        }
        let result = try await sink.beginAuthenticatedText(
            AuthenticatedTextOperation(
                pairedDeviceID: context.pairedDeviceID,
                operationID: frame.operationID,
                payload: complete,
                payloadSHA256: frame.fullMessageSHA256
            )
        )
        return .accepted(result.status)
    }

    public func disconnect(connectionID: Data) {
        guard activeConnectionID == connectionID else {
            return
        }
        activeConnectionID = nil
        replay = ReplayWindow(width: 32)
        pending.removeAll(keepingCapacity: false)
    }

    public func highestAcceptedCounter() -> UInt64? {
        replay.highestAccepted
    }

    public func pendingOperationCount() -> Int {
        pending.count
    }

    private func addAuthenticated(_ frame: GATTFrame) throws -> Data? {
        if pending[frame.operationID] == nil {
            guard pending.count < 4 else {
                throw StableErrorCode.invalidRequest
            }
            pending[frame.operationID] = Pending(
                fragmentCount: frame.fragmentCount,
                totalLength: frame.totalUTF8Length,
                hash: frame.fullMessageSHA256,
                fragments: [:]
            )
        }
        guard var value = pending[frame.operationID],
              value.fragmentCount == frame.fragmentCount,
              value.totalLength == frame.totalUTF8Length,
              value.hash == frame.fullMessageSHA256,
              value.fragments[frame.fragmentIndex] == nil else {
            throw StableErrorCode.invalidRequest
        }
        value.fragments[frame.fragmentIndex] = frame.fragment
        let receivedLength = value.fragments.values.reduce(0) { $0 + $1.count }
        guard receivedLength <= value.totalLength else {
            pending.removeValue(forKey: frame.operationID)
            throw StableErrorCode.invalidRequest
        }
        pending[frame.operationID] = value
        guard value.fragments.count == value.fragmentCount else {
            return nil
        }
        var message = Data()
        for index in 0..<value.fragmentCount {
            guard let fragment = value.fragments[index] else {
                throw StableErrorCode.invalidRequest
            }
            message.append(fragment)
        }
        pending.removeValue(forKey: frame.operationID)
        guard message.count == value.totalLength,
              Data(SHA256.hash(data: message)) == value.hash,
              String(data: message, encoding: .utf8) != nil else {
            throw StableErrorCode.invalidRequest
        }
        return message
    }
}
```

receiver 的代码审查门禁是源文件中以下调用顺序保持不变：

```text
GATTFrame.parseBounded
frame.authenticate
replay.accept
addAuthenticated
sink.beginAuthenticatedText
```

任何为了“优化”而将 `replay.accept`、`addAuthenticated` 或 sink 移到 `authenticate` 前面的变更必须拒绝。

- [ ] **Step 5: 实现 CoreBluetooth 真 notify 和 IOHID HID-serial 身份路径**

创建 `companion/Sources/Phase0GATT/HIDIdentityReader.swift`：

```swift
import Foundation
import IOKit.hid
import Phase0Contracts

public enum IdentityBinder {
    public static func bind(
        hidDeviceIDs: [Data],
        gattDeviceID: Data
    ) throws -> Data {
        guard gattDeviceID.count == 16 else {
            throw StableErrorCode.identityMismatch
        }
        let matches = hidDeviceIDs.filter {
            constantTimeEqual($0, gattDeviceID)
        }
        guard matches.count == 1 else {
            throw StableErrorCode.identityMismatch
        }
        return matches[0]
    }

    private static func constantTimeEqual(_ left: Data, _ right: Data) -> Bool {
        guard left.count == right.count else {
            return false
        }
        var difference: UInt8 = 0
        for (a, b) in zip(left, right) {
            difference |= a ^ b
        }
        return difference == 0
    }
}

public enum HIDIdentityReader {
    public static func readKeyboardDeviceIDs() throws -> [Data] {
        let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let matching: [String: Any] = [
            kIOHIDTransportKey: "Bluetooth",
            kIOHIDDeviceUsagePageKey: kHIDPage_GenericDesktop,
            kIOHIDDeviceUsageKey: kHIDUsage_GD_Keyboard
        ]
        IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)
        guard IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone)) == kIOReturnSuccess,
              let values = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice> else {
            throw StableErrorCode.permissionDenied
        }
        defer {
            IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        }
        return values.compactMap { device in
            guard let serial = IOHIDDeviceGetProperty(
                device,
                kIOHIDSerialNumberKey as CFString
            ) as? String,
                  let decoded = Base32NoPadding.decode(serial),
                  decoded.count == 16 else {
                return nil
            }
            return decoded
        }
    }
}
```

`Base32NoPadding.decode` accepts uppercase RFC 4648 alphabet only, rejects padding, aliases, non-canonical trailing bits and any decoded length other than 16 bytes. The HIL report stores only `SHA-256(device_id)`; it never stores the HID serial or raw `device_id`.

创建 `companion/Sources/Phase0GATT/CoreBluetoothProbeClient.swift`，所有帧只从 `didUpdateValueFor` 进入 receiver：

```swift
import CoreBluetooth
import CryptoKit
import Foundation
import Phase0Contracts
import Security

@MainActor
public final class CoreBluetoothProbeClient: NSObject {
    public static let serviceUUID = CBUUID(
        string: "7a100001-2c4d-4f20-9f20-434f44455831"
    )
    public static let notifyUUID = CBUUID(
        string: "7a100002-2c4d-4f20-9f20-434f44455831"
    )
    public static let controlUUID = CBUUID(
        string: "7a100003-2c4d-4f20-9f20-434f44455831"
    )
    public static let identityUUID = CBUUID(
        string: "7a100004-2c4d-4f20-9f20-434f44455831"
    )

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var expectedPeripheralIdentifier: UUID!
    private var expectedDeviceID = Data()
    private var receiver: GATTFrameReceiver!
    private var context: GATTConnectionContext!
    private var controlCharacteristic: CBCharacteristic?
    private var observedDeviceID = Data()
    private var notifyEnabled = false
    public private(set) var notifyCallbackCount = 0
    public var evidenceHandler: @Sendable (String, Bool) -> Void = { _, _ in }

    public override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    public func start(
        expectedPeripheralIdentifier: UUID,
        expectedDeviceID: Data,
        pairedDeviceID: String,
        gattAuthKey: SymmetricKey
    ) throws {
        guard expectedDeviceID.count == 16 else {
            throw StableErrorCode.invalidRequest
        }
        var connectionID = Data(repeating: 0, count: 16)
        guard connectionID.withUnsafeMutableBytes({
            SecRandomCopyBytes(kSecRandomDefault, 16, $0.baseAddress!)
        }) == errSecSuccess else {
            throw StableErrorCode.indeterminate
        }
        self.expectedPeripheralIdentifier = expectedPeripheralIdentifier
        self.expectedDeviceID = expectedDeviceID
        self.context = GATTConnectionContext(
            pairedDeviceID: pairedDeviceID,
            connectionID: connectionID
        )
        self.receiver = GATTFrameReceiver(
            authKey: gattAuthKey,
            sink: ProbeSinkRegistry.shared
        )
        if central.state == .poweredOn {
            central.scanForPeripherals(
                withServices: [Self.serviceUUID],
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
        }
    }
}

extension CoreBluetoothProbeClient: CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        evidenceHandler("bluetooth_powered_on", central.state == .poweredOn)
        guard central.state == .poweredOn,
              expectedPeripheralIdentifier != nil else {
            return
        }
        central.scanForPeripherals(
            withServices: [Self.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard peripheral.identifier == expectedPeripheralIdentifier else {
            return
        }
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        central.connect(peripheral)
    }

    public func centralManager(
        _ central: CBCentralManager,
        didConnect peripheral: CBPeripheral
    ) {
        peripheral.discoverServices([Self.serviceUUID])
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: Error?
    ) {
        let connectionID = context.connectionID
        Task {
            await receiver.disconnect(connectionID: connectionID)
        }
        evidenceHandler("gatt_disconnected", true)
    }
}

extension CoreBluetoothProbeClient: CBPeripheralDelegate {
    public func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverServices error: Error?
    ) {
        guard error == nil,
              let service = peripheral.services?.first(where: {
                  $0.uuid == Self.serviceUUID
              }) else {
            evidenceHandler("service_discovered", false)
            return
        }
        peripheral.discoverCharacteristics(
            [Self.notifyUUID, Self.controlUUID, Self.identityUUID],
            for: service
        )
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        guard error == nil, let characteristics = service.characteristics else {
            evidenceHandler("characteristics_discovered", false)
            return
        }
        for characteristic in characteristics {
            switch characteristic.uuid {
            case Self.identityUUID:
                peripheral.readValue(for: characteristic)
            case Self.notifyUUID:
                peripheral.setNotifyValue(true, for: characteristic)
            case Self.controlUUID:
                controlCharacteristic = characteristic
            default:
                break
            }
        }
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard characteristic.uuid == Self.notifyUUID,
              error == nil,
              characteristic.isNotifying else {
            evidenceHandler("notify_enabled", false)
            return
        }
        notifyEnabled = true
        evidenceHandler("notify_enabled", true)
        sendConnectionIDIfReady(peripheral)
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard error == nil, let value = characteristic.value else {
            evidenceHandler("protected_characteristic_access", false)
            return
        }
        if characteristic.uuid == Self.identityUUID {
            observedDeviceID = value
            evidenceHandler(
                "gatt_identity_matches",
                value == expectedDeviceID
            )
            sendConnectionIDIfReady(peripheral)
            return
        }
        guard characteristic.uuid == Self.notifyUUID,
              observedDeviceID == expectedDeviceID else {
            evidenceHandler("notify_identity_bound", false)
            return
        }
        notifyCallbackCount += 1
        let receiver = self.receiver!
        let context = self.context!
        Task {
            do {
                _ = try await receiver.receive(value, context: context)
                self.evidenceHandler("authenticated_notify_received", true)
            } catch {
                self.evidenceHandler("authenticated_notify_received", false)
            }
        }
    }

    private func sendConnectionIDIfReady(_ peripheral: CBPeripheral) {
        guard notifyEnabled,
              observedDeviceID == expectedDeviceID,
              let controlCharacteristic else {
            return
        }
        var control = Data([1])
        control.append(context.connectionID)
        peripheral.writeValue(
            control,
            for: controlCharacteristic,
            type: .withResponse
        )
    }
}

public actor ProbeSinkRegistry: AuthenticatedTextSink {
    public static let shared = ProbeSinkRegistry()
    private var sink: (any AuthenticatedTextSink)?

    public func install(_ sink: any AuthenticatedTextSink) {
        self.sink = sink
    }

    public func beginAuthenticatedText(
        _ operation: AuthenticatedTextOperation
    ) async throws -> InjectionResult {
        guard let sink else {
            throw StableErrorCode.indeterminate
        }
        return try await sink.beginAuthenticatedText(operation)
    }
}
```

`CoreBluetoothProbeClient` 只有在受保护 identity read 成功、notify 已启用、base32 HID serial 解码出的 device ID 唯一匹配后才能把 `authenticated_notify_received` 写为 true。测试或 CLI 直接调用 `receiver.receive` 只能作为单元证据，不能替代 HIL 的真实 notify 计数。

- [ ] **Step 6: 消费 foundation 的唯一跨端认证帧 fixture**

Swift test 从 `protocol/phase0/fixtures/gatt-auth-v1.json` 读取固定 key、connection ID、operation ID、counter、payload、完整 frame 和 tag，重新调用 `GATTFrame.encodeAuthenticated` 并逐 byte 比较，然后解码并核对所有字段。固件侧消费同一个 JSON。任一端创建本地替代 fixture、复制预期 hex 或在测试中重写协议都视为失败。

- [ ] **Step 7: 运行 GATT 测试与真实 framework 链接检查**

Run:

```bash
swift test --package-path companion --filter ReplayWindowTests
swift test --package-path companion --filter GATTFrameReceiverTests
swift test --package-path companion --filter IdentityBindingTests
swift build --package-path companion --target Phase0GATT
```

Expected: 所有测试通过；`Phase0GATT` 成功链接 CoreBluetooth 与 IOKit；坏 MAC 后 counter 0 的正确帧仍可接受。

- [ ] **Step 8: 提交**

```bash
git add companion/Sources/Phase0GATT companion/Tests/Phase0GATTTests
git commit -m "feat: add authenticated corebluetooth probe path"
```

## Task 6: 实现 1024-byte 原生 Unicode 注入、AX 焦点守卫与明确部分完成

**Files:**
- Create: `companion/Sources/Phase0Unicode/UTF8Chunker.swift`
- Create: `companion/Sources/Phase0Unicode/AXFocusGuard.swift`
- Create: `companion/Sources/Phase0Unicode/CGUnicodePoster.swift`
- Create: `companion/Sources/Phase0Unicode/UnicodeInjectionEngine.swift`
- Create: `companion/Tests/Phase0UnicodeTests/UTF8ChunkerTests.swift`
- Create: `companion/Tests/Phase0UnicodeTests/UnicodeInjectionEngineTests.swift`
- Create: `scripts/check_macos_injection_policy.py`

**Interfaces:**
- Consumes:
  - `UnicodeInjectionRequest(key:text:)`，其中 `text.utf8.count <= 1024`；
  - Task 2 的 `TextOperationLedger`；
  - Task 5 的 `AuthenticatedTextOperation`，由 executable 中的 adapter 转成 request。
- Produces:
  - `UTF8Chunker.split(_ text: String, maximumUTF16Units: Int = 20) throws -> [UnicodeChunk]`
  - `AXFocusProviding.capture() throws -> FocusSnapshot`
  - `AXFocusProviding.isStillFocused(_:) -> Bool`
  - `AXFocusProviding.readValue(_:) -> String?`
  - `UnicodeEventPosting.post(_ chunk: UnicodeChunk) throws`
  - `UnicodeInjectionEngine.inject(_:) throws -> InjectionResult`
- 每个 `UnicodeChunk.utf8EndOffset` 是原始文本的字节前缀位置。一个 chunk 内最多 20 个 UTF-16 code units，但不会拆开一个 Unicode scalar。

- [ ] **Step 1: 写入失败的 chunking 和注入顺序测试**

创建 `companion/Tests/Phase0UnicodeTests/UTF8ChunkerTests.swift`：

```swift
import XCTest
@testable import Phase0Unicode
import Phase0Contracts

final class UTF8ChunkerTests: XCTestCase {
    func testExactly1024BytesRoundTripsWithoutSplittingSurrogates() throws {
        let text = String(repeating: "中", count: 341) + "x"
        XCTAssertEqual(text.utf8.count, 1024)
        let chunks = try UTF8Chunker.split(text)
        XCTAssertEqual(chunks.map(\.text).joined(), text)
        XCTAssertEqual(chunks.last?.utf8EndOffset, 1024)
        XCTAssertTrue(chunks.allSatisfy { $0.utf16Units.count <= 20 })
        XCTAssertTrue(chunks.allSatisfy {
            String(decoding: $0.utf16Units, as: UTF16.self) == $0.text
        })
    }

    func test1025BytesIsRejected() throws {
        let text = String(repeating: "中", count: 341) + "xy"
        XCTAssertEqual(text.utf8.count, 1025)
        XCTAssertThrowsError(try UTF8Chunker.split(text)) {
            XCTAssertEqual($0 as? StableErrorCode, .invalidRequest)
        }
    }

    func testEmojiAndCombiningScalarsRemainValidAcrossChunks() throws {
        let text = String(repeating: "👩🏽‍💻e\u{301}", count: 30)
        let chunks = try UTF8Chunker.split(text, maximumUTF16Units: 7)
        XCTAssertEqual(chunks.map(\.text).joined(), text)
        XCTAssertTrue(chunks.allSatisfy {
            String(data: Data($0.text.utf8), encoding: .utf8) != nil
        })
    }
}
```

创建 `companion/Tests/Phase0UnicodeTests/UnicodeInjectionEngineTests.swift`：

```swift
import CryptoKit
import Foundation
import XCTest
@testable import Phase0Unicode
import Phase0Contracts
import Phase0Ledger

private final class MemoryLedger: TextOperationLedger, @unchecked Sendable {
    var records: [OperationKey: TextOperationRecord] = [:]
    var calls: [String] = []

    func begin(_ intent: TextOperationIntent, now: Date) throws -> BeginDisposition {
        calls.append("intent")
        if let record = records[intent.key] {
            guard record.payloadSHA256 == intent.payloadSHA256 else {
                throw StableErrorCode.invalidRequest
            }
            return .existing(record.status)
        }
        records[intent.key] = try TextOperationRecord(
            pairedDeviceID: intent.key.pairedDeviceID,
            operationID: intent.key.operationID,
            payloadSHA256: intent.payloadSHA256,
            targetPID: intent.targetPID,
            targetElementFingerprint: intent.targetElementFingerprint,
            totalUTF8Length: intent.totalUTF8Length,
            postedPrefixLength: 0,
            verifiedPrefixLength: 0,
            status: .intent,
            errorCode: nil,
            createdAt: now,
            updatedAt: now,
            expiresAt: now.addingTimeInterval(600)
        )
        return .created
    }

    func markAccepted(key: OperationKey, now: Date) throws {
        calls.append("accepted")
        try replace(key: key, status: .accepted, error: nil, posted: nil, verified: nil, now: now)
    }

    func markPosted(key: OperationKey, utf8PrefixLength: Int, now: Date) throws {
        calls.append("posted:\(utf8PrefixLength)")
        try replace(key: key, status: nil, error: nil, posted: utf8PrefixLength, verified: nil, now: now)
    }

    func markVerified(key: OperationKey, utf8PrefixLength: Int, now: Date) throws {
        calls.append("verified:\(utf8PrefixLength)")
        try replace(key: key, status: nil, error: nil, posted: nil, verified: utf8PrefixLength, now: now)
    }

    func finish(
        key: OperationKey,
        status: OperationStatus,
        errorCode: StableErrorCode?,
        now: Date
    ) throws {
        calls.append("finish:\(status.rawValue)")
        try replace(key: key, status: status, error: errorCode, posted: nil, verified: nil, now: now)
    }

    func recoverInterrupted(now: Date) throws -> [OperationKey] {
        []
    }

    func fetch(pairedDeviceID: String, operationID: UUID) throws -> TextOperationRecord? {
        records[OperationKey(pairedDeviceID: pairedDeviceID, operationID: operationID)]
    }

    private func replace(
        key: OperationKey,
        status: OperationStatus?,
        error: StableErrorCode?,
        posted: Int?,
        verified: Int?,
        now: Date
    ) throws {
        let old = try XCTUnwrap(records[key])
        records[key] = try TextOperationRecord(
            pairedDeviceID: old.pairedDeviceID,
            operationID: old.operationID,
            payloadSHA256: old.payloadSHA256,
            targetPID: old.targetPID,
            targetElementFingerprint: old.targetElementFingerprint,
            totalUTF8Length: old.totalUTF8Length,
            postedPrefixLength: posted ?? old.postedPrefixLength,
            verifiedPrefixLength: verified ?? old.verifiedPrefixLength,
            status: status ?? old.status,
            errorCode: error,
            createdAt: old.createdAt,
            updatedAt: now,
            expiresAt: old.expiresAt
        )
    }
}

private final class FakeFocus: AXFocusProviding, @unchecked Sendable {
    var trusted = true
    var secure = false
    var checks = 0
    var changeAfterChecks: Int?
    var value = ""

    func accessibilityTrusted() -> Bool { trusted }
    func secureInputEnabled() -> Bool { secure }

    func capture() throws -> FocusSnapshot {
        FocusSnapshot(
            pid: 123,
            elementFingerprint: "sha256:field",
            opaqueElement: NSObject()
        )
    }

    func isStillFocused(_ snapshot: FocusSnapshot) -> Bool {
        checks += 1
        return changeAfterChecks.map { checks <= $0 } ?? true
    }

    func readValue(_ snapshot: FocusSnapshot) -> String? {
        value
    }
}

private final class FakePoster: UnicodeEventPosting, @unchecked Sendable {
    let focus: FakeFocus
    var posted: [UnicodeChunk] = []

    init(focus: FakeFocus) {
        self.focus = focus
    }

    func post(_ chunk: UnicodeChunk) throws {
        posted.append(chunk)
        focus.value += chunk.text
    }
}

@MainActor
final class UnicodeInjectionEngineTests: XCTestCase {
    private func request(_ text: String) -> UnicodeInjectionRequest {
        UnicodeInjectionRequest(
            key: OperationKey(
                pairedDeviceID: "device-a",
                operationID: UUID()
            ),
            text: text
        )
    }

    func testIntentCommitsBeforeFirstNativePostAnd1024BytesComplete() throws {
        let ledger = MemoryLedger()
        let focus = FakeFocus()
        let poster = FakePoster(focus: focus)
        let engine = UnicodeInjectionEngine(
            ledger: ledger,
            focus: focus,
            poster: poster,
            now: { Date(timeIntervalSince1970: 10) }
        )
        let text = String(repeating: "中", count: 341) + "x"
        let result = try engine.inject(request(text))

        XCTAssertEqual(ledger.calls.first, "intent")
        XCTAssertEqual(result.status, .completed)
        XCTAssertEqual(result.postedPrefixLength, 1024)
        XCTAssertEqual(result.verifiedPrefixLength, 1024)
        XCTAssertEqual(focus.value, text)
    }

    func testFocusChangeStopsWithoutSendingRemainder() throws {
        let ledger = MemoryLedger()
        let focus = FakeFocus()
        focus.changeAfterChecks = 1
        let poster = FakePoster(focus: focus)
        let engine = UnicodeInjectionEngine(
            ledger: ledger,
            focus: focus,
            poster: poster,
            now: { Date(timeIntervalSince1970: 10) }
        )
        let result = try engine.inject(
            request(String(repeating: "中文", count: 40))
        )

        XCTAssertEqual(result.status, .partial)
        XCTAssertEqual(result.errorCode, .focusChanged)
        XCTAssertEqual(poster.posted.count, 1)
        XCTAssertGreaterThan(result.postedPrefixLength, 0)
        XCTAssertLessThan(result.postedPrefixLength, 240)
    }

    func testSecureInputAndMissingAccessibilityPostNothing() throws {
        let ledger = MemoryLedger()
        let focus = FakeFocus()
        focus.secure = true
        let poster = FakePoster(focus: focus)
        let engine = UnicodeInjectionEngine(
            ledger: ledger,
            focus: focus,
            poster: poster,
            now: { Date(timeIntervalSince1970: 10) }
        )
        var result = try engine.inject(request("中文"))
        XCTAssertEqual(result.status, .failed)
        XCTAssertEqual(result.errorCode, .secureInputActive)
        XCTAssertTrue(poster.posted.isEmpty)

        focus.secure = false
        focus.trusted = false
        result = try engine.inject(request("中文"))
        XCTAssertEqual(result.errorCode, .permissionDenied)
        XCTAssertTrue(poster.posted.isEmpty)
    }
}
```

- [ ] **Step 2: 运行测试确认 chunker、focus 和 engine 尚不存在**

Run:

```bash
swift test --package-path companion --filter UTF8ChunkerTests
swift test --package-path companion --filter UnicodeInjectionEngineTests
```

Expected: FAIL，错误包含 `cannot find 'UTF8Chunker' in scope` 和 `cannot find type 'AXFocusProviding'`。

- [ ] **Step 3: 实现按 scalar 分块与原生 CGEvent poster**

创建 `companion/Sources/Phase0Unicode/UTF8Chunker.swift`：

```swift
import Foundation
import Phase0Contracts

public struct UnicodeChunk: Equatable, Sendable {
    public let text: String
    public let utf16Units: [UInt16]
    public let utf8StartOffset: Int
    public let utf8EndOffset: Int
}

public enum UTF8Chunker {
    public static func split(
        _ text: String,
        maximumUTF16Units: Int = 20
    ) throws -> [UnicodeChunk] {
        guard text.utf8.count <= 1024,
              maximumUTF16Units > 0 else {
            throw StableErrorCode.invalidRequest
        }
        if text.isEmpty {
            return []
        }
        var chunks: [UnicodeChunk] = []
        var current = ""
        var currentUTF16Count = 0
        var start = 0

        func appendCurrent() {
            guard !current.isEmpty else {
                return
            }
            let end = start + current.utf8.count
            chunks.append(
                UnicodeChunk(
                    text: current,
                    utf16Units: Array(current.utf16),
                    utf8StartOffset: start,
                    utf8EndOffset: end
                )
            )
            start = end
            current = ""
            currentUTF16Count = 0
        }

        for scalar in text.unicodeScalars {
            let scalarText = String(scalar)
            let units = scalarText.utf16.count
            guard units <= maximumUTF16Units else {
                throw StableErrorCode.invalidRequest
            }
            if currentUTF16Count + units > maximumUTF16Units {
                appendCurrent()
            }
            current.append(scalarText)
            currentUTF16Count += units
        }
        appendCurrent()
        guard chunks.map(\.text).joined() == text,
              chunks.last?.utf8EndOffset == text.utf8.count else {
            throw StableErrorCode.indeterminate
        }
        return chunks
    }
}
```

创建 `companion/Sources/Phase0Unicode/CGUnicodePoster.swift`：

```swift
import ApplicationServices
import Foundation
import Phase0Contracts

public protocol UnicodeEventPosting: Sendable {
    func post(_ chunk: UnicodeChunk) throws
}

public struct CGUnicodePoster: UnicodeEventPosting {
    public init() {}

    public func post(_ chunk: UnicodeChunk) throws {
        guard !chunk.utf16Units.isEmpty,
              chunk.utf16Units.count <= 20,
              let source = CGEventSource(stateID: .hidSystemState),
              let keyDown = CGEvent(
                  keyboardEventSource: source,
                  virtualKey: 0,
                  keyDown: true
              ),
              let keyUp = CGEvent(
                  keyboardEventSource: source,
                  virtualKey: 0,
                  keyDown: false
              ) else {
            throw StableErrorCode.indeterminate
        }
        var units = chunk.utf16Units
        units.withUnsafeMutableBufferPointer { buffer in
            keyDown.keyboardSetUnicodeString(
                stringLength: buffer.count,
                unicodeString: buffer.baseAddress
            )
        }
        keyDown.post(tap: .cghidEventTap)
        keyUp.post(tap: .cghidEventTap)
    }
}
```

- [ ] **Step 4: 实现真实 AX focus token、Secure Input 检测和可读值**

创建 `companion/Sources/Phase0Unicode/AXFocusGuard.swift`：

```swift
import AppKit
import ApplicationServices
import Carbon.HIToolbox
import CryptoKit
import Foundation
import Phase0Contracts

public struct FocusSnapshot: @unchecked Sendable {
    public let pid: pid_t
    public let elementFingerprint: String
    public let opaqueElement: AnyObject

    public init(
        pid: pid_t,
        elementFingerprint: String,
        opaqueElement: AnyObject
    ) {
        self.pid = pid
        self.elementFingerprint = elementFingerprint
        self.opaqueElement = opaqueElement
    }
}

public protocol AXFocusProviding: Sendable {
    func accessibilityTrusted() -> Bool
    func secureInputEnabled() -> Bool
    func capture() throws -> FocusSnapshot
    func isStillFocused(_ snapshot: FocusSnapshot) -> Bool
    func readValue(_ snapshot: FocusSnapshot) -> String?
}

public struct AXFocusGuard: AXFocusProviding {
    public init() {}

    public func accessibilityTrusted() -> Bool {
        AXIsProcessTrusted()
    }

    public func secureInputEnabled() -> Bool {
        IsSecureEventInputEnabled()
    }

    public func capture() throws -> FocusSnapshot {
        guard accessibilityTrusted() else {
            throw StableErrorCode.permissionDenied
        }
        guard !secureInputEnabled() else {
            throw StableErrorCode.secureInputActive
        }
        guard let frontmost = NSWorkspace.shared.frontmostApplication else {
            throw StableErrorCode.indeterminate
        }
        let system = AXUIElementCreateSystemWide()
        var focusedApplication: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            system,
            kAXFocusedApplicationAttribute as CFString,
            &focusedApplication
        ) == .success,
              let app = focusedApplication as! AXUIElement? else {
            throw StableErrorCode.permissionDenied
        }
        var pid: pid_t = 0
        guard AXUIElementGetPid(app, &pid) == .success,
              pid == frontmost.processIdentifier else {
            throw StableErrorCode.focusChanged
        }
        var focusedElement: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            app,
            kAXFocusedUIElementAttribute as CFString,
            &focusedElement
        ) == .success,
              let element = focusedElement as! AXUIElement? else {
            throw StableErrorCode.focusChanged
        }
        return FocusSnapshot(
            pid: pid,
            elementFingerprint: fingerprint(pid: pid, element: element),
            opaqueElement: element
        )
    }

    public func isStillFocused(_ snapshot: FocusSnapshot) -> Bool {
        guard !secureInputEnabled(),
              let current = try? capture(),
              current.pid == snapshot.pid,
              let expected = snapshot.opaqueElement as? AXUIElement,
              let observed = current.opaqueElement as? AXUIElement else {
            return false
        }
        return CFEqual(expected, observed)
    }

    public func readValue(_ snapshot: FocusSnapshot) -> String? {
        guard let element = snapshot.opaqueElement as? AXUIElement else {
            return nil
        }
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            element,
            kAXValueAttribute as CFString,
            &value
        ) == .success else {
            return nil
        }
        if let string = value as? String {
            return string
        }
        if let attributed = value as? NSAttributedString {
            return attributed.string
        }
        return nil
    }

    private func fingerprint(pid: pid_t, element: AXUIElement) -> String {
        let attributes = [
            kAXRoleAttribute,
            kAXSubroleAttribute,
            kAXIdentifierAttribute
        ].map { name -> String in
            var value: CFTypeRef?
            guard AXUIElementCopyAttributeValue(
                element,
                name as CFString,
                &value
            ) == .success else {
                return "-"
            }
            return String(describing: value)
        }
        let material = Data(
            "\(pid)|\(attributes.joined(separator: "|"))".utf8
        )
        return "sha256:" + SHA256.hash(data: material)
            .map { String(format: "%02x", $0) }
            .joined()
    }
}
```

- [ ] **Step 5: 实现 ledger-first 注入状态机**

创建 `companion/Sources/Phase0Unicode/UnicodeInjectionEngine.swift`：

```swift
import CryptoKit
import Foundation
import Phase0Contracts
import Phase0Ledger

public struct UnicodeInjectionRequest: Sendable {
    public let key: OperationKey
    public let text: String

    public init(key: OperationKey, text: String) {
        self.key = key
        self.text = text
    }
}

@MainActor
public final class UnicodeInjectionEngine {
    private let ledger: TextOperationLedger
    private let focus: AXFocusProviding
    private let poster: UnicodeEventPosting
    private let now: @Sendable () -> Date
    private let afterPosted: @Sendable (Int) -> Void

    public init(
        ledger: TextOperationLedger,
        focus: AXFocusProviding,
        poster: UnicodeEventPosting,
        now: @escaping @Sendable () -> Date = Date.init,
        afterPosted: @escaping @Sendable (Int) -> Void = { _ in }
    ) {
        self.ledger = ledger
        self.focus = focus
        self.poster = poster
        self.now = now
        self.afterPosted = afterPosted
    }

    public func inject(_ request: UnicodeInjectionRequest) throws -> InjectionResult {
        let chunks = try UTF8Chunker.split(request.text)
        guard focus.accessibilityTrusted() else {
            return failure(request.key.operationID, .permissionDenied)
        }
        guard !focus.secureInputEnabled() else {
            return failure(request.key.operationID, .secureInputActive)
        }
        let snapshot: FocusSnapshot
        do {
            snapshot = try focus.capture()
        } catch let code as StableErrorCode {
            return failure(request.key.operationID, code)
        }
        let initialValue = focus.readValue(snapshot)
        let payload = Data(request.text.utf8)
        let intent = TextOperationIntent(
            key: request.key,
            payloadSHA256: Data(SHA256.hash(data: payload)),
            targetPID: snapshot.pid,
            targetElementFingerprint: snapshot.elementFingerprint,
            totalUTF8Length: payload.count
        )
        let disposition = try ledger.begin(intent, now: now())
        if case let .existing(status) = disposition {
            guard let existing = try ledger.fetch(
                pairedDeviceID: request.key.pairedDeviceID,
                operationID: request.key.operationID
            ) else {
                throw StableErrorCode.indeterminate
            }
            return InjectionResult(
                operationID: request.key.operationID,
                status: status.isTerminal ? status : .indeterminate,
                postedPrefixLength: existing.postedPrefixLength,
                verifiedPrefixLength: existing.verifiedPrefixLength,
                errorCode: status.isTerminal ? existing.errorCode : .indeterminate
            )
        }
        try ledger.markAccepted(key: request.key, now: now())

        var posted = 0
        var verified = 0
        var prefix = ""
        for chunk in chunks {
            if focus.secureInputEnabled() {
                return try stop(
                    request.key,
                    posted: posted,
                    verified: verified,
                    code: .secureInputActive
                )
            }
            guard focus.isStillFocused(snapshot) else {
                return try stop(
                    request.key,
                    posted: posted,
                    verified: verified,
                    code: .focusChanged
                )
            }
            do {
                try poster.post(chunk)
            } catch {
                return try stop(
                    request.key,
                    posted: posted,
                    verified: verified,
                    code: .indeterminate
                )
            }
            posted = chunk.utf8EndOffset
            try ledger.markPosted(
                key: request.key,
                utf8PrefixLength: posted,
                now: now()
            )
            afterPosted(posted)
            prefix += chunk.text
            if let initialValue,
               focus.readValue(snapshot) == initialValue + prefix {
                verified = posted
                try ledger.markVerified(
                    key: request.key,
                    utf8PrefixLength: verified,
                    now: now()
                )
            }
        }
        try ledger.finish(
            key: request.key,
            status: .completed,
            errorCode: nil,
            now: now()
        )
        return InjectionResult(
            operationID: request.key.operationID,
            status: .completed,
            postedPrefixLength: posted,
            verifiedPrefixLength: verified,
            errorCode: nil
        )
    }

    private func stop(
        _ key: OperationKey,
        posted: Int,
        verified: Int,
        code: StableErrorCode
    ) throws -> InjectionResult {
        let status: OperationStatus = posted == 0 ? .failed : .partial
        try ledger.finish(
            key: key,
            status: status,
            errorCode: code,
            now: now()
        )
        return InjectionResult(
            operationID: key.operationID,
            status: status,
            postedPrefixLength: posted,
            verifiedPrefixLength: verified,
            errorCode: code
        )
    }

    private func failure(
        _ operationID: UUID,
        _ code: StableErrorCode
    ) -> InjectionResult {
        InjectionResult(
            operationID: operationID,
            status: .failed,
            postedPrefixLength: 0,
            verifiedPrefixLength: 0,
            errorCode: code
        )
    }
}
```

源代码中 `ledger.begin` 必须位于第一处 `poster.post` 之前。若 `afterPosted` 在 HIL crash 模式调用 `_exit(86)`，已提交的 prefix 保留在 WAL；重启后 Task 2 的 `recoverInterrupted` 将其转换为 `indeterminate`。

- [ ] **Step 6: 添加禁止 clipboard/Command-V 的可执行源码门禁**

创建 `scripts/check_macos_injection_policy.py`：

```python
#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "companion" / "Sources" / "Phase0Unicode"
FORBIDDEN = {
    r"\bNSPasteboard\b": "clipboard API",
    r"\bCGEventFlags\.maskCommand\b": "synthetic Command modifier",
    r"\bkVK_ANSI_V\b": "synthetic V key",
    r"(?i)command-v|cmd-v": "paste shortcut text",
}

if not SOURCE.is_dir():
    print(f"missing source directory: {SOURCE}", file=sys.stderr)
    raise SystemExit(2)

violations: list[str] = []
for path in sorted(SOURCE.glob("*.swift")):
    text = path.read_text(encoding="utf-8")
    for pattern, label in FORBIDDEN.items():
        if re.search(pattern, text):
            violations.append(f"{path.relative_to(ROOT)}: {label}")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("native-unicode-policy: pass")
```

- [ ] **Step 7: 运行 Unicode 测试和源码门禁**

Run:

```bash
swift test --package-path companion --filter UTF8ChunkerTests
swift test --package-path companion --filter UnicodeInjectionEngineTests
python3 scripts/check_macos_injection_policy.py
```

Expected: Swift 测试全部通过；Python 最后一行严格为 `native-unicode-policy: pass`；1025-byte 输入、焦点切换、Secure Input 和无 Accessibility 均不会继续投递。

- [ ] **Step 8: 提交**

```bash
git add companion/Sources/Phase0Unicode companion/Tests/Phase0UnicodeTests scripts/check_macos_injection_policy.py
git commit -m "feat: add focus-bound native unicode probe"
```

## Task 7: 构建真实签名 `.app`、注入 CLI 与 TCC A/B 升级探针

**Files:**
- Modify: `companion/Sources/cardputer-phase0-probe/main.swift`
- Create: `companion/Sources/cardputer-phase0-probe/PairGATTHILCommand.swift`
- Create: `companion/Sources/cardputer-phase0-probe/ConcurrencyHILAgent.swift`
- Create: `companion/Tests/Phase0SecurityTests/ConcurrencyHILAgentTests.swift`
- Create: `companion/AppBundle/Info.plist`
- Create: `companion/AppBundle/CardputerPhase0Probe.entitlements`
- Create: `scripts/build_signed_macos_probe.sh`
- Create: `scripts/test_build_signed_macos_probe.py`

**Interfaces:**
- Consumes:
  - 环境变量 `CARDPUTER_PHASE0_SIGN_IDENTITY`，值必须精确匹配 `security find-identity -p codesigning` 中的真实 Apple Development 或 Developer ID Application 身份；
  - 子命令 `permission-status`、`inject`、`recover-ledger`、`pair-gatt-hil`、`concurrency-hil-agent`；
  - UTF-8 request 文件和 GATT secret 文件均要求 POSIX mode `0600`，读取后立即删除。
- Produces:
  - `build/phase0/macos/a/Cardputer Phase0 Probe.app`，版本 `0.1.0`；
  - `build/phase0/macos/b/Cardputer Phase0 Probe.app`，版本 `0.1.1`；
  - 两个 bundle 的 identifier 均为 `lc.iam.cardputer.phase0probe`，designated requirement 必须逐字相同；
  - `inject` JSON 结果只包含 hash、PID、AX fingerprint、字节计数、状态和稳定错误码；
  - `recover-ledger` 把遗留非终态记录写为 `indeterminate`；
  - `pair-gatt-hil` 运行 Task 3–5 的真实 Network/CoreBluetooth/IOHID 路径，并记录/核对当前运行固件的完整 image SHA-256 与 app ELF SHA-256；
  - `concurrency-hil-agent` 在固定时长内保持同一 WSS、CoreBluetooth GATT 与 HID 身份会话，以 stdout JSONL 输出可由 firmware HIL runner 加盖接收时间的事件；不得输出正文、SAS、密钥或完整 device ID。

- [ ] **Step 1: 写入失败的签名构建测试**

创建 `scripts/test_build_signed_macos_probe.py`：

```python
#!/usr/bin/env python3
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "build_signed_macos_probe.sh"


class SignedBundleTests(unittest.TestCase):
    def test_missing_identity_fails_without_adhoc_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            env = os.environ.copy()
            env.pop("CARDPUTER_PHASE0_SIGN_IDENTITY", None)
            result = subprocess.run(
                [str(SCRIPT), "a", directory],
                cwd=ROOT,
                env=env,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("CARDPUTER_PHASE0_SIGN_IDENTITY is required", result.stderr)
            self.assertFalse(any(Path(directory).rglob("*.app")))

    @unittest.skipUnless(
        os.environ.get("CARDPUTER_PHASE0_SIGN_IDENTITY"),
        "real signing identity is required",
    )
    def test_a_and_b_are_real_signed_and_keep_designated_requirement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for variant in ("a", "b"):
                output = Path(directory) / variant
                subprocess.run(
                    [str(SCRIPT), variant, str(output)],
                    cwd=ROOT,
                    check=True,
                )
                paths.append(output / "Cardputer Phase0 Probe.app")
            requirements = []
            versions = []
            for app in paths:
                subprocess.run(
                    ["codesign", "--verify", "--deep", "--strict", str(app)],
                    check=True,
                )
                detail = subprocess.run(
                    ["codesign", "-d", "-r-", str(app)],
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertNotIn("adhoc", detail.stderr.lower())
                requirements.append(
                    next(
                        line.removeprefix("designated => ")
                        for line in detail.stderr.splitlines()
                        if line.startswith("designated => ")
                    )
                )
                with (app / "Contents" / "Info.plist").open("rb") as handle:
                    versions.append(
                        plistlib.load(handle)["CFBundleShortVersionString"]
                    )
            self.assertEqual(requirements[0], requirements[1])
            self.assertEqual(versions, ["0.1.0", "0.1.1"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: 运行测试确认签名脚本尚不存在**

Run:

```bash
python3 scripts/test_build_signed_macos_probe.py
```

Expected: ERROR，错误包含 `build_signed_macos_probe.sh` 和 `No such file or directory`。

- [ ] **Step 3: 写入 `.app` metadata 与 entitlements**

创建 `companion/AppBundle/Info.plist`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>cardputer-phase0-probe</string>
  <key>CFBundleIdentifier</key>
  <string>lc.iam.cardputer.phase0probe</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>Cardputer Phase0 Probe</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>__VERSION__</string>
  <key>CFBundleVersion</key>
  <string>__BUILD__</string>
  <key>LSMinimumSystemVersion</key>
  <string>14.0</string>
  <key>LSUIElement</key>
  <true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>Validate the paired Cardputer GATT path during Phase 0.</string>
</dict>
</plist>
```

创建 `companion/AppBundle/CardputerPhase0Probe.entitlements`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.network.client</key>
  <true/>
  <key>com.apple.security.network.server</key>
  <true/>
  <key>com.apple.security.device.bluetooth</key>
  <true/>
</dict>
</plist>
```

- [ ] **Step 4: 实现严格拒绝 adhoc 的 A/B 构建脚本**

创建 `scripts/build_signed_macos_probe.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail

variant="${1:-}"
output_root="${2:-}"
if [[ "$variant" != "a" && "$variant" != "b" ]]; then
  echo "usage: build_signed_macos_probe.sh a|b OUTPUT_DIR" >&2
  exit 64
fi
if [[ -z "$output_root" ]]; then
  echo "usage: build_signed_macos_probe.sh a|b OUTPUT_DIR" >&2
  exit 64
fi
if [[ -z "${CARDPUTER_PHASE0_SIGN_IDENTITY:-}" ]]; then
  echo "CARDPUTER_PHASE0_SIGN_IDENTITY is required; adhoc signing is forbidden" >&2
  exit 2
fi
if ! security find-identity -v -p codesigning |
  grep -F "\"${CARDPUTER_PHASE0_SIGN_IDENTITY}\"" >/dev/null; then
  echo "configured signing identity was not found" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
case "$variant" in
  a) version="0.1.0"; build="100" ;;
  b) version="0.1.1"; build="101" ;;
esac
swift build --package-path "$repo_root/companion" -c release \
  --product cardputer-phase0-probe

app="$output_root/Cardputer Phase0 Probe.app"
mkdir -p "$app/Contents/MacOS"
install -m 0755 \
  "$repo_root/companion/.build/release/cardputer-phase0-probe" \
  "$app/Contents/MacOS/cardputer-phase0-probe"
sed \
  -e "s/__VERSION__/$version/g" \
  -e "s/__BUILD__/$build/g" \
  "$repo_root/companion/AppBundle/Info.plist" \
  >"$app/Contents/Info.plist"
plutil -lint "$app/Contents/Info.plist" >/dev/null
codesign --force --options runtime --timestamp=none \
  --entitlements "$repo_root/companion/AppBundle/CardputerPhase0Probe.entitlements" \
  --sign "$CARDPUTER_PHASE0_SIGN_IDENTITY" \
  "$app"
codesign --verify --deep --strict "$app"
signature_detail="$(codesign -dvv "$app" 2>&1)"
if grep -qi "adhoc" <<<"$signature_detail"; then
  echo "adhoc signature is not valid Phase 0 evidence" >&2
  exit 3
fi
echo "$app"
```

- [ ] **Step 5: 将 executable 接到 ledger、AX、CGEvent 和真机命令**

将 `companion/Sources/cardputer-phase0-probe/main.swift` 改为 `@main` 命令入口。参数解析必须拒绝未知参数、输出文件必须原子写入、JSON encoder 使用 ISO-8601 日期和 sorted keys：

```swift
import AppKit
import CryptoKit
import Foundation
import Phase0Contracts
import Phase0GATT
import Phase0Ledger
import Phase0Security
import Phase0Unicode

private struct InjectRequest: Decodable {
    let pairedDeviceID: String
    let operationID: UUID
    let text: String
    let crashAfterPostedPrefix: Int?
    let chunkDelayMilliseconds: Int
}

private struct PermissionResult: Encodable {
    let accessibilityTrusted: Bool
    let secureInputActive: Bool
}

private struct ProbeInjectionEvidence: Encodable {
    let operationID: UUID
    let status: OperationStatus
    let postedPrefixLength: Int
    let verifiedPrefixLength: Int
    let errorCode: StableErrorCode?
    let targetPID: pid_t
    let targetElementFingerprint: String
}

enum AtomicJSON {
    static func write<T: Encodable>(_ value: T, to path: String) throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        let data = try encoder.encode(value)
        let url = URL(fileURLWithPath: path)
        let temporary = url.appendingPathExtension("new")
        try data.write(to: temporary, options: .atomic)
        if FileManager.default.fileExists(atPath: url.path) {
            _ = try FileManager.default.replaceItemAt(
                url,
                withItemAt: temporary
            )
        } else {
            try FileManager.default.moveItem(at: temporary, to: url)
        }
    }
}

@main
@MainActor
struct CardputerPhase0Probe {
    static func main() async {
        NSApplication.shared.setActivationPolicy(.accessory)
        do {
            try await run(Array(CommandLine.arguments.dropFirst()))
        } catch {
            FileHandle.standardError.write(
                Data("probe failed: \(error)\n".utf8)
            )
            exit(1)
        }
    }

    private static func run(_ arguments: [String]) async throws {
        guard let command = arguments.first else {
            throw StableErrorCode.invalidRequest
        }
        switch command {
        case "--version":
            print("cardputer-phase0-probe 0.1.0")
        case "permission-status":
            let options = parsePairs(Array(arguments.dropFirst()))
            let output = try required("--output", in: options)
            try AtomicJSON.write(
                PermissionResult(
                    accessibilityTrusted: AXIsProcessTrusted(),
                    secureInputActive: AXFocusGuard().secureInputEnabled()
                ),
                to: output
            )
        case "inject":
            try inject(parsePairs(Array(arguments.dropFirst())))
        case "recover-ledger":
            try recover(parsePairs(Array(arguments.dropFirst())))
        case "pair-gatt-hil":
            try await PairGATTHILCommand.run(
                options: parsePairs(Array(arguments.dropFirst()))
            )
        default:
            throw StableErrorCode.invalidRequest
        }
    }

    private static func inject(_ options: [String: String]) throws {
        let requestPath = try required("--request", in: options)
        let outputPath = try required("--output", in: options)
        let ledgerPath = try required("--ledger", in: options)
        let requestURL = URL(fileURLWithPath: requestPath)
        defer { try? FileManager.default.removeItem(at: requestURL) }
        let attributes = try FileManager.default.attributesOfItem(atPath: requestPath)
        guard (attributes[.posixPermissions] as? NSNumber)?.intValue == 0o600 else {
            throw StableErrorCode.permissionDenied
        }
        let request = try JSONDecoder().decode(
            InjectRequest.self,
            from: Data(contentsOf: requestURL)
        )
        let ledger = try SQLiteTextOperationLedger(path: ledgerPath)
        let engine = UnicodeInjectionEngine(
            ledger: ledger,
            focus: AXFocusGuard(),
            poster: CGUnicodePoster(),
            afterPosted: { prefix in
                if request.chunkDelayMilliseconds > 0 {
                    usleep(useconds_t(request.chunkDelayMilliseconds * 1_000))
                }
                if let crash = request.crashAfterPostedPrefix,
                   prefix >= crash {
                    _exit(86)
                }
            }
        )
        let result = try engine.inject(
            UnicodeInjectionRequest(
                key: OperationKey(
                    pairedDeviceID: request.pairedDeviceID,
                    operationID: request.operationID
                ),
                text: request.text
            )
        )
        let record = try ledger.fetch(
            pairedDeviceID: request.pairedDeviceID,
            operationID: request.operationID
        )
        try AtomicJSON.write(
            ProbeInjectionEvidence(
                operationID: result.operationID,
                status: result.status,
                postedPrefixLength: result.postedPrefixLength,
                verifiedPrefixLength: result.verifiedPrefixLength,
                errorCode: result.errorCode,
                targetPID: record?.targetPID ?? 0,
                targetElementFingerprint: record?.targetElementFingerprint
                    ?? "sha256:" + String(repeating: "0", count: 64)
            ),
            to: outputPath
        )
    }

    private static func recover(_ options: [String: String]) throws {
        let ledgerPath = try required("--ledger", in: options)
        let outputPath = try required("--output", in: options)
        let ledger = try SQLiteTextOperationLedger(path: ledgerPath)
        let keys = try ledger.recoverInterrupted(now: Date())
        try AtomicJSON.write(
            keys.map {
                [
                    "paired_device_id": $0.pairedDeviceID,
                    "operation_id": $0.operationID.uuidString.lowercased(),
                    "status": OperationStatus.indeterminate.rawValue
                ]
            },
            to: outputPath
        )
    }

    private static func parsePairs(_ arguments: [String]) -> [String: String] {
        guard arguments.count.isMultiple(of: 2) else {
            return [:]
        }
        return stride(from: 0, to: arguments.count, by: 2).reduce(into: [:]) {
            $0[arguments[$1]] = arguments[$1 + 1]
        }
    }

    private static func required(
        _ name: String,
        in options: [String: String]
    ) throws -> String {
        guard let value = options[name], !value.isEmpty else {
            throw StableErrorCode.invalidRequest
        }
        return value
    }
}
```

创建 `companion/Sources/cardputer-phase0-probe/PairGATTHILCommand.swift`，只做真实组件编排和脱敏：

```swift
import CryptoKit
import Foundation
import Phase0Contracts
import Phase0GATT
import Phase0Security

private struct PairGATTHILResult: Encodable {
    struct Bluetooth: Encodable {
        let corebluetoothNotifyCallbacks: Int
        let authenticatedFrames: Int
        let protectedCharacteristicAccess: Bool
        let hidIdentitySHA256: String
        let gattIdentitySHA256: String
        let samePhysicalDevice: Bool
        let badMACAdvancedCounter: Bool
        let replayRejected: Bool

        enum CodingKeys: String, CodingKey {
            case corebluetoothNotifyCallbacks = "corebluetooth_notify_callbacks"
            case authenticatedFrames = "authenticated_frames"
            case protectedCharacteristicAccess = "protected_characteristic_access"
            case hidIdentitySHA256 = "hid_identity_sha256"
            case gattIdentitySHA256 = "gatt_identity_sha256"
            case samePhysicalDevice = "same_physical_device"
            case badMACAdvancedCounter = "bad_mac_advanced_counter"
            case replayRejected = "replay_rejected"
        }
    }

    struct Pairing: Encodable {
        let transcriptSHA256: String
        let sasConfirmedOnMac: Bool
        let sasConfirmedOnCardputer: Bool
        let tlsExporterLength: Int
        let clientSignatureValid: Bool
        let wssChallengeSeen: Bool
        let gattChallengeSeen: Bool
        let bindChallengeComplete: Bool

        enum CodingKeys: String, CodingKey {
            case transcriptSHA256 = "transcript_sha256"
            case sasConfirmedOnMac = "sas_confirmed_on_mac"
            case sasConfirmedOnCardputer = "sas_confirmed_on_cardputer"
            case tlsExporterLength = "tls_exporter_length"
            case clientSignatureValid = "client_signature_valid"
            case wssChallengeSeen = "wss_challenge_seen"
            case gattChallengeSeen = "gatt_challenge_seen"
            case bindChallengeComplete = "bind_challenge_complete"
        }
    }

    struct LAN: Encodable {
        let selectedInterface: String
        let listenerInterface: String
        let mdnsInterface: String
        let interfaceFingerprintUnchanged: Bool
        let remoteInSelectedSubnet: Bool
        let publicListenerDetected: Bool

        enum CodingKeys: String, CodingKey {
            case selectedInterface = "selected_interface"
            case listenerInterface = "listener_interface"
            case mdnsInterface = "mdns_interface"
            case interfaceFingerprintUnchanged =
                "interface_fingerprint_unchanged"
            case remoteInSelectedSubnet = "remote_in_selected_subnet"
            case publicListenerDetected = "public_listener_detected"
        }
    }

    let bluetooth: Bluetooth
    let pairing: Pairing
    let lan: LAN
}

enum PairGATTHILCommand {
    @MainActor
    static func run(options: [String: String]) async throws {
        let output = try required("--output", options)
        let interface = try required("--interface", options)
        let address = try required("--interface-address", options)
        let netmask = try required("--interface-netmask", options)
        let peripheralID = try UUID(
            uuidString: required("--peripheral-id", options)
        ).unwrap()
        let expectedDeviceID = try Data(
            strictHex: required("--device-id-hex", options)
        )
        let secretURL = URL(
            fileURLWithPath: try required("--gatt-secret-file", options)
        )
        let tlsIdentityLabel = try required("--tls-identity-label", options)
        guard expectedDeviceID.count == 16,
              FileManager.default.fileExists(atPath: secretURL.path),
              try secretURL.posixMode() == 0o600 else {
            throw StableErrorCode.permissionDenied
        }
        let secret = try Data(contentsOf: secretURL)
        try FileManager.default.removeItem(at: secretURL)
        guard secret.count == 32 else {
            throw StableErrorCode.invalidRequest
        }

        let snapshot = InterfaceSnapshot(
            name: interface,
            ipv4Address: address,
            ipv4Netmask: netmask
        )
        let policy = try LANInterfacePolicy.confirmed(
            name: interface,
            snapshots: [snapshot]
        )
        let bind = BindChallengeCoordinator()
        async let wss = WSSPairingProbe.run(
            config: WSSPairingConfig(
                policy: policy,
                tlsIdentityLabel: tlsIdentityLabel,
                protocolVersion: "1.0"
            ),
            bindCoordinator: bind
        )
        async let bluetooth = CoreBluetoothProbeClient.run(
            config: BluetoothProbeConfig(
                peripheralIdentifier: peripheralID,
                expectedDeviceID: expectedDeviceID,
                pairedDeviceID: "phase0-hil",
                gattAuthKey: SymmetricKey(data: secret),
                bindCoordinator: bind
            ),
            timeout: .seconds(60)
        )
        let (wssEvidence, bluetoothEvidence) = try await (wss, bluetooth)
        let hidDigest = try IdentityBinder.bind(
            hidDeviceIDs: bluetoothEvidence.hidDeviceIDs,
            gattDeviceID: bluetoothEvidence.gattDeviceID
        )
        let hidHash = hex(Data(SHA256.hash(data: hidDigest)))
        let gattHash = hex(
            Data(SHA256.hash(data: bluetoothEvidence.gattDeviceID))
        )
        let result = PairGATTHILResult(
            bluetooth: .init(
                corebluetoothNotifyCallbacks: bluetoothEvidence.notifyCallbacks,
                authenticatedFrames: bluetoothEvidence.authenticatedFrames,
                protectedCharacteristicAccess:
                    bluetoothEvidence.protectedCharacteristicAccess,
                hidIdentitySHA256: hidHash,
                gattIdentitySHA256: gattHash,
                samePhysicalDevice: hidHash == gattHash,
                badMACAdvancedCounter: bluetoothEvidence.badMACAdvancedCounter,
                replayRejected: bluetoothEvidence.replayRejected
            ),
            pairing: .init(
                transcriptSHA256: hex(wssEvidence.transcriptSHA256),
                sasConfirmedOnMac: wssEvidence.sasConfirmedOnMac,
                sasConfirmedOnCardputer: wssEvidence.sasConfirmedOnCardputer,
                tlsExporterLength: wssEvidence.tlsExporterLength,
                clientSignatureValid: wssEvidence.clientSignatureValid,
                wssChallengeSeen: wssEvidence.wssChallengeSeen,
                gattChallengeSeen: bluetoothEvidence.gattChallengeSeen,
                bindChallengeComplete:
                    wssEvidence.bindChallengeComplete
                    && bluetoothEvidence.bindChallengeComplete
            ),
            lan: .init(
                selectedInterface: interface,
                listenerInterface: wssEvidence.listenerInterface,
                mdnsInterface: wssEvidence.mdnsInterface,
                interfaceFingerprintUnchanged:
                    wssEvidence.interfaceFingerprintUnchanged,
                remoteInSelectedSubnet: wssEvidence.remoteInSelectedSubnet,
                publicListenerDetected: wssEvidence.publicListenerDetected
            )
        )
        try AtomicJSON.write(result, to: output)
    }

    private static func required(
        _ key: String,
        _ options: [String: String]
    ) throws -> String {
        guard let value = options[key], !value.isEmpty else {
            throw StableErrorCode.invalidRequest
        }
        return value
    }

    private static func hex(_ data: Data) -> String {
        data.map { String(format: "%02x", $0) }.joined()
    }
}

private extension Optional {
    func unwrap() throws -> Wrapped {
        guard let self else {
            throw StableErrorCode.invalidRequest
        }
        return self
    }
}

private extension Data {
    init(strictHex value: String) throws {
        guard value.count.isMultiple(of: 2),
              value.allSatisfy(\.isHexDigit) else {
            throw StableErrorCode.invalidRequest
        }
        var result = Data()
        var index = value.startIndex
        while index < value.endIndex {
            let end = value.index(index, offsetBy: 2)
            guard let byte = UInt8(value[index..<end], radix: 16) else {
                throw StableErrorCode.invalidRequest
            }
            result.append(byte)
            index = end
        }
        self = result
    }
}

private extension URL {
    func posixMode() throws -> Int {
        let attributes = try FileManager.default.attributesOfItem(
            atPath: path
        )
        return (attributes[.posixPermissions] as? NSNumber)?.intValue ?? -1
    }
}
```

`PairGATTHILCommand.run(options:)` 必须是同一 executable target 内的实际实现，并执行以下固定顺序：

```text
1. 读取用户确认的 BSD interface name 和确认时 address/netmask；
2. LANInterfacePolicy.revalidate；
3. 从 Keychain 读取 probe TLS SecIdentity；
4. 以 requiredInterface 启动 WSS listener 和 mDNS；
5. 完成 PairingTranscript、ECDH、6 位 SAS 双端物理确认；
6. 从真实 NWConnection TLS metadata 导出 32-byte exporter；
7. 验证 Cardputer 长期 P-256 WSS 客户端签名；
8. BindChallengeCoordinator.begin 生成一次 challenge；
9. 经 WSS 与 GATT control characteristic 发送相同 challenge；
10. 两通道响应进入 observe，只有 complete 才继续；
11. HIDIdentityReader 读取 HID serial，做严格 RFC 4648 base32 解码并取得 16-byte device ID；
12. CoreBluetoothProbeClient 读取受保护 identity 并启用 notify；
13. IdentityBinder 要求恰好一个 HID device ID 匹配 GATT raw device ID；
14. 等待至少一个 didUpdateValueFor 认证帧；
15. 写入不含 SAS、密钥、正文和完整 identity 的结果 JSON。
```

实现若缺少 Task 3–5 中任一真实 API，`pair-gatt-hil` 必须退出非零并把该项标为 `blocked`；禁止以固定 true、sleep 后成功或直接调用 receiver 替代 CoreBluetooth callback。

- [ ] **Step 6: 运行无身份失败测试与 Swift release build**

Run:

```bash
python3 scripts/test_build_signed_macos_probe.py
swift build --package-path companion -c release --product cardputer-phase0-probe
```

Expected: 第一条的无身份测试通过，真实签名测试在未配置身份时显示 1 skipped；Swift release build 成功。

设置真实身份后运行：

```bash
export CARDPUTER_PHASE0_SIGN_IDENTITY='Apple Development: exact local identity'
python3 scripts/test_build_signed_macos_probe.py
```

Expected: 两项测试都通过、0 skipped；A/B bundle 版本不同但 designated requirement 完全一致。命令中的身份字符串必须替换为 `security find-identity -v -p codesigning` 当前主机实际列出的完整名称，不得把身份写入仓库。

- [ ] **Step 7: 提交**

```bash
git add companion/AppBundle companion/Sources/cardputer-phase0-probe scripts/build_signed_macos_probe.sh scripts/test_build_signed_macos_probe.py
git commit -m "feat: package signed macos phase zero probe"
```

## Task 8: 编排五应用、TCC、Secure Input、崩溃和 BLE 真机 HIL

**Files:**
- Create: `docs/validation/phase0/macos-hil.schema.json`
- Create: `docs/validation/phase0/macos-hil-operator.md`
- Create: `docs/validation/phase0/macos-hil.md`
- Create: `scripts/run_macos_hil.py`
- Create: `scripts/test_run_macos_hil.py`

**Interfaces:**
- Consumes:
  - Task 7 的 A/B 签名 bundle；
  - foundation 生成的 `build/phase0/toolchain.json`；
  - 已安装 TextEdit、Visual Studio Code、Google Chrome、Terminal 和 iTerm2；
  - 真实 Cardputer 固件探针、当前 HID bond、CoreBluetooth peripheral UUID；
  - 用户确认的 LAN interface、address/netmask 和 Keychain 中 probe TLS identity；
  - mode `0600` 的 GATT secret input，runner 结束时删除。
- Produces:
  - `scripts/run_macos_hil.py`；
  - `docs/validation/phase0/macos-hil.schema.json`；
  - 被 Git 忽略的 `build/phase0/macos-hil/<run-id>/raw.json` 与 artifact hashes；
  - 只含事实摘要、阈值、hash 和 blocker/failure ID 的 `docs/validation/phase0/macos-hil.md`。
- `run_macos_hil.py` wraps its entire execution in `try/finally` and unlinks the mode-`0600` GATT secret input on success, blocker, test failure, exception or interrupt. The app also unlinks immediately after reading; neither layer logs the path contents.
- `overall_status=pass` 必须同时满足：
  - A/B 都是真实签名且 designated requirement 相同；
  - 升级前后 Accessibility 均为 true；
  - 五个 app 各自精确读回同一 1024-byte UTF-8 文本，`posted=verified=1024`；
  - 焦点切换返回 `partial` 且 `0 < posted < 1024`；
  - Secure Input 返回 `failed/secure_input_active` 且 `posted=0`；
  - crash exit code 为 86，重启恢复为 `indeterminate`；
  - `didUpdateValueFor` 的认证 notify 至少 1 个；
  - HID/GATT 原始 device ID 唯一相同，证据只保存各自 SHA-256；
  - 双端 SAS 已物理确认、真实 TLS exporter 客户端签名有效、WSS/GATT bind challenge 完成；
  - replay 被拒绝且坏 MAC 没有推进 counter；
  - listener 与 mDNS 位于用户选择接口，remote 位于该接口本地子网。

- [ ] **Step 1: 写入失败的 runner 单元测试**

创建 `scripts/test_run_macos_hil.py`：

```python
#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import jsonschema

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run_macos_hil.py"
SCHEMA = ROOT / "docs" / "validation" / "phase0" / "macos-hil.schema.json"


def load_runner():
    spec = importlib.util.spec_from_file_location("run_macos_hil", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RunnerTests(unittest.TestCase):
    def test_probe_text_is_exactly_1024_bytes_and_not_control_text(self) -> None:
        module = load_runner()
        text = module.make_probe_text()
        self.assertEqual(len(text.encode("utf-8")), 1024)
        self.assertNotIn("\n", text)
        self.assertNotIn("\r", text)
        self.assertNotIn("\t", text)

    def test_pass_requires_exact_five_target_set(self) -> None:
        module = load_runner()
        checks = {
            "signing": True,
            "tcc_upgrade": True,
            "focus_partial": True,
            "secure_input": True,
            "restart_indeterminate": True,
            "ble_notify": True,
            "same_identity": True,
            "pairing": True,
            "selected_lan": True,
        }
        four = [
            {"id": value, "exact_match": True, "posted": 1024, "verified": 1024}
            for value in ("textedit", "vscode", "chrome", "terminal")
        ]
        self.assertEqual(module.evaluate_gate(checks, four)[0], "fail")
        five = four + [
            {"id": "iterm2", "exact_match": True, "posted": 1024, "verified": 1024}
        ]
        self.assertEqual(module.evaluate_gate(checks, five)[0], "pass")

    def test_schema_rejects_claimed_pass_without_ble_and_tcc_evidence(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        invalid = {
            "schema_version": "1.0",
            "run_id": "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
            "started_at": "2026-07-24T00:00:00Z",
            "completed_at": "2026-07-24T00:01:00Z",
            "overall_status": "pass",
        }
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.Draft202012Validator(schema).validate(invalid)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: 运行测试确认 runner 与 schema 尚不存在**

Run:

```bash
uv run python scripts/test_run_macos_hil.py
```

Expected: ERROR，错误包含 `No such file or directory`，指向 `run_macos_hil.py` 或 `macos-hil.schema.json`。

- [ ] **Step 3: 建立拒绝缺项 pass 的 JSON Schema**

创建 `docs/validation/phase0/macos-hil.schema.json`：

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://cardputer.local/schema/phase0/macos-hil-1.0.json",
  "title": "Cardputer Phase 0 macOS HIL Evidence",
  "type": "object",
  "additionalProperties": false,
  "required": [
    "schema_version",
    "run_id",
    "git_commit",
    "toolchain_manifest_sha256",
    "started_at",
    "completed_at",
    "host",
    "signing",
    "unicode_targets",
    "negative_cases",
    "bluetooth",
    "pairing",
    "lan",
    "overall_status",
    "failures",
    "blockers"
  ],
  "properties": {
    "schema_version": {"const": "1.0"},
    "run_id": {"type": "string", "format": "uuid"},
    "git_commit": {"type": "string", "pattern": "^[0-9a-f]{40}$"},
    "toolchain_manifest_sha256": {"$ref": "#/$defs/sha256"},
    "started_at": {"type": "string", "format": "date-time"},
    "completed_at": {"type": "string", "format": "date-time"},
    "host": {
      "type": "object",
      "additionalProperties": false,
      "required": ["macos_version", "hardware", "swift_version", "probe_sha256"],
      "properties": {
        "macos_version": {"type": "string", "minLength": 1},
        "hardware": {"type": "string", "minLength": 1},
        "swift_version": {"type": "string", "minLength": 1},
        "probe_sha256": {"$ref": "#/$defs/sha256"}
      }
    },
    "signing": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "bundle_id",
        "a_valid",
        "b_valid",
        "not_adhoc",
        "designated_requirement_match",
        "accessibility_before_upgrade",
        "accessibility_after_upgrade"
      ],
      "properties": {
        "bundle_id": {"const": "lc.iam.cardputer.phase0probe"},
        "a_valid": {"type": "boolean"},
        "b_valid": {"type": "boolean"},
        "not_adhoc": {"type": "boolean"},
        "designated_requirement_match": {"type": "boolean"},
        "accessibility_before_upgrade": {"type": "boolean"},
        "accessibility_after_upgrade": {"type": "boolean"}
      }
    },
    "unicode_targets": {
      "type": "array",
      "minItems": 0,
      "maxItems": 5,
      "items": {"$ref": "#/$defs/unicode_target"}
    },
    "negative_cases": {
      "type": "object",
      "additionalProperties": false,
      "required": ["focus_switch", "secure_input", "restart"],
      "properties": {
        "focus_switch": {"$ref": "#/$defs/injection_result"},
        "secure_input": {"$ref": "#/$defs/injection_result"},
        "restart": {
          "type": "object",
          "additionalProperties": false,
          "required": ["crash_exit_code", "recovered_status", "auto_replayed"],
          "properties": {
            "crash_exit_code": {"type": "integer"},
            "recovered_status": {
              "enum": ["missing", "intent", "accepted", "indeterminate"]
            },
            "auto_replayed": {"type": "boolean"}
          }
        }
      }
    },
    "bluetooth": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "corebluetooth_notify_callbacks",
        "authenticated_frames",
        "protected_characteristic_access",
        "hid_identity_sha256",
        "gatt_identity_sha256",
        "same_physical_device",
        "bad_mac_advanced_counter",
        "replay_rejected"
      ],
      "properties": {
        "corebluetooth_notify_callbacks": {"type": "integer", "minimum": 0},
        "authenticated_frames": {"type": "integer", "minimum": 0},
        "protected_characteristic_access": {"type": "boolean"},
        "hid_identity_sha256": {"$ref": "#/$defs/sha256"},
        "gatt_identity_sha256": {"$ref": "#/$defs/sha256"},
        "same_physical_device": {"type": "boolean"},
        "bad_mac_advanced_counter": {"type": "boolean"},
        "replay_rejected": {"type": "boolean"}
      }
    },
    "pairing": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "transcript_sha256",
        "sas_confirmed_on_mac",
        "sas_confirmed_on_cardputer",
        "tls_exporter_length",
        "client_signature_valid",
        "wss_challenge_seen",
        "gatt_challenge_seen",
        "bind_challenge_complete"
      ],
      "properties": {
        "transcript_sha256": {"$ref": "#/$defs/sha256"},
        "sas_confirmed_on_mac": {"type": "boolean"},
        "sas_confirmed_on_cardputer": {"type": "boolean"},
        "tls_exporter_length": {"type": "integer", "minimum": 0, "maximum": 32},
        "client_signature_valid": {"type": "boolean"},
        "wss_challenge_seen": {"type": "boolean"},
        "gatt_challenge_seen": {"type": "boolean"},
        "bind_challenge_complete": {"type": "boolean"}
      }
    },
    "lan": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "selected_interface",
        "listener_interface",
        "mdns_interface",
        "interface_fingerprint_unchanged",
        "remote_in_selected_subnet",
        "public_listener_detected"
      ],
      "properties": {
        "selected_interface": {"type": "string", "minLength": 1},
        "listener_interface": {"type": "string", "minLength": 1},
        "mdns_interface": {"type": "string", "minLength": 1},
        "interface_fingerprint_unchanged": {"type": "boolean"},
        "remote_in_selected_subnet": {"type": "boolean"},
        "public_listener_detected": {"type": "boolean"}
      }
    },
    "overall_status": {"enum": ["pass", "fail", "blocked"]},
    "failures": {
      "type": "array",
      "items": {"type": "string", "minLength": 1}
    },
    "blockers": {
      "type": "array",
      "items": {"type": "string", "minLength": 1}
    }
  },
  "allOf": [
    {
      "if": {
        "properties": {"overall_status": {"const": "pass"}},
        "required": ["overall_status"]
      },
      "then": {
        "properties": {
          "signing": {
            "properties": {
              "a_valid": {"const": true},
              "b_valid": {"const": true},
              "not_adhoc": {"const": true},
              "designated_requirement_match": {"const": true},
              "accessibility_before_upgrade": {"const": true},
              "accessibility_after_upgrade": {"const": true}
            }
          },
          "unicode_targets": {
            "minItems": 5,
            "maxItems": 5,
            "items": {
              "allOf": [
                {"$ref": "#/$defs/unicode_target"},
                {
                  "properties": {
                    "expected_utf8_bytes": {"const": 1024},
                    "posted": {"const": 1024},
                    "verified": {"const": 1024},
                    "exact_match": {"const": true}
                  }
                }
              ]
            }
          },
          "negative_cases": {
            "properties": {
              "focus_switch": {
                "properties": {
                  "status": {"const": "partial"},
                  "error_code": {"const": "focus_changed"},
                  "posted": {"minimum": 1, "maximum": 1023}
                }
              },
              "secure_input": {
                "properties": {
                  "status": {"const": "failed"},
                  "error_code": {"const": "secure_input_active"},
                  "posted": {"const": 0},
                  "verified": {"const": 0}
                }
              },
              "restart": {
                "properties": {
                  "crash_exit_code": {"const": 86},
                  "recovered_status": {"const": "indeterminate"},
                  "auto_replayed": {"const": false}
                }
              }
            }
          },
          "bluetooth": {
            "properties": {
              "corebluetooth_notify_callbacks": {"minimum": 1},
              "authenticated_frames": {"minimum": 1},
              "protected_characteristic_access": {"const": true},
              "same_physical_device": {"const": true},
              "bad_mac_advanced_counter": {"const": false},
              "replay_rejected": {"const": true}
            }
          },
          "pairing": {
            "properties": {
              "sas_confirmed_on_mac": {"const": true},
              "sas_confirmed_on_cardputer": {"const": true},
              "tls_exporter_length": {"const": 32},
              "client_signature_valid": {"const": true},
              "wss_challenge_seen": {"const": true},
              "gatt_challenge_seen": {"const": true},
              "bind_challenge_complete": {"const": true}
            }
          },
          "lan": {
            "properties": {
              "interface_fingerprint_unchanged": {"const": true},
              "remote_in_selected_subnet": {"const": true},
              "public_listener_detected": {"const": false}
            }
          },
          "failures": {"maxItems": 0},
          "blockers": {"maxItems": 0}
        }
      }
    },
    {
      "if": {
        "properties": {"overall_status": {"const": "fail"}},
        "required": ["overall_status"]
      },
      "then": {
        "properties": {
          "failures": {"minItems": 1},
          "blockers": {"maxItems": 0}
        }
      }
    },
    {
      "if": {
        "properties": {"overall_status": {"const": "blocked"}},
        "required": ["overall_status"]
      },
      "then": {
        "properties": {"blockers": {"minItems": 1}}
      }
    }
  ],
  "$defs": {
    "sha256": {
      "type": "string",
      "pattern": "^[0-9a-f]{64}$"
    },
    "unicode_target": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "id",
        "bundle_id",
        "readback",
        "expected_utf8_bytes",
        "posted",
        "verified",
        "exact_match",
        "frontmost_pid",
        "element_fingerprint"
      ],
      "properties": {
        "id": {"enum": ["textedit", "vscode", "chrome", "terminal", "iterm2"]},
        "bundle_id": {
          "enum": [
            "com.apple.TextEdit",
            "com.microsoft.VSCode",
            "com.google.Chrome",
            "com.apple.Terminal",
            "com.googlecode.iterm2"
          ]
        },
        "readback": {"enum": ["ax_exact", "ax_suffix"]},
        "expected_utf8_bytes": {"const": 1024},
        "posted": {"type": "integer", "minimum": 0, "maximum": 1024},
        "verified": {"type": "integer", "minimum": 0, "maximum": 1024},
        "exact_match": {"type": "boolean"},
        "frontmost_pid": {"type": "integer", "minimum": 1},
        "element_fingerprint": {
          "type": "string",
          "pattern": "^sha256:[0-9a-f]{64}$"
        }
      }
    },
    "injection_result": {
      "type": "object",
      "additionalProperties": false,
      "required": ["status", "error_code", "posted", "verified"],
      "properties": {
        "status": {"enum": ["completed", "failed", "partial", "indeterminate"]},
        "error_code": {
          "type": ["string", "null"],
          "enum": [
            null,
            "permission_denied",
            "secure_input_active",
            "focus_changed",
            "partial",
            "indeterminate"
          ]
        },
        "posted": {"type": "integer", "minimum": 0, "maximum": 1024},
        "verified": {"type": "integer", "minimum": 0, "maximum": 1024}
      }
    }
  }
}
```

schema 验证后还必须由 runner 检查 `unicode_targets[].id` 恰好是五个不同值；JSON Schema 的 `uniqueItems` 不能只按单字段去重。

- [ ] **Step 4: 实现独立 HIL runner**

创建 `scripts/run_macos_hil.py`：

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

import jsonschema

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "docs" / "validation" / "phase0" / "macos-hil.schema.json"
TARGETS = (
    ("textedit", "com.apple.TextEdit", "TextEdit", "ax_exact"),
    ("vscode", "com.microsoft.VSCode", "Visual Studio Code", "ax_exact"),
    ("chrome", "com.google.Chrome", "Google Chrome", "ax_exact"),
    ("terminal", "com.apple.Terminal", "Terminal", "ax_suffix"),
    ("iterm2", "com.googlecode.iterm2", "iTerm", "ax_suffix"),
)


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def make_probe_text() -> str:
    prefix = "CCC-PHASE0-"
    remaining = 1024 - len(prefix.encode("utf-8"))
    chinese_count, ascii_count = divmod(remaining, 3)
    value = prefix + ("中" * chinese_count) + ("x" * ascii_count)
    if len(value.encode("utf-8")) != 1024:
        raise RuntimeError("probe text length invariant failed")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_checked(arguments: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(arguments, check=True, text=True, **kwargs)


def bundle_executable(app: Path) -> Path:
    return app / "Contents" / "MacOS" / "cardputer-phase0-probe"


def designated_requirement(app: Path) -> str:
    result = subprocess.run(
        ["codesign", "-d", "-r-", str(app)],
        text=True,
        capture_output=True,
        check=False,
    )
    for line in result.stderr.splitlines():
        if line.startswith("designated => "):
            return line.removeprefix("designated => ")
    return ""


def run_json(app: Path, arguments: list[str], output: Path) -> dict:
    if output.exists():
        output.unlink()
    run_checked([str(bundle_executable(app)), *arguments, "--output", str(output)])
    return json.loads(output.read_text(encoding="utf-8"))


def secure_request(path: Path, value: dict) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, sort_keys=True),
        encoding="utf-8",
    )
    path.chmod(0o600)


def activate(bundle_id: str) -> None:
    script = f'tell application id "{bundle_id}" to activate'
    run_checked(["osascript", "-e", script], capture_output=True)
    time.sleep(3)


def inject_target(
    app: Path,
    work: Path,
    target_id: str,
    bundle_id: str,
    readback: str,
    text: str,
) -> dict:
    print(
        f"Prepare a blank editable field in {bundle_id}. "
        "The app will activate in three seconds; click its text field and do not type."
    )
    activate(bundle_id)
    request = work / f"{target_id}-request.json"
    output = work / f"{target_id}-result.json"
    secure_request(
        request,
        {
            "pairedDeviceID": "phase0-hil",
            "operationID": str(uuid.uuid4()),
            "text": text,
            "crashAfterPostedPrefix": None,
            "chunkDelayMilliseconds": 0,
        },
    )
    result = run_json(
        app,
        [
            "inject",
            "--request",
            str(request),
            "--ledger",
            str(work / "unicode.sqlite3"),
        ],
        output,
    )
    return {
        "id": target_id,
        "bundle_id": bundle_id,
        "readback": readback,
        "expected_utf8_bytes": 1024,
        "posted": result["postedPrefixLength"],
        "verified": result["verifiedPrefixLength"],
        "exact_match": result["verifiedPrefixLength"] == 1024,
        "frontmost_pid": result["targetPID"],
        "element_fingerprint": result["targetElementFingerprint"],
    }


def evaluate_gate(
    checks: dict[str, bool],
    targets: list[dict],
) -> tuple[str, list[str]]:
    failures = [name for name, passed in checks.items() if not passed]
    expected = {"textedit", "vscode", "chrome", "terminal", "iterm2"}
    observed = {item["id"] for item in targets}
    if observed != expected:
        failures.append("five_app_target_set")
    for item in targets:
        if not (
            item["exact_match"]
            and item["posted"] == 1024
            and item["verified"] == 1024
        ):
            failures.append(f"unicode_{item['id']}")
    return ("pass", []) if not failures else ("fail", sorted(set(failures)))


def write_blocked(
    args: argparse.Namespace,
    started: str,
    run_id: str,
    blockers: list[str],
) -> int:
    zero_hash = "0" * 64
    evidence = {
        "schema_version": "1.0",
        "run_id": run_id,
        "git_commit": run_checked(
            ["git", "rev-parse", "HEAD"],
            cwd=ROOT,
            capture_output=True,
        ).stdout.strip(),
        "toolchain_manifest_sha256": (
            sha256_file(args.toolchain_manifest)
            if args.toolchain_manifest.is_file()
            else zero_hash
        ),
        "started_at": started,
        "completed_at": now_iso(),
        "host": {
            "macos_version": platform.mac_ver()[0] or "unavailable",
            "hardware": platform.machine() or "unavailable",
            "swift_version": (
                subprocess.run(
                    ["swift", "--version"],
                    text=True,
                    capture_output=True,
                    check=False,
                ).stdout.splitlines()
                or ["unavailable"]
            )[0],
            "probe_sha256": zero_hash,
        },
        "signing": {
            "bundle_id": "lc.iam.cardputer.phase0probe",
            "a_valid": False,
            "b_valid": False,
            "not_adhoc": False,
            "designated_requirement_match": False,
            "accessibility_before_upgrade": False,
            "accessibility_after_upgrade": False,
        },
        "unicode_targets": [],
        "negative_cases": {
            "focus_switch": {
                "status": "indeterminate",
                "error_code": "indeterminate",
                "posted": 0,
                "verified": 0,
            },
            "secure_input": {
                "status": "indeterminate",
                "error_code": "indeterminate",
                "posted": 0,
                "verified": 0,
            },
            "restart": {
                "crash_exit_code": 0,
                "recovered_status": "missing",
                "auto_replayed": False,
            },
        },
        "bluetooth": {
            "corebluetooth_notify_callbacks": 0,
            "authenticated_frames": 0,
            "protected_characteristic_access": False,
            "hid_identity_sha256": zero_hash,
            "gatt_identity_sha256": zero_hash,
            "same_physical_device": False,
            "bad_mac_advanced_counter": False,
            "replay_rejected": False,
        },
        "pairing": {
            "transcript_sha256": zero_hash,
            "sas_confirmed_on_mac": False,
            "sas_confirmed_on_cardputer": False,
            "tls_exporter_length": 0,
            "client_signature_valid": False,
            "wss_challenge_seen": False,
            "gatt_challenge_seen": False,
            "bind_challenge_complete": False,
        },
        "lan": {
            "selected_interface": args.interface,
            "listener_interface": "unavailable",
            "mdns_interface": "unavailable",
            "interface_fingerprint_unchanged": False,
            "remote_in_selected_subnet": False,
            "public_listener_detected": False,
        },
        "overall_status": "blocked",
        "failures": [],
        "blockers": sorted(set(blockers)),
    }
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(
        schema,
        format_checker=jsonschema.FormatChecker(),
    ).validate(evidence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"blocked: {args.output}")
    return 2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle-a", type=Path, required=True)
    parser.add_argument("--bundle-b", type=Path, required=True)
    parser.add_argument("--toolchain-manifest", type=Path, required=True)
    parser.add_argument("--interface", required=True)
    parser.add_argument("--interface-address", required=True)
    parser.add_argument("--interface-netmask", required=True)
    parser.add_argument("--peripheral-id", required=True)
    parser.add_argument("--device-id-hex", required=True)
    parser.add_argument("--gatt-secret-file", type=Path, required=True)
    parser.add_argument("--tls-identity-label", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    started = now_iso()
    run_id = str(uuid.uuid4())
    blockers: list[str] = []
    if not args.toolchain_manifest.is_file():
        blockers.append("missing_toolchain_manifest")
    for app in (args.bundle_a, args.bundle_b):
        if not app.is_dir():
            blockers.append(f"missing_signed_bundle:{app}")
        elif subprocess.run(
            ["codesign", "--verify", "--deep", "--strict", str(app)],
            capture_output=True,
            check=False,
        ).returncode != 0:
            blockers.append(f"invalid_signature:{app}")
    if not args.gatt_secret_file.is_file():
        blockers.append("missing_gatt_secret_file")
    elif (args.gatt_secret_file.stat().st_mode & 0o777) != 0o600:
        blockers.append("gatt_secret_mode_is_not_0600")
    for _target_id, bundle_id, name, _readback in TARGETS:
        found = subprocess.run(
            ["mdfind", f"kMDItemCFBundleIdentifier == '{bundle_id}'"],
            text=True,
            capture_output=True,
            check=False,
        ).stdout.strip()
        if not found:
            blockers.append(f"missing_target_app:{name}")
    if blockers:
        if (
            args.gatt_secret_file.is_file()
            and (args.gatt_secret_file.stat().st_mode & 0o777) == 0o600
        ):
            args.gatt_secret_file.unlink()
        return write_blocked(args, started, run_id, blockers)

    with tempfile.TemporaryDirectory(prefix="cardputer-phase0-hil-") as directory:
        work = Path(directory)
        installed = work / "Cardputer Phase0 Probe.app"
        shutil.copytree(args.bundle_a, installed)
        permission_a = run_json(
            installed,
            ["permission-status"],
            work / "permission-a.json",
        )
        if not permission_a["accessibilityTrusted"]:
            print(
                "Grant Accessibility to Cardputer Phase0 Probe in "
                "System Settings > Privacy & Security > Accessibility, then press Return."
            )
            input()
            permission_a = run_json(
                installed,
                ["permission-status"],
                work / "permission-a-after-grant.json",
            )
        if not permission_a["accessibilityTrusted"]:
            args.gatt_secret_file.unlink(missing_ok=True)
            return write_blocked(
                args,
                started,
                run_id,
                ["accessibility_not_granted"],
            )

        text = make_probe_text()
        targets = [
            inject_target(installed, work, target_id, bundle_id, readback, text)
            for target_id, bundle_id, _name, readback in TARGETS
        ]

        print(
            "Focus a blank TextEdit document. VS Code will be activated automatically "
            "during injection to prove focus-change handling."
        )
        activate("com.apple.TextEdit")
        focus_request = work / "focus-request.json"
        secure_request(
            focus_request,
            {
                "pairedDeviceID": "phase0-hil",
                "operationID": str(uuid.uuid4()),
                "text": text,
                "crashAfterPostedPrefix": None,
                "chunkDelayMilliseconds": 250,
            },
        )
        focus_output = work / "focus-result.json"
        process = subprocess.Popen(
            [
                str(bundle_executable(installed)),
                "inject",
                "--request",
                str(focus_request),
                "--ledger",
                str(work / "unicode.sqlite3"),
                "--output",
                str(focus_output),
            ]
        )
        time.sleep(1)
        activate("com.microsoft.VSCode")
        if process.wait(timeout=20) != 0:
            raise RuntimeError("focus-switch probe process failed")
        focus_result = json.loads(focus_output.read_text(encoding="utf-8"))

        print(
            "Enable Terminal > Secure Keyboard Entry, leave Terminal frontmost, "
            "then press Return here and immediately reactivate Terminal."
        )
        input()
        activate("com.apple.Terminal")
        secure_request_path = work / "secure-input-request.json"
        secure_request(
            secure_request_path,
            {
                "pairedDeviceID": "phase0-hil",
                "operationID": str(uuid.uuid4()),
                "text": text,
                "crashAfterPostedPrefix": None,
                "chunkDelayMilliseconds": 0,
            },
        )
        secure_result = run_json(
            installed,
            [
                "inject",
                "--request",
                str(secure_request_path),
                "--ledger",
                str(work / "unicode.sqlite3"),
            ],
            work / "secure-input-result.json",
        )
        print("Disable Terminal > Secure Keyboard Entry before continuing.")
        input()

        activate("com.apple.TextEdit")
        crash_request = work / "crash-request.json"
        secure_request(
            crash_request,
            {
                "pairedDeviceID": "phase0-hil",
                "operationID": str(uuid.uuid4()),
                "text": text,
                "crashAfterPostedPrefix": 60,
                "chunkDelayMilliseconds": 0,
            },
        )
        crash = subprocess.run(
            [
                str(bundle_executable(installed)),
                "inject",
                "--request",
                str(crash_request),
                "--ledger",
                str(work / "unicode.sqlite3"),
                "--output",
                str(work / "crash-result.json"),
            ],
            check=False,
        )
        recovered = run_json(
            installed,
            ["recover-ledger", "--ledger", str(work / "unicode.sqlite3")],
            work / "recovered.json",
        )

        shutil.rmtree(installed)
        shutil.copytree(args.bundle_b, installed)
        permission_b = run_json(
            installed,
            ["permission-status"],
            work / "permission-b.json",
        )

        pair_output = work / "pair-gatt.json"
        pair_arguments = [
            "pair-gatt-hil",
            "--interface",
            args.interface,
            "--interface-address",
            args.interface_address,
            "--interface-netmask",
            args.interface_netmask,
            "--peripheral-id",
            args.peripheral_id,
            "--device-id-hex",
            args.device_id_hex,
            "--gatt-secret-file",
            str(args.gatt_secret_file),
            "--tls-identity-label",
            args.tls_identity_label,
        ]
        try:
            pair_result = run_json(installed, pair_arguments, pair_output)
        except subprocess.CalledProcessError:
            args.gatt_secret_file.unlink(missing_ok=True)
            return write_blocked(
                args,
                started,
                run_id,
                ["cardputer_pair_gatt_hil_unavailable"],
            )

        requirement_a = designated_requirement(args.bundle_a)
        requirement_b = designated_requirement(args.bundle_b)
        signing = {
            "bundle_id": "lc.iam.cardputer.phase0probe",
            "a_valid": True,
            "b_valid": True,
            "not_adhoc": bool(requirement_a and requirement_b),
            "designated_requirement_match": requirement_a == requirement_b,
            "accessibility_before_upgrade": permission_a["accessibilityTrusted"],
            "accessibility_after_upgrade": permission_b["accessibilityTrusted"],
        }
        negative = {
            "focus_switch": {
                "status": focus_result["status"],
                "error_code": focus_result["errorCode"],
                "posted": focus_result["postedPrefixLength"],
                "verified": focus_result["verifiedPrefixLength"],
            },
            "secure_input": {
                "status": secure_result["status"],
                "error_code": secure_result["errorCode"],
                "posted": secure_result["postedPrefixLength"],
                "verified": secure_result["verifiedPrefixLength"],
            },
            "restart": {
                "crash_exit_code": crash.returncode,
                "recovered_status": (
                    recovered[0]["status"] if recovered else "missing"
                ),
                "auto_replayed": False,
            },
        }
        checks = {
            "signing": all(
                (
                    signing["a_valid"],
                    signing["b_valid"],
                    signing["not_adhoc"],
                    signing["designated_requirement_match"],
                )
            ),
            "tcc_upgrade": all(
                (
                    signing["accessibility_before_upgrade"],
                    signing["accessibility_after_upgrade"],
                )
            ),
            "focus_partial": (
                negative["focus_switch"]["status"] == "partial"
                and 0 < negative["focus_switch"]["posted"] < 1024
            ),
            "secure_input": (
                negative["secure_input"]["status"] == "failed"
                and negative["secure_input"]["error_code"]
                == "secure_input_active"
                and negative["secure_input"]["posted"] == 0
            ),
            "restart_indeterminate": (
                crash.returncode == 86
                and negative["restart"]["recovered_status"] == "indeterminate"
            ),
            "ble_notify": pair_result["bluetooth"]["corebluetooth_notify_callbacks"] >= 1,
            "same_identity": pair_result["bluetooth"]["same_physical_device"],
            "pairing": all(
                (
                    pair_result["pairing"]["sas_confirmed_on_mac"],
                    pair_result["pairing"]["sas_confirmed_on_cardputer"],
                    pair_result["pairing"]["client_signature_valid"],
                    pair_result["pairing"]["bind_challenge_complete"],
                )
            ),
            "selected_lan": (
                pair_result["lan"]["selected_interface"]
                == pair_result["lan"]["listener_interface"]
                == pair_result["lan"]["mdns_interface"]
                and not pair_result["lan"]["public_listener_detected"]
            ),
        }
        status, failures = evaluate_gate(checks, targets)
        evidence = {
            "schema_version": "1.0",
            "run_id": run_id,
            "git_commit": run_checked(
                ["git", "rev-parse", "HEAD"],
                cwd=ROOT,
                capture_output=True,
            ).stdout.strip(),
            "toolchain_manifest_sha256": sha256_file(
                args.toolchain_manifest
            ),
            "started_at": started,
            "completed_at": now_iso(),
            "host": {
                "macos_version": platform.mac_ver()[0],
                "hardware": platform.machine(),
                "swift_version": subprocess.run(
                    ["swift", "--version"],
                    text=True,
                    capture_output=True,
                    check=True,
                ).stdout.splitlines()[0],
                "probe_sha256": sha256_file(bundle_executable(installed)),
            },
            "signing": signing,
            "unicode_targets": targets,
            "negative_cases": negative,
            "bluetooth": pair_result["bluetooth"],
            "pairing": pair_result["pairing"],
            "lan": pair_result["lan"],
            "overall_status": status,
            "failures": failures,
            "blockers": [],
        }
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        validator = jsonschema.Draft202012Validator(
            schema,
            format_checker=jsonschema.FormatChecker(),
        )
        validator.validate(evidence)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".new")
        temporary.write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(args.output)
        args.gatt_secret_file.unlink(missing_ok=True)
        print(f"{status}: {args.output}")
        return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
```

runner 的真实实现还要把 probe `InjectionResult` 包成下列非敏感字段，供 `inject_target` 使用：

```json
{
  "operationID": "uuid",
  "status": "completed",
  "postedPrefixLength": 1024,
  "verifiedPrefixLength": 1024,
  "errorCode": null,
  "targetPID": 123,
  "targetElementFingerprint": "sha256:64-lowercase-hex"
}
```

禁止把 `text`、AX 完整值、initial/final AX value、SAS、secret 或完整 device ID 加入 result。`exact_match` 由 `verifiedPrefixLength == 1024` 推导；engine 只有在 `current AX value == initial AX value + original prefix` 时才能推进 verified。

- [ ] **Step 5: 写入精确人工检查点**

创建 `docs/validation/phase0/macos-hil-operator.md`：

```markdown
# Phase 0 macOS HIL Operator Runbook

1. 确认 Cardputer 固件探针显示 development build，已经与本 Mac 完成唯一 HID bond，并保持 Wi-Fi 与 BLE 同时在线。
2. 用 `security find-identity -v -p codesigning` 选择 Apple Development 或 Developer ID Application 身份；不要选择 adhoc。
3. 构建 A/B 后分别运行 `codesign --verify --deep --strict`，并保存 designated requirement 的比较结果。
4. 在 TextEdit 创建空白纯文本文档；在 VS Code 创建空白 Untitled Text；在 Chrome 打开一个本地空白 `<textarea autofocus>` 页面并点击 textarea；在 Terminal 与 iTerm2 各创建一个无待输入命令的新 shell。
5. HIL runner 激活每个 app 后不要切换窗口或输入。TextEdit、VS Code、Chrome 必须从聚焦 AX element 读到完整值；Terminal、iTerm2 必须从聚焦 AX element 读到原值加 1024-byte suffix。
6. Accessibility 提示出现时，只给 `Cardputer Phase0 Probe.app` 授权。禁止给未签名 Swift binary 授权，禁止运行 `tccutil reset`。
7. 焦点切换用例开始后保持 TextEdit 聚焦；runner 会在注入中途激活 VS Code，结果必须为 `partial`，且不得补发余文。
8. Secure Input 用例前，在 Terminal 菜单显式开启 Secure Keyboard Entry；确认结果为 `failed`、`secure_input_active`、posted 0 后立即关闭。
9. 崩溃用例只接受进程 exit 86；重启查询必须是 `indeterminate`，并人工确认没有自动补发。
10. 配对时逐位比较 Mac 与 Cardputer 上的 6 位 SAS，分别在两端确认。任一位不同立即拒绝。
11. 在 Cardputer 屏幕确认 WSS 与 GATT 收到相同 bind challenge；Mac 结果必须同时记录两个 channel，且 TLS exporter 长度为 32。
12. CoreBluetooth 结果必须包含至少一个真实 `didUpdateValueFor` callback。随后读取 HID serial；严格 base32 解码后，只有一个 16-byte HID device ID 与受保护 GATT raw device ID 相同时通过。
13. 断开并重连 BLE，确认新的 connection ID 使 counter 从 0 重新开始；旧连接帧、重复 counter 和坏 MAC 都必须被拒绝。
14. 确认 WSS listener 与 `_codex-companion._tcp.local` 只发布在命令行指定接口。切换接口后旧 listener 必须停止，并要求重新确认。
15. 用 JSON Schema 校验证据。任一 app 缺失、签名身份缺失、TCC 未授权、Secure Input 未按要求切换、Cardputer 不在线或接口漂移时记录 `blocked`，不能手填通过。
```

- [ ] **Step 6: 运行 runner 单元测试、schema 自检和源码隐私扫描**

Run:

```bash
uv run python scripts/test_run_macos_hil.py
uv run check-jsonschema \
  --schemafile https://json-schema.org/draft/2020-12/schema \
  docs/validation/phase0/macos-hil.schema.json
python3 scripts/check_macos_injection_policy.py
rg -n '"text"|"sas"|"gatt_auth_key"|"device_private_key"' \
  docs/validation/phase0/macos-hil.schema.json
```

Expected:

- runner tests `3 tests` 全部通过；
- schema 自检退出 0；
- native Unicode policy 输出 `native-unicode-policy: pass`；
- 最后一条 `rg` 退出 1 且无输出，证明 evidence schema 不提供正文、SAS 或秘密字段。

- [ ] **Step 7: 在 live run 前提交已验证的 HIL harness**

```bash
git add scripts/run_macos_hil.py scripts/test_run_macos_hil.py \
  docs/validation/phase0/macos-hil.schema.json \
  docs/validation/phase0/macos-hil-operator.md
git commit -m "test: add macos unicode and ble hil harness"
```

不得用未提交 runner 生成 gate 证据。等待 firmware 与 release-security harness 也提交后，要求 clean tree，并使用共同 `HIL_BASE_COMMIT`。

- [ ] **Step 8: 执行真实签名 A/B 与真机 HIL**

Run:

```bash
scripts/build_signed_macos_probe.sh a build/phase0/macos/a
scripts/build_signed_macos_probe.sh b build/phase0/macos/b
uv run python scripts/run_macos_hil.py \
  --bundle-a 'build/phase0/macos/a/Cardputer Phase0 Probe.app' \
  --bundle-b 'build/phase0/macos/b/Cardputer Phase0 Probe.app' \
  --toolchain-manifest build/phase0/toolchain.json \
  --interface en0 \
  --interface-address 192.168.1.10 \
  --interface-netmask 255.255.255.0 \
  --peripheral-id aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee \
  --device-id-hex 00112233445566778899aabbccddeeff \
  --gatt-secret-file build/phase0/macos/gatt-secret.bin \
  --tls-identity-label cardputer-phase0-wss \
  --output build/phase0/macos-hil/run/raw.json
```

Expected: 最后一行是 `pass: build/phase0/macos-hil/run/raw.json`，证据通过 schema，五个 target 全部精确匹配，TCC B 未再次要求授权，真实 notify count 至少为 1。

命令中的 interface address、peripheral UUID、raw device ID 和 Keychain label 必须使用当前 HIL 会话由探针显示或系统只读枚举得到的实际值。示例值不可用于验收；任何实际值尚未取得时 runner 必须在写证据前返回 `blocked`。

- [ ] **Step 9: 记录失败或通过结论并只提交脱敏摘要**

如果 runner 返回 `fail`，将原始证据保留在被忽略的 `build/phase0/macos-hil/`，并在 `docs/validation/phase0/macos-hil.md` 写入脱敏事实、artifact SHA-256 和精确 failure ID，停止 Phase 1。若环境缺失导致无法执行，创建 schema-valid 的 `blocked` 原始结果和脱敏摘要并列出 blocker；不得把单元测试当作 HIL pass。

```bash
git add docs/validation/phase0/macos-hil.md
git commit -m "docs: record macos unicode and ble hil evidence"
```

## Final Phase 0 Exit Check

执行者在向总 Phase 0 计划回报前必须逐项检查：

- [ ] `swift test --package-path companion` 全部通过。
- [ ] `python3 scripts/check_macos_injection_policy.py` 通过。
- [ ] A/B bundle 都通过 `codesign --verify --deep --strict`，不是 adhoc，designated requirement 相同。
- [ ] Accessibility 在 B 第一次启动前已经保持授权，没有再次授权。
- [ ] TextEdit、VS Code、Chrome、Terminal、iTerm2 五项均 `posted=verified=1024` 且 `exact_match=true`。
- [ ] 焦点切换为 `partial`，Secure Input 为 `failed/secure_input_active`，crash 恢复为 `indeterminate`。
- [ ] GATT 坏 MAC 未推进 counter，replay 被拒绝，真实 CoreBluetooth notify count 至少为 1。
- [ ] IOHID serial 的 base32 结果与受保护 GATT raw device ID 唯一匹配。
- [ ] 双端 SAS、真实 TLS exporter 签名和 WSS/GATT bind challenge 全部通过。
- [ ] listener 与 mDNS 只位于用户选择的本地接口，接口变化会停止服务。
- [ ] evidence JSON 通过 schema，且不包含正文、完整 AX value、SAS、密钥或完整 device ID。
- [ ] 任一项失败时整体为 `fail`；任一必要环境不可用时整体为 `blocked`；两者都不得进入 Phase 1。
