package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/app"
	"github.com/cardputer/codex-companion/windows-agent/internal/codex"
	"github.com/cardputer/codex-companion/windows-agent/internal/config"
	"github.com/cardputer/codex-companion/windows-agent/internal/device"
	"github.com/cardputer/codex-companion/windows-agent/internal/pet"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "cardputer-agent:", err)
		os.Exit(1)
	}
}

func run(arguments []string) error {
	if len(arguments) == 0 {
		return errors.New("usage: cardputer-agent pair|heartbeat|run")
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
	case "run":
		flags := flag.NewFlagSet("run", flag.ContinueOnError)
		path := flags.String("config", configPath, "secure configuration path")
		codexExecutable := flags.String("codex", "codex", "Codex CLI executable")
		if err := flags.Parse(arguments[1:]); err != nil {
			return err
		}
		stored, pin, err := (config.Store{
			Path: *path, Protector: protector,
		}).Load()
		if err != nil {
			return err
		}
		deviceClient, err := device.NewClient(
			stored.DeviceURL,
			pin,
			stored.CertificateSHA256,
		)
		if err != nil {
			return err
		}
		rpc := codex.NewProcess(*codexExecutable)
		machine := codex.NewAdapter(rpc)
		ctx, stop := signal.NotifyContext(
			context.Background(),
			os.Interrupt,
			syscall.SIGTERM,
		)
		defer stop()
		if err := machine.Start(ctx); err != nil {
			return err
		}
		defer machine.Close()
		agent := app.NewAgent(deviceClient, machine)
		selection := pet.SelectionReader{}
		transcoder := pet.Transcoder{}
		agent.SetPetSynchronizer(
			pet.NewCoordinator(
				selection.SelectedSource,
				transcoder.Transcode,
				deviceClient,
			),
		)
		agent.SetPairingMigrationHandler(
			stored.PINRevision,
			func(next string, revision uint32) error {
				updated := stored
				updated.PINRevision = revision
				if err := (config.Store{
					Path: *path, Protector: protector,
				}).Save(updated, next); err != nil {
					return err
				}
				if err := deviceClient.UpdatePairing(next); err != nil {
					return err
				}
				stored = updated
				return nil
			},
		)
		for {
			stepCtx, cancel := context.WithTimeout(ctx, 15*time.Second)
			stepErr := agent.Step(stepCtx)
			cancel()
			if stepErr != nil && ctx.Err() == nil {
				fmt.Fprintln(os.Stderr, "sync warning:", stepErr)
			}
			timer := time.NewTimer(2 * time.Second)
			select {
			case <-ctx.Done():
				timer.Stop()
				return nil
			case <-timer.C:
			}
		}
	default:
		return errors.New("usage: cardputer-agent pair|heartbeat|run")
	}
}

func defaultConfigPath() (string, error) {
	root, err := os.UserConfigDir()
	if err != nil {
		return "", errors.New("locate user configuration directory")
	}
	return filepath.Join(root, "CardputerCodexCompanion", "config.json"), nil
}
