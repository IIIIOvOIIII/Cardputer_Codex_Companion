# Launcher Wi-Fi Recovery Progress

## 2026-07-28 21:46 HKT

- Current work: Reproduced and decoded the `1.3.1l` post-Wi-Fi reboot loop on
  the attached Cardputer, then defined the Wi-Fi event isolation, deferred scan,
  and Backspace Y/N full Companion reset design.
- Expected result: Establish an evidence-backed implementation boundary that
  fixes both the reboot and E294 while preserving M5Launcher.
- Result: Achieved. Serial logs show `sys_evt` stack overflow immediately after
  got-IP, and address decoding resolves the application frame to the synchronous
  online notification in `wifi_manager.cpp`. The user approved the recommended
  design and selected full Companion data deletion.
- Next step: Complete written-spec review, create the implementation plan and
  isolated worktree, then write failing tests before production code.
