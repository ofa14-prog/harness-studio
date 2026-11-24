package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
)

func main() {
	root, err := os.Getwd()
	if err != nil {
		fail(err)
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
	for _, arg := range args {
		if arg == "--gpu-backend" {
			hasBackend = true
			break
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

func fail(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
