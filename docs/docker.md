# Docker

Docker is used for the Ubuntu Server `greenhouse-hub` runtime.

## Mock Mode

Mock mode does not need RS485 hardware:

```bash
docker compose up --build greenhouse-hub-mock
```

The hub listens on `${HUB_PORT:-8080}` and loads sample files from `/app/topology`
and `/app/slave_maps` inside the container.

## RTU Mode

RTU mode passes a host USB-RS485 device into the container:

```bash
SERIAL_PORT=/dev/ttyUSB0 BAUD=115200 docker compose --profile rtu up --build greenhouse-hub-rtu
```

The Linux user running Docker must be allowed to access the serial device.

The RTU service also adds Linux group id `${SERIAL_GROUP_ID:-20}` inside the
container. On Ubuntu, `20` is normally the `dialout` group. If your serial
device uses another group id, check it with:

```bash
stat -c '%g %G %a %n' /dev/ttyUSB0
```

Then run with the matching id:

```bash
SERIAL_GROUP_ID=20 SERIAL_PORT=/dev/ttyUSB0 BAUD=19200 docker compose --profile rtu up --build greenhouse-hub-rtu
```

## Image Build

```bash
docker build -f Dockerfile.hub .
```
