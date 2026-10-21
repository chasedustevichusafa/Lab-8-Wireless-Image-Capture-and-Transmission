"""Cross-language tests: compile the firmware service and drive it with the GCS.

Run from anywhere: python test/test_protocol.py [--gcs /path/to/KestrelSAT-GCS]
Requires a C++17 host compiler, POSIX PTYs, and the GCS Python requirements.
"""
import argparse
from pathlib import Path
import os
import pty
import subprocess
import sys
import tempfile
import threading
import tty
import unittest

PROJECT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--gcs', type=Path, default=PROJECT.parents[2] / 'KestrelSAT-GCS')
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(args.gcs))
import serial
from kestrelsat.remote_files import Client, Cancelled, RemoteError, encode, decode


class FirmwareProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.build.name) / 'file-service'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I', str(PROJECT / 'include'), str(PROJECT / 'test/host_service.cpp'),
                        '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sd = self.root / 'sd'
        self.sd.mkdir()
        (self.sd / 'images').mkdir()
        (self.sd / 'images' / 'photo.jpg').write_bytes(bytes(range(256))*5)
        (self.sd / 'empty.txt').write_bytes(b'')
        self.payload = bytes(range(256))*256 + b'\x00+++\xff\r\n'
        (self.sd / 'binary.bin').write_bytes(self.payload)

    def client(self, fault=''):
        master, slave = pty.openpty()
        tty.setraw(slave)
        port = serial.Serial(os.ttyname(slave), 9600, timeout=0.03, write_timeout=0.2)
        process = subprocess.Popen([str(self.exe), str(self.sd), fault],
                                   stdin=master, stdout=master, stderr=subprocess.PIPE, close_fds=True)
        os.close(master)
        os.close(slave)
        self.addCleanup(process.stderr.close)
        def stop():
            port.close()
            process.terminate()
            process.wait(timeout=2)
        self.addCleanup(stop)
        return Client(port, timeout=0.12, attempts=4)

    def test_nested_listing_and_binary_and_empty_downloads(self):
        client = self.client()
        entries = client.list_directory('/')
        self.assertEqual({e.name for e in entries}, {'images', 'empty.txt', 'binary.bin'})
        self.assertTrue(next(e for e in entries if e.name == 'images').directory)
        self.assertEqual(client.list_directory('/images')[0].name, 'photo.jpg')
        for remote, expected in [('/binary.bin', self.payload), ('/empty.txt', b''),
                                 ('/images/photo.jpg', bytes(range(256))*5)]:
            destination = self.root / 'result'
            client.download(remote, destination)
            self.assertEqual(destination.read_bytes(), expected)
        self.assertFalse(list(self.root.glob('*.part')))

    def test_retry_dropped_corrupt_truncated_and_duplicate_frames(self):
        for fault in ('drop', 'corrupt', 'truncate', 'duplicate'):
            with self.subTest(fault=fault):
                client = self.client(fault)
                destination = self.root / fault
                client.download('/binary.bin', destination)
                self.assertEqual(destination.read_bytes(), self.payload)

    def test_transient_sd_reads_recover_without_skipping_bytes(self):
        for fault in ('read-short', 'read-zero', 'read-error', 'read-scattered'):
            with self.subTest(fault=fault):
                client = self.client(fault)
                destination = self.root / fault
                client.download('/binary.bin', destination)
                self.assertEqual(destination.read_bytes(), self.payload)

    def test_twenty_transfers_with_scattered_transient_read_failures(self):
        client = self.client('read-scattered')
        destination = self.root / 'stress.bin'
        for _ in range(20):
            client.download('/binary.bin', destination)
            self.assertEqual(destination.read_bytes(), self.payload)

    def test_expired_session_fails_cleanly_without_saving_partial_file(self):
        client = self.client('expire')
        destination = self.root / 'expired.bin'
        with self.assertRaisesRegex(RemoteError, 'NO_SESSION'):
            client.download('/binary.bin', destination)
        self.assertFalse(destination.exists())
        self.assertFalse(list(self.root.glob('*.part')))
        self.assertEqual(len(client.list_directory('/')), 3)

    def test_permanent_sd_failure_is_bounded_and_preserves_existing_file(self):
        client = self.client('read-permanent')
        destination = self.root / 'result'
        destination.write_bytes(b'original')
        with self.assertRaisesRegex(RemoteError, 'READ_FAILED.*4096.*128.*-1.*3'):
            client.download('/binary.bin', destination)
        self.assertEqual(destination.read_bytes(), b'original')
        self.assertFalse(list(self.root.glob('*.part')))
        self.assertEqual(len(client.list_directory('/')), 3)

    def test_checksum_failure_preserves_existing_file(self):
        client = self.client('bad-final-crc')
        destination = self.root / 'result'
        destination.write_bytes(b'original')
        with self.assertRaisesRegex(RemoteError, 'checksum'):
            client.download('/binary.bin', destination)
        self.assertEqual(destination.read_bytes(), b'original')
        self.assertFalse(list(self.root.glob('*.part')))

    def test_cancel_removes_partial_and_allows_next_operation(self):
        client = self.client()
        def cancel(**info):
            if info.get('done', 0) >= 128:
                client.cancel.set()
        client.progress = cancel
        with self.assertRaises(Cancelled):
            client.download('/binary.bin', self.root / 'cancelled')
        self.assertFalse((self.root / 'cancelled').exists())
        self.assertFalse(list(self.root.glob('*.part')))
        client.cancel.clear()
        client.progress = lambda **info: None
        self.assertEqual(len(client.list_directory('/')), 3)

    def test_disconnect_does_not_publish_partial_file(self):
        client = self.client('disconnect')
        with self.assertRaises((RemoteError, serial.SerialException)):
            client.download('/binary.bin', self.root / 'disconnected')
        self.assertFalse((self.root / 'disconnected').exists())
        self.assertFalse(list(self.root.glob('*.part')))

    def test_missing_paths_and_malformed_commands(self):
        client = self.client()
        for path in ('/missing', '/binary.bin'):
            with self.assertRaisesRegex(RemoteError, 'DIRECTORY_UNAVAILABLE'):
                client.list_directory(path)
        with self.assertRaisesRegex(RemoteError, 'FILE_UNAVAILABLE'):
            client.download('/images', self.root / 'bad')
        with self.assertRaisesRegex(RemoteError, 'NO_SESSION'):
            client.request('READ', '00000000', 0)
        with self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
            client.request('OPEN', '/../../outside'.encode().hex())
        # Damage a command; no action/response. A valid request must still work.
        client.port.write(encode(17, 'OPEN', '/binary.bin'.encode().hex())[:-3] + b'00\n')
        client.hello()
        self.assertIsNone(decode(b'@KFS1|bad|frame\n'))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
