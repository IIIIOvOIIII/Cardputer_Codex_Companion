package pet

import (
	"encoding/json"
	"errors"
	"fmt"
	"image"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	_ "golang.org/x/image/webp"
)

const (
	AtlasColumns = 8
	CellWidth    = 192
	CellHeight   = 208
)

type AtlasVersion uint8

const (
	AtlasV1 AtlasVersion = 1
	AtlasV2 AtlasVersion = 2
)

type Source struct {
	ID        string
	AtlasPath string
	Version   AtlasVersion
}

type SelectionReader struct {
	Environment map[string]string
	Dimensions  func(string) (int, int, error)
}

type customManifest struct {
	ID                  string `json:"id"`
	SpriteVersionNumber uint8  `json:"spriteVersionNumber"`
	SpritesheetPath     string `json:"spritesheetPath"`
}

func (reader SelectionReader) SelectedSource() (Source, error) {
	home, err := reader.codexHome()
	if err != nil {
		return Source{}, err
	}
	selected, err := selectedID(filepath.Join(home, "config.toml"))
	if err != nil {
		return Source{}, err
	}
	if source, found := reader.officialSource(home, selected); found {
		return source, nil
	}
	return reader.customSource(home, selected)
}

func (reader SelectionReader) codexHome() (string, error) {
	if value := reader.environmentValue("CODEX_HOME"); value != "" {
		return filepath.Clean(value), nil
	}
	if value := reader.environmentValue("HOME"); value != "" {
		return filepath.Join(value, ".codex"), nil
	}
	if value := reader.environmentValue("USERPROFILE"); value != "" {
		return filepath.Join(value, ".codex"), nil
	}
	if reader.Environment == nil {
		home, err := os.UserHomeDir()
		if err == nil && home != "" {
			return filepath.Join(home, ".codex"), nil
		}
	}
	return "", errors.New("Codex home is unavailable")
}

func (reader SelectionReader) environmentValue(key string) string {
	if reader.Environment != nil {
		return reader.Environment[key]
	}
	return os.Getenv(key)
}

func selectedID(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", errors.New("pet selection configuration is missing")
	}
	inTUI := false
	for _, raw := range strings.Split(string(data), "\n") {
		line := strings.TrimSpace(stripTOMLComment(raw))
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			inTUI = line == "[tui]"
			continue
		}
		if !inTUI {
			continue
		}
		key, value, found := strings.Cut(line, "=")
		if !found || strings.TrimSpace(key) != "pet" {
			continue
		}
		value = strings.TrimSpace(value)
		if len(value) < 2 ||
			(value[0] != '"' && value[0] != '\'') ||
			value[len(value)-1] != value[0] {
			return "", errors.New("pet ID is invalid")
		}
		id := value[1 : len(value)-1]
		if !validPetID(id) {
			return "", errors.New("pet ID is invalid")
		}
		return id, nil
	}
	return "", errors.New("pet selection is missing")
}

func stripTOMLComment(line string) string {
	var quote byte
	escaped := false
	for index := 0; index < len(line); index++ {
		character := line[index]
		if escaped {
			escaped = false
			continue
		}
		if character == '\\' && quote == '"' {
			escaped = true
			continue
		}
		if character == '"' || character == '\'' {
			if quote == character {
				quote = 0
			} else if quote == 0 {
				quote = character
			}
			continue
		}
		if character == '#' && quote == 0 {
			return line[:index]
		}
	}
	return line
}

