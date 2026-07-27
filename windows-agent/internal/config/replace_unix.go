//go:build !windows

package config

import "os"

func replaceFile(source string, destination string) error {
	return os.Rename(source, destination)
}
