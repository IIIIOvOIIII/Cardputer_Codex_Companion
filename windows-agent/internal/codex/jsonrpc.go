package codex

import (
	"context"
)

type RPCClient interface {
	Start(context.Context) error
	Request(context.Context, string, map[string]any) (map[string]any, error)
	RespondToPendingApproval(context.Context, bool) error
	Close() error
}
