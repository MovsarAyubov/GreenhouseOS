# Ubuntu Deployment

## Files

Install the hub binary and configuration like this:

- `/opt/greenhouse-hub/greenhouse-hub`
- `/etc/greenhouse/topology.json`
- `/etc/greenhouse/semantics.json`
- `/etc/greenhouse/slave_maps/*.json`
- `/etc/systemd/system/greenhouse-hub.service`

## Build

```bash
cd greenhouse-hub
go mod tidy
go build -o greenhouse-hub ./cmd/greenhouse-hub
```

## Service

The service definition lives at `greenhouse-hub/deploy/greenhouse-hub.service`.
It starts the hub against `/dev/ttyUSB0`, loads topology and semantics from
`/etc/greenhouse`, and loads slave maps from `/etc/greenhouse/slave_maps`.

The service user needs access to the serial device:

```bash
sudo usermod -aG dialout greenhouse
```

## Health Check

```bash
curl http://localhost:8080/api/health
```
