//go:build windows

package main

import "os/exec"

const scheduledTaskName = "Cardputer Codex Companion"

func triggerScheduledTask() error {
	return exec.Command(
		"schtasks.exe",
		"/Run",
		"/TN",
		scheduledTaskName,
	).Run()
}

func scheduledTaskInstalled() bool {
	return exec.Command(
		"schtasks.exe",
		"/Query",
		"/TN",
		scheduledTaskName,
	).Run() == nil
}
