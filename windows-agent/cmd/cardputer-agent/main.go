package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
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
	"golang.org/x/term"
)

var version = "dev"

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "cardputer-agent:", err)
		os.Exit(1)
	}
}

func run(arguments []string) error {
	if len(arguments) == 0 {
		return errors.New("usage: cardputer-agent pair|status|doctor|run")
	}
	if arguments[0] == "--version" || arguments[0] == "version" {
		fmt.Println("cardputer-agent " + version)
		return nil
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
		reader := bufio.NewReader(os.Stdin)
		if *deviceAddress == "" {
			fmt.Fprint(os.Stderr, "Device address: ")
			value, readErr := reader.ReadString('\n')
			if readErr != nil {
				return errors.New("read device address")
			}
			*deviceAddress = strings.TrimSpace(value)
		}
		fmt.Fprint(os.Stderr, "Device PIN: ")
		pin, err := readPIN(reader)
		fmt.Fprintln(os.Stderr)
		if err != nil {
			return errors.New("read device PIN")
		}
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
		if err := triggerScheduledTask(); err != nil {
			fmt.Println("Auto-start task will activate at the next sign-in.")
		}
		return nil
	case "status":
		flags := flag.NewFlagSet("status", flag.ContinueOnError)
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
		if _, err := client.PetStatus(ctx); err != nil {
			return err
		}
		fmt.Println("Cardputer Companion " + version)
		fmt.Println("CONFIG OK")
		fmt.Println("DEVICE OK")
		if scheduledTaskInstalled() {
			fmt.Println("AUTOSTART OK")
		} else {
			fmt.Println("AUTOSTART MISSING")
		}
		return nil
	case "doctor":
		flags := flag.NewFlagSet("doctor", flag.ContinueOnError)
		path := flags.String("config", configPath, "secure configuration path")
		if err := flags.Parse(arguments[1:]); err != nil {
			return err
		}
		fmt.Println("Cardputer Companion " + version)
		fmt.Println("DPAPI OK")
		if _, err := exec.LookPath("codex"); err == nil {
			fmt.Println("CODEX OK")
		} else {
			fmt.Println("CODEX MISSING")
		}
		stored, pin, loadErr := (config.Store{
			Path: *path, Protector: protector,
		}).Load()
		if loadErr != nil {
			fmt.Println("CONFIG MISSING")
		} else {
			fmt.Println("CONFIG OK")
			client, clientErr := device.NewClient(
				stored.DeviceURL,
				pin,
				stored.CertificateSHA256,
			)
			if clientErr == nil {
				ctx, cancel := context.WithTimeout(
					context.Background(),
					5*time.Second,
				)
				_, clientErr = client.PetStatus(ctx)
				cancel()
			}
			if clientErr == nil {
				fmt.Println("DEVICE OK")
			} else {
				fmt.Println("DEVICE UNREACHABLE")
			}
		}
		if scheduledTaskInstalled() {
			fmt.Println("AUTOSTART OK")
		} else {
			fmt.Println("AUTOSTART MISSING")
		}
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
		logFile, logErr := openAgentLog(*path)
		if logErr != nil {
			return logErr
		}
		defer logFile.Close()
		reporter := io.MultiWriter(os.Stderr, logFile)
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
				fmt.Fprintln(reporter, "sync warning:", stepErr)
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
		return errors.New("usage: cardputer-agent pair|status|doctor|run")
	}
}

func defaultConfigPath() (string, error) {
	root, err := os.UserConfigDir()
	if err != nil {
		return "", errors.New("locate user configuration directory")
	}
	return filepath.Join(root, "CardputerCodexCompanion", "config.json"), nil
}

func readPIN(reader *bufio.Reader) (string, error) {
	if term.IsTerminal(int(os.Stdin.Fd())) {
		value, err := term.ReadPassword(int(os.Stdin.Fd()))
		return strings.TrimSpace(string(value)), err
	}
	value, err := reader.ReadString('\n')
	return strings.TrimSpace(value), err
}

func openAgentLog(configPath string) (*os.File, error) {
	logDirectory := filepath.Join(filepath.Dir(configPath), "logs")
	if err := os.MkdirAll(logDirectory, 0o700); err != nil {
		return nil, errors.New("create agent log directory")
	}
	path := filepath.Join(logDirectory, "agent.log")
	if info, err := os.Stat(path); err == nil && info.Size() > 2*1024*1024 {
		previous := filepath.Join(logDirectory, "agent.log.1")
		_ = os.Remove(previous)
		if err := os.Rename(path, previous); err != nil {
			return nil, errors.New("rotate agent log")
		}
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o600)
	if err != nil {
		return nil, errors.New("open agent log")
	}
	return file, nil
}
