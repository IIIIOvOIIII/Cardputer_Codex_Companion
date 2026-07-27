package device

import (
	"errors"
	"net/url"
	"strings"
)

func SelectAddress(manual string, discovered []string) (string, error) {
	if manual != "" {
		return normalizeAddress(manual)
	}
	for _, candidate := range discovered {
		address, err := normalizeAddress(candidate)
		if err == nil {
			return address, nil
		}
	}
	return "", errors.New("no Cardputer device was discovered")
}

func normalizeAddress(address string) (string, error) {
	parsed, err := url.Parse(address)
	if err != nil || parsed.Scheme != "https" || parsed.Host == "" ||
		parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" ||
		(parsed.Path != "" && parsed.Path != "/") {
		return "", errors.New("device address must be an HTTPS origin")
	}
	return strings.TrimRight(address, "/"), nil
}
