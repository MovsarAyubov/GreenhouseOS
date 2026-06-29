package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"greenhouse-hub/internal/api"
	"greenhouse-hub/internal/config"
	"greenhouse-hub/internal/hub"
	"greenhouse-hub/internal/modbusrtu"
	"greenhouse-hub/internal/slavemap"
)

func main() {
	var (
		topologyPath  = flag.String("topology", "../topology/two_zones_one_weather_all_points_schedule_topology.json", "path to topology_config_v2 JSON")
		semanticsPath = flag.String("semantics", "../topology/two_zones_one_weather_all_points_schedule_semantics.json", "path to scada_semantic_mapping_v1 JSON")
		slaveMapsDir  = flag.String("slave-maps", "slave_maps", "path to slave map catalog directory")
		listenAddr    = flag.String("listen", ":8080", "HTTP listen address")
		serialDevice  = flag.String("serial", "/dev/ttyUSB0", "RS485 serial device")
		baudRate      = flag.Int("baud", 115200, "RS485 baud rate")
		timeout       = flag.Duration("timeout", 300*time.Millisecond, "Modbus request timeout")
		mock          = flag.Bool("mock", false, "run with in-memory Modbus transport")
		scanFrom      = flag.Int("scan-from", 1, "first Modbus slave address for autoscan")
		scanTo        = flag.Int("scan-to", 40, "last Modbus slave address for autoscan")
	)
	flag.Parse()

	cfg, err := config.Load(*topologyPath, *semanticsPath)
	if err != nil {
		log.Fatalf("load config: %v", err)
	}
	maps, err := slavemap.LoadCatalog(*slaveMapsDir)
	if err != nil {
		log.Fatalf("load slave maps: %v", err)
	}
	log.Printf("loaded %d slave map(s) from %s", len(maps.List()), *slaveMapsDir)

	var transport modbusrtu.Transport
	if *mock {
		transport = modbusrtu.NewMockTransport(cfg.Topology)
		log.Printf("using mock Modbus transport")
	} else {
		transport = modbusrtu.NewRTUTransport(modbusrtu.RTUConfig{
			Device:  *serialDevice,
			Baud:    *baudRate,
			Timeout: *timeout,
		})
		log.Printf("using Modbus RTU transport device=%s baud=%d", *serialDevice, *baudRate)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	service := hub.NewService(cfg, maps, transport, hub.Options{
		ScanFrom: uint8(*scanFrom),
		ScanTo:   uint8(*scanTo),
	})
	if err := service.Start(ctx); err != nil {
		log.Fatalf("start hub: %v", err)
	}
	defer service.Close()

	server := &http.Server{
		Addr:              *listenAddr,
		Handler:           api.NewHandler(service),
		ReadHeaderTimeout: 5 * time.Second,
	}

	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := server.Shutdown(shutdownCtx); err != nil {
			log.Printf("HTTP shutdown: %v", err)
		}
	}()

	log.Printf("greenhouse hub listening on %s", *listenAddr)
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("HTTP server: %v", err)
	}
}
