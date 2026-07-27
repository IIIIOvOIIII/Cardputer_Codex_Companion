//go:build !windows

package config

import "errors"

func NewPlatformProtector() (Protector, error) {
	return nil, errors.New("secure platform storage requires Windows DPAPI")
}
