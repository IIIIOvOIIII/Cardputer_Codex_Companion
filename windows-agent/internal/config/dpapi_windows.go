//go:build windows

package config

import (
	"errors"
	"fmt"
	"runtime"
	"syscall"
	"unsafe"
)

const cryptProtectUIForbidden = 0x1

type dataBlob struct {
	size uint32
	data *byte
}

type dpapiProtector struct{}

var (
	crypt32DLL         = syscall.NewLazyDLL("crypt32.dll")
	cryptProtectData   = crypt32DLL.NewProc("CryptProtectData")
	cryptUnprotectData = crypt32DLL.NewProc("CryptUnprotectData")
	localFree          = kernel32DLL.NewProc("LocalFree")
)

func NewPlatformProtector() (Protector, error) {
	if err := cryptProtectData.Find(); err != nil {
		return nil, errors.New("Windows DPAPI is unavailable")
	}
	if err := cryptUnprotectData.Find(); err != nil {
		return nil, errors.New("Windows DPAPI is unavailable")
	}
	return dpapiProtector{}, nil
}

func (dpapiProtector) Protect(plaintext []byte) ([]byte, error) {
	return callDPAPI(cryptProtectData, plaintext, "protect")
}

func (dpapiProtector) Unprotect(ciphertext []byte) ([]byte, error) {
	return callDPAPI(cryptUnprotectData, ciphertext, "unprotect")
}

func callDPAPI(procedure *syscall.LazyProc, input []byte, operation string) ([]byte, error) {
	var inputBlob dataBlob
	if len(input) != 0 {
		inputBlob.size = uint32(len(input))
		inputBlob.data = &input[0]
	}
	var outputBlob dataBlob
	result, _, callErr := procedure.Call(
		uintptr(unsafe.Pointer(&inputBlob)),
		0,
		0,
		0,
		0,
		cryptProtectUIForbidden,
		uintptr(unsafe.Pointer(&outputBlob)),
	)
	runtime.KeepAlive(input)
	if result == 0 {
		if callErr == syscall.Errno(0) {
			callErr = errors.New("unknown Windows error")
		}
		return nil, fmt.Errorf("DPAPI %s failed: %w", operation, callErr)
	}
	if outputBlob.data == nil || outputBlob.size == 0 {
		return []byte{}, nil
	}
	defer localFree.Call(uintptr(unsafe.Pointer(outputBlob.data)))
	output := make([]byte, int(outputBlob.size))
	copy(output, unsafe.Slice(outputBlob.data, int(outputBlob.size)))
	return output, nil
}
