package testutil

import (
	"os"
	"path/filepath"
	"runtime"
)

type testingT interface {
	Helper()
	Fatalf(string, ...any)
}

func ProductFixture(t testingT, name string) []byte {
	t.Helper()
	_, current, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatalf("cannot locate fixture helper")
	}
	path := filepath.Join(
		filepath.Dir(current), "..", "..", "..",
		"protocol", "product-v1", "fixtures", name,
	)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read product fixture: %v", err)
	}
	return data
}
