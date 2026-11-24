# Harness Studio

Harness Studio is a lightweight launcher for the locally built Blender runtime. It starts Blender with the OpenGL backend by default and supports the standard Blender command-line options.

## Running

From this directory:

```sh
./run.sh
```

The included `harnessstudio` executable is built for Linux x86_64. To rebuild it from source, use Go 1.17 or newer:

```sh
go build -o harnessstudio .
```

For a source-level credential check:

```sh
./harnessstudio --security-audit
```

## Publishing

The prepared distribution is published from the `main` branch. After configuring
the repository's Git credentials, run:

```sh
./push.sh
```

The script verifies the branch, remote, and clean working tree before pushing.

## Distribution package

The `packages/` directory contains a ready-to-run Linux archive with the launcher and its supporting files.
