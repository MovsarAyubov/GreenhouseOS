# GreenhouseOS

GreenhouseOS is now centered on the Ubuntu Server `greenhouse-hub` runtime.
The hub owns the RS485 bus, polls Modbus RTU greenhouse slaves, exposes an
HTTP API for operator clients, and loads topology plus slave-map data from
JSON files.

## What Is Kept

- `greenhouse-hub/`: Go service, HTTP API, Modbus RTU transport, mock mode, and slave-map catalog.
- `topology/`: production sample topology and semantics files used by Docker and development runs.
- `tools/topology_designer/`: human-friendly profile compiler for topology JSON.
- `tools/topology/topology_packer.py`: topology binary/chunk packer for offline tooling.
- `Dockerfile.hub` and `docker-compose.yml`: container build and mock/RTU runtime profiles.

## Quick Start

Run the mock hub without RS485 hardware:

```bash
docker compose up --build greenhouse-hub-mock
```

Then open:

```text
http://localhost:8080/api/health
```

Run against a USB-RS485 adapter:

```bash
SERIAL_PORT=/dev/ttyUSB0 docker compose --profile rtu up --build greenhouse-hub-rtu
```

## Tests

```bash
cd greenhouse-hub
go test ./...
cd ..
python -m unittest discover -s tools/topology/tests -p "test_*.py"
python -m unittest discover -s tools/topology_designer/tests -p "test_*.py"
```

See `docs/` for deployment, Docker, topology, and slave-map details.
