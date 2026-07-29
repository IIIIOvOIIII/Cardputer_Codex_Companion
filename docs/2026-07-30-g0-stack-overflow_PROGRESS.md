# G0 Stack Overflow Repair Progress

## 2026-07-30 00:49 HKT

- Current work: Capture the intermittent G0 reboot on the attached Cardputer and establish the root cause before changing firmware.
- Expected result: A complete serial failure window that identifies the exact task, trigger sequence, reset reason, and relevant resource watermark.
- Result: Achieved. With G0 dual action enabled, the first G0 press completed but reduced the `product-macro` task high-water free stack from 1,080 bytes to 24 bytes. The second press sent the configured Cmd+I HID report, completed the dual-action path, and immediately triggered `***ERROR*** A stack overflow in task product-macro has been detected`, followed by `RTC_SW_CPU_RST`. The captured ELF SHA-256 prefix `966ec57b2` exactly matches the 1.3.4 Launcher ELF, and address decoding confirms the FreeRTOS stack-overflow hook. Disabled G0 presses did not enter the macro path or reset.
- Next step: Confirm the minimal 1.3.5 repair design: increase the static macro task stack with a fail-closed source contract and expand hardware validation from a single G0 click to repeated enabled clicks.

## 2026-07-30 01:02 HKT

- Current work: Define the user-selected dedicated G0 task design and its concurrency, fallback, memory, HIL, and release boundaries.
- Expected result: G0 no longer consumes the shared macro task stack, while direct HID ordering, Profile-macro serialization, and Mic fallback remain exact.
- Result: Achieved for design. The proposed `product-g0` task uses a 3,072-byte static stack and eight-entry queue. A shared execution mutex serializes complete MacroEngine operations across Profile and G0 workers. Queue or mutex failure sends no partial chord and still enqueues Mic. Repeated HIL expands to 20 sequential enabled clicks with a 768-byte minimum G0 stack-headroom gate. All active release surfaces advance to 1.3.5/1.3.5l.
- Next step: Commit the design, complete the written-spec review gate, then produce the test-driven implementation plan.

## 2026-07-30 01:09 HKT

- Current work: Convert the approved dedicated-task design into a file-specific TDD, build, HIL, integration, and publication plan.
- Expected result: Every design requirement maps to a bounded task with exact interfaces, RED/GREEN commands, commits, hardware gates, and release verification.
- Result: Achieved. The plan separates target task wiring, repeated HIL, 1.3.5 version unification, clean build/app-only flash, and public publication. It explicitly gates macro/G0 HID serialization, Mic fallback, 20/20 transitions, zero reset/queue failure, at least 768 bytes G0 stack headroom, Launcher `0x190000` fit, credential exclusion, and digest-pinned Pages deployment.
- Next step: Commit the plan, create an isolated execution worktree, and execute it inline with review checkpoints.
