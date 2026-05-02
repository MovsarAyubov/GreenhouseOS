# Docker Build Setup

This project is STM32CubeIDE/GNU Make based (not ESP-IDF), and can be built in Docker.

## Build firmware

```bash
docker compose run --rm firmware
```

Build artifacts are generated in:

- `Debug/greenhouseOS.elf`
- `Debug/greenhouseOS.map`
- `Debug/greenhouseOS.list`

## Clean

```bash
docker compose run --rm clean
```

## Build + Python quality tests

```bash
docker compose run --rm quality
```

## Why the wrapper script exists

`Debug/makefile` contains a generated absolute Windows linker-script path.  
Inside Linux containers this path is invalid, so the entrypoint normalizes it to:

- `../STM32F407VETX_FLASH.ld`

before running `make`.
