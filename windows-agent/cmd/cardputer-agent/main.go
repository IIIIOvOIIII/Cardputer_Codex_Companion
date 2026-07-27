package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/config"
	"github.com/cardputer/codex-companion/windows-agent/internal/device"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "cardputer-agent:", err)
		os.Exit(1)
	}
}

func run(arguments []string) error {
	if len(arguments) == 0 {
		return errors.New("usage: cardputer-agent pair|heartbeat")
	}
	protector, err := config.NewPlatformProtector()
	if err != nil {
		return err
	}
	configPath, err := defaultConfigPath()
	if err != nil {
		return err
	}

	switch arguments[0] {
	case "pair":
		flags := flag.NewFlagSet("pair", flag.ContinueOnError)
		deviceAddress := flags.String("device", "", "Cardputer HTTPS origin")
		path := flags.String("config", configPath, "secure configuration path")
		if err := flags.Parse(arguments[1:]); err != nil {
			return err
		}
		if *deviceAddress == "" {
			return errors.New("--device is required")
		}
		fmt.Fprint(os.Stderr, "Device PIN: ")
		pin, err := bufio.NewReader(os.Stdin).ReadString('\n')
		fmt.Fprintln(os.Stderr)
		if err != nil {
			return errors.New("read device PIN")
		}
		pin = strings.TrimSpace(pin)
		ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer cancel()
		fingerprint, _, err := device.FirstPair(ctx, *deviceAddress, pin)
		if err != nil {
			return err
		}
		store := config.Store{Path: *path, Protector: protector}
		if err := store.Save(config.Config{
			DeviceURL:         *deviceAddress,
			CertificateSHA256: fingerprint,
		}, pin); err != nil {
			return err
		}
		fmt.Println("Device paired.")
		return nil
	case "heartbeat":
		flags := flag.NewFlagSet("heartbeat", flag.ContinueOnError)
		path := flags.String("config", configPath, "secure configuration path")
		if err := flags.Parse(arguments[1:]); err != nil {
			return err
		}
		stored, pin, err := (config.Store{Path: *path, Protector: protector}).Load()
		if err != nil {
			return err
		}
		client, err := device.NewClient(stored.DeviceURL, pin, stored.CertificateSHA256)
		if err != nil {
			return err
		}
		ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer cancel()
		action, err := client.Heartbeat(ctx)
		if err != nil {
			return err
		}
		fmt.Printf("sequence=%d action=%s needs_snapshot=%t\n",
			action.Sequence, action.Action, action.NeedsSnapshot)
		return nil
	default:
		return errors.New("usage: cardputer-agent pair|heartbeat")
	}
}

func defaultConfigPath() (string, error) {
	root, err := os.UserConfigDir()
	if err != nil {
		return "", errors.New("locate user configuration directory")
	}
	return filepath.Join(root, "CardputerCodexCompanion", "config.json"), nil
}
