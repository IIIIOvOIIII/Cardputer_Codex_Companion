package codex

import "encoding/json"

func snapshotContent(snapshot Snapshot) (string, error) {
	data, err := json.Marshal(snapshot)
	return string(data), err
}
