import socket
import struct
import sys
import threading
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.topology import topology_uploader as uploader  # noqa: E402


def _recv_exact(conn: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError("socket closed while reading request")
        data.extend(chunk)
    return bytes(data)


def _send_response(conn: socket.socket, tx_id: int, unit_id: int, pdu: bytes) -> None:
    mbap = struct.pack(">HHHB", tx_id & 0xFFFF, 0, len(pdu) + 1, unit_id & 0xFF)
    conn.sendall(mbap + pdu)


class _OneClientServer:
    def __init__(self, handler) -> None:
        self._handler = handler
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.bind(("127.0.0.1", 0))
        self._srv.listen(1)
        self._srv.settimeout(2.0)
        self.port = self._srv.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._error = None

    def _serve(self) -> None:
        try:
            conn, _ = self._srv.accept()
            conn.settimeout(2.0)
            with conn:
                self._handler(conn)
        except Exception as exc:  # noqa: BLE001
            self._error = exc
        finally:
            try:
                self._srv.close()
            except Exception:  # noqa: BLE001
                pass

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self._thread.join(timeout=3.0)
        if (exc_type is None) and (self._error is not None):
            raise self._error


class ModbusTcpClientTests(unittest.TestCase):
    def test_fc3_fc6_fc16_roundtrip(self) -> None:
        def handler(conn: socket.socket) -> None:
            for _ in range(3):
                hdr = _recv_exact(conn, 7)
                tx_id, proto_id, length, unit_id = struct.unpack(">HHHB", hdr)
                self.assertEqual(0, proto_id)
                pdu = _recv_exact(conn, length - 1)
                fn = pdu[0]
                if fn == 0x03:
                    _, addr, qty = struct.unpack(">BHH", pdu)
                    self.assertEqual(50, addr)
                    self.assertEqual(3, qty)
                    regs = [0x1111, 0x2222, 0x3333]
                    payload = struct.pack(">BB", 0x03, qty * 2) + b"".join(struct.pack(">H", r) for r in regs)
                    _send_response(conn, tx_id, unit_id, payload)
                elif fn == 0x06:
                    _, addr, value = struct.unpack(">BHH", pdu)
                    self.assertEqual(77, addr)
                    self.assertEqual(0xABCD, value)
                    _send_response(conn, tx_id, unit_id, pdu)
                elif fn == 0x10:
                    _, addr, qty, byte_count = struct.unpack(">BHHB", pdu[:6])
                    self.assertEqual(100, addr)
                    self.assertEqual(2, qty)
                    self.assertEqual(4, byte_count)
                    payload = struct.pack(">BHH", 0x10, addr, qty)
                    _send_response(conn, tx_id, unit_id, payload)
                else:
                    raise AssertionError(f"unexpected function code {fn}")

        with _OneClientServer(handler) as srv:
            with uploader.ModbusTcpClient("127.0.0.1", srv.port, 1, 0.3) as client:
                regs = client.read_holding_registers(50, 3)
                self.assertEqual([0x1111, 0x2222, 0x3333], regs)
                client.write_single_register(77, 0xABCD)
                client.write_multiple_registers(100, [0x0102, 0x0304])

    def test_rejects_tx_id_mismatch(self) -> None:
        def handler(conn: socket.socket) -> None:
            hdr = _recv_exact(conn, 7)
            tx_id, _proto_id, length, unit_id = struct.unpack(">HHHB", hdr)
            pdu = _recv_exact(conn, length - 1)
            self.assertEqual(0x03, pdu[0])
            payload = struct.pack(">BBH", 0x03, 2, 0x1234)
            _send_response(conn, tx_id + 1, unit_id, payload)

        with _OneClientServer(handler) as srv:
            with uploader.ModbusTcpClient("127.0.0.1", srv.port, 1, 0.3) as client:
                with self.assertRaises(uploader.ModbusTcpError):
                    client.read_holding_registers(10, 1)

    def test_rejects_peer_close_mid_response(self) -> None:
        def handler(conn: socket.socket) -> None:
            _ = _recv_exact(conn, 7)
            conn.close()

        with _OneClientServer(handler) as srv:
            with uploader.ModbusTcpClient("127.0.0.1", srv.port, 1, 0.3) as client:
                with self.assertRaises((uploader.ModbusTcpError, ConnectionResetError, OSError)):
                    client.read_holding_registers(20, 1)


if __name__ == "__main__":
    unittest.main()
