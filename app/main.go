// Command harnessstudio starts Harness Studio: Blender (built from ../blender
// with the Qt 6 GHOST back-end and the Harness Studio shell) from
// ../build-blender.
package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
)

func main() {
	here, err := os.Getwd()
	if err != nil {
		fail(err)
	}
	if hasFlag(os.Args[1:], "--security-audit") {
		sourceDir := here
		if _, err := os.Stat(filepath.Join(sourceDir, "main.go")); err != nil {
			sourceDir = filepath.Join(here, "app")
		}
		if err := auditSources(sourceDir); err != nil {
			fail(err)
		}
		return
	}
	root := here
	if _, err := os.Stat(filepath.Join(root, "build-blender", "bin", "blender")); err != nil {
		root = filepath.Dir(here)
	}
	blender := filepath.Join(root, "build-blender", "bin", "blender")
	if _, err := os.Stat(blender); err != nil {
		fmt.Fprintf(os.Stderr, "Harness Studio is not built yet: run `make` in %s\n", root)
		os.Exit(1)
	}

	args := []string{}
	env := os.Environ()
	in := os.Args[1:]
	for i := 0; i < len(in); i++ {
		if in[i] == "--screenshot" && i+1 < len(in) {
			path, _ := filepath.Abs(in[i+1])
			env = append(env, "HARNESS_SCREENSHOT="+path)
			args = append([]string{"--factory-startup"}, args...)
			i++
			continue
		}
		args = append(args, in[i])
	}

	hasBackend := false
	for _, a := range args {
		if a == "--gpu-backend" {
			hasBackend = true
		}
	}
	if !hasBackend {
		args = append([]string{"--gpu-backend", "opengl"}, args...)
	}

	argv := append([]string{blender}, args...)
	if err := syscall.Exec(blender, argv, env); err != nil {
		cmd := exec.Command(blender, args...)
		cmd.Env = env
		cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			fail(err)
		}
	}
}

func hasFlag(args []string, want string) bool {
	for _, arg := range args {
		if arg == want {
			return true
		}
	}
	return false
}

// auditSources checks only for credential-shaped literals. It deliberately
// reports locations, never the matched value, so an audit cannot leak a key.
func auditSources(dir string) error {
	assignment := regexp.MustCompile(`(?i)(api[_-]?key|client[_-]?secret|password|token|secret)\s*[:=]\s*["'][^"']{8,}["']`)
	privateKey := regexp.MustCompile(`-----BEGIN [A-Z ]*PRIVATE KEY-----`)
	var findings []string

	entries, err := os.ReadDir(dir)
	if err != nil {
		return err
	}
	for _, entry := range entries {
		if entry.IsDir() || !isSourceFile(entry.Name()) {
			continue
		}
		path := filepath.Join(dir, entry.Name())
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		for lineNumber, line := range strings.Split(string(data), "\n") {
			if assignment.MatchString(line) || privateKey.MatchString(line) {
				findings = append(findings, fmt.Sprintf("%s:%d", entry.Name(), lineNumber+1))
			}
		}
	}
	if len(findings) > 0 {
		fmt.Fprintln(os.Stderr, "Potential embedded credential literals found at:")
		for _, finding := range findings {
			fmt.Fprintln(os.Stderr, "-", finding)
		}
		return fmt.Errorf("security audit failed")
	}
	fmt.Println("Security audit passed: no embedded API keys or private keys found in app source.")
	return nil
}

func isSourceFile(name string) bool {
	for _, suffix := range []string{".go", ".py", ".sh"} {
		if strings.HasSuffix(name, suffix) {
			return true
		}
	}
	return false
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
