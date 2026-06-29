# Project Overview

GreenhouseOS is an Ubuntu Server hub plus topology tooling project.

## Runtime

`greenhouse-hub` is the production runtime. It:

- owns the RS485 bus;
- polls Modbus RTU slaves;
- keeps the latest greenhouse state in memory;
- routes operator setpoints to slaves through the slave-map catalog;
- exposes an HTTP API for clients and SCADA adapters.

The hub loads:

- topology JSON from `topology/*_topology.json`;
- semantics JSON from `topology/*_semantics.json`;
- slave maps from `greenhouse-hub/slave_maps/`.

## Tooling

- `tools/topology_designer/cli.py` validates and compiles human-friendly greenhouse profiles.
- `tools/topology/topology_packer.py` packs topology JSON into a deterministic binary/chunk artifact.
- Docker support is in `Dockerfile.hub` and `docker-compose.yml`.

## Removed Legacy Scope

The STM32 master firmware, CubeIDE project files, firmware Docker image, Modbus TCP topology uploader, and firmware quality gate are no longer part of the active project.
