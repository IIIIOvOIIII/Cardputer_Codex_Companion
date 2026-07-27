//go:build !windows

package main

import "errors"

func triggerScheduledTask() error {
	return errors.New("Windows Scheduled Task is unavailable")
}

func scheduledTaskInstalled() bool {
	return false
}
