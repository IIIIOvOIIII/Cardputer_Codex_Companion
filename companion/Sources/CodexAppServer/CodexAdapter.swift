import Foundation
import ProductContracts

public final class CodexAdapter {
    private let rpc: CodexRPCClient
    private let telemetryReader: CodexTelemetryReader
    private var threads: [[String: Any]] = []
    private var selectedIndex = 0
    private var sequence: UInt64 = 0

    public init(rpc: CodexRPCClient = JSONRPCProcess()) {
        self.rpc = rpc
        self.telemetryReader = CodexTelemetryReader(rpc: rpc)
    }

    public func start() throws {
        try rpc.start()
    }

    public func snapshot() throws -> CompanionSnapshot {
        let result = try rpc.request(
            method: "thread/list",
            params: [
                "limit": 50,
                "sortKey": "updated_at",
                "sortDirection": "desc"
            ]
        )
        threads = result["data"] as? [[String: Any]] ?? []
        if let active = threads.firstIndex(where: {
            (($0["status"] as? [String: Any])?["type"] as? String) == "active"
        }) {
            selectedIndex = active
        } else if !threads.isEmpty {
            selectedIndex = min(selectedIndex, threads.count - 1)
        } else {
            selectedIndex = 0
        }
        sequence += 1
        guard !threads.isEmpty else {
            return CompanionSnapshot(
                sequence: sequence,
                sessionID: "",
                title: "NO SESSION",
                cwd: "-",
                state: "idle",
                approvals: 0,
                inputs: 0
            )
        }
        let selected = threads[selectedIndex]
        let telemetry = try? telemetryReader.read(
            thread: selected,
            now: Date()
        )
        return normalize(selected, telemetry: telemetry)
    }

    public func perform(_ action: RemoteAction) throws {
        switch action {
        case .none:
            return
        case .selectNext:
            if !threads.isEmpty {
                selectedIndex = (selectedIndex + 1) % threads.count
            }
        case .selectPrevious:
            if !threads.isEmpty {
                selectedIndex = (selectedIndex + threads.count - 1) % threads.count
            }
        case .interrupt:
            try interruptSelected()
        case .approve:
            try rpc.respondToPendingApproval(approved: true)
        case .reject:
            try rpc.respondToPendingApproval(approved: false)
        case .newSession, .provideInput:
            // These operations require additional user input or an outstanding
            // app-server request. They are deliberately fail-closed here.
            return
        }
    }

    private func normalize(
        _ thread: [String: Any],
        telemetry: CodexTelemetry?
    ) -> CompanionSnapshot {
        let status = thread["status"] as? [String: Any] ?? [:]
        let flags = status["activeFlags"] as? [String] ?? []
        let title = (thread["name"] as? String).flatMap {
            $0.isEmpty ? nil : $0
        } ?? (thread["preview"] as? String) ?? "Codex session"
        return CompanionSnapshot(
            sequence: sequence,
            sessionID: thread["id"] as? String ?? "",
            title: title,
            cwd: thread["cwd"] as? String ?? "-",
            state: status["type"] as? String ?? "unknown",
            approvals: flags.contains("waitingOnApproval") ? 1 : 0,
            inputs: flags.contains("waitingOnUserInput") ? 1 : 0,
            petState: PetState.resolve(
                sessionState: status["type"] as? String ?? "unknown",
                flags: flags
            ),
            model: telemetry?.model,
            thinkingLevel: telemetry?.thinkingLevel,
            fast: telemetry?.fast,
            limits: telemetry?.limits
        )
    }

    private func interruptSelected() throws {
        guard threads.indices.contains(selectedIndex),
              let threadID = threads[selectedIndex]["id"] as? String else {
            return
        }
        let result = try rpc.request(
            method: "thread/read",
            params: ["threadId": threadID, "includeTurns": true]
        )
        guard let thread = result["thread"] as? [String: Any],
              let turns = thread["turns"] as? [[String: Any]],
              let activeTurn = turns.last(where: {
                  ($0["status"] as? String) == "inProgress"
              }),
              let turnID = activeTurn["id"] as? String else {
            return
        }
        _ = try rpc.request(
            method: "turn/interrupt",
            params: ["threadId": threadID, "turnId": turnID]
        )
    }
}
