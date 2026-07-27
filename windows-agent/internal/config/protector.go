package config

// Protector encrypts and decrypts device pairing material for the current user.
// Production Windows builds provide a DPAPI-backed implementation.
type Protector interface {
	Protect(plaintext []byte) ([]byte, error)
	Unprotect(ciphertext []byte) ([]byte, error)
}