func validPetID(id string) bool {
	return id != "" && len([]byte(id)) <= 64 && id != "." && id != ".." &&
		!strings.ContainsAny(id, `/\`)
}

func (reader SelectionReader) officialSource(
	home string,
	id string,
) (Source, bool) {
	directory := filepath.Join(home, "cache", "tui-pets", "v1", "assets")
	entries, err := os.ReadDir(directory)
	if err != nil {
		return Source{}, false
	}
	pattern := regexp.MustCompile(
		"^" + regexp.QuoteMeta(id) + `-spritesheet-v([0-9]+)\.webp$`,
	)
	type candidate struct {
		version int
		path    string
	}
	var candidates []candidate
	for _, entry := range entries {
		match := pattern.FindStringSubmatch(entry.Name())
		if len(match) != 2 {
			continue
		}
		version, err := strconv.Atoi(match[1])
		if err == nil {
			candidates = append(candidates, candidate{
				version: version,
				path:    filepath.Join(directory, entry.Name()),
			})
		}
	}
	sort.Slice(candidates, func(left int, right int) bool {
		return candidates[left].version > candidates[right].version
	})
	for _, candidate := range candidates {
		width, height, err := reader.readDimensions(candidate.path)
		if err != nil {
			continue
		}
		if version, ok := atlasVersion(width, height); ok {
			return Source{ID: id, AtlasPath: candidate.path, Version: version}, true
		}
	}
	return Source{}, false
}

func (reader SelectionReader) customSource(home string, id string) (Source, error) {
	petsDirectory, err := filepath.Abs(filepath.Join(home, "pets"))
	if err != nil {
		return Source{}, errors.New("custom pet path is invalid")
	}
	petDirectory, err := filepath.Abs(filepath.Join(petsDirectory, id))
	if err != nil || !withinDirectory(petDirectory, petsDirectory) {
		return Source{}, errors.New("custom pet path traversal rejected")
	}
	manifestData, err := os.ReadFile(filepath.Join(petDirectory, "pet.json"))
	if err != nil {
		return Source{}, errors.New("custom pet source was not found")
	}
	var manifest customManifest
	if json.Unmarshal(manifestData, &manifest) != nil || manifest.ID != id ||
		manifest.SpritesheetPath == "" {
		return Source{}, errors.New("custom pet manifest is invalid")
	}
	version := AtlasV1
	switch manifest.SpriteVersionNumber {
	case 0, 1:
		version = AtlasV1
	case 2:
		version = AtlasV2
	default:
		return Source{}, errors.New("custom pet manifest is invalid")
	}
	atlasPath, err := filepath.Abs(
		filepath.Join(petDirectory, manifest.SpritesheetPath),
	)
	if err != nil || !withinDirectory(atlasPath, petDirectory) {
		return Source{}, errors.New("custom pet path traversal rejected")
	}
	resolvedPet, petErr := filepath.EvalSymlinks(petDirectory)
	resolvedAtlas, atlasErr := filepath.EvalSymlinks(atlasPath)
	if petErr != nil || atlasErr != nil ||
		!withinDirectory(resolvedAtlas, resolvedPet) {
		return Source{}, errors.New("custom pet path traversal rejected")
	}
	width, height, err := reader.readDimensions(resolvedAtlas)
	if err != nil {
		return Source{}, errors.New("custom pet atlas is invalid")
	}
	detected, ok := atlasVersion(width, height)
	if !ok || detected != version {
		return Source{}, errors.New("custom pet atlas is invalid")
	}
	return Source{ID: id, AtlasPath: resolvedAtlas, Version: version}, nil
}

func (reader SelectionReader) readDimensions(path string) (int, int, error) {
	if reader.Dimensions != nil {
		return reader.Dimensions(path)
	}
	file, err := os.Open(path)
	if err != nil {
		return 0, 0, err
	}
	defer file.Close()
	configuration, _, err := image.DecodeConfig(file)
	if err != nil {
		return 0, 0, err
	}
	return configuration.Width, configuration.Height, nil
}

func atlasVersion(width int, height int) (AtlasVersion, bool) {
	switch {
	case width == 1536 && height == 1872:
		return AtlasV1, true
	case width == 1536 && height == 2288:
		return AtlasV2, true
	default:
		return 0, false
	}
}

func expectedAtlasDimensions(version AtlasVersion) (int, int, error) {
	switch version {
	case AtlasV1:
		return 1536, 1872, nil
	case AtlasV2:
		return 1536, 2288, nil
	default:
		return 0, 0, fmt.Errorf("unsupported pet atlas version %d", version)
	}
}

func withinDirectory(candidate string, directory string) bool {
	relative, err := filepath.Rel(directory, candidate)
	return err == nil && relative != "." && relative != ".." &&
		!strings.HasPrefix(relative, ".."+string(filepath.Separator)) &&
		!filepath.IsAbs(relative)
}
