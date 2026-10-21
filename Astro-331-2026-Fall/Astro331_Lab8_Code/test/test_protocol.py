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
import zlib

PROJECT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--gcs', type=Path, default=PROJECT.parents[2] / 'KestrelSAT-GCS')
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(args.gcs))
import serial
from kestrelsat.remote_files import Client, Cancelled, RemoteError, UploadUncertain, encode, decode


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

    def test_cancel_during_verify_and_after_commit_are_distinguished(self):
        source = self.root/'source'
        source.write_bytes(self.payload)
        client = self.client()
        def cancel_verify(**info):
            if 'Verifying SD' in info.get('status', ''):
                client.cancel.set()
        client.progress = cancel_verify
        with self.assertRaises(Cancelled):
            client.upload(source, '/cancel-verify.bin')
        self.assertFalse((self.sd/'cancel-verify.bin').exists())
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))
        client.cancel.clear(); client.progress = lambda **info: None
        write = client.port.write
        def cancel_commit(data):
            parsed = decode(data.strip())
            sent = write(data)
            if parsed and parsed[1][0] == 'COMMIT':
                client.cancel.set()
            return sent
        client.port.write = cancel_commit
        # A published, verified upload is complete even if Cancel races its ACK.
        self.assertEqual(client.upload(source, '/committed.bin'), '/committed.bin')
        self.assertEqual((self.sd/'committed.bin').read_bytes(), self.payload)

    def test_upload_duplicates_and_target_created_before_commit(self):
        client = self.client()
        payload = self.payload[:256]
        target = '/idempotent.bin'
        ready = client.request('PUT', target.encode().hex(), len(payload), f'{zlib.crc32(payload):08x}')
        token = ready[1]
        self.assertEqual(client.request('WRITE', token, 0, payload[:128].hex()), ['ACK', '128', '256'])
        client.request('STATUS', token)
        self.assertEqual(client.request('WRITE', token, 0, payload[:128].hex()), ['ACK', '128', '256'])
        with self.assertRaisesRegex(RemoteError, 'BAD_OFFSET'):
            client.request('WRITE', token, 0, (b'x'*128).hex())
        client.request('WRITE', token, 128, payload[128:].hex())
        self.assertEqual(client.request('VERIFY', token, 0), ['CHECKED', '256', '256'])
        client.request('STATUS', token)
        self.assertEqual(client.request('VERIFY', token, 0), ['CHECKED', '256', '256'])
        (self.sd/'idempotent.bin').write_bytes(b'created concurrently')
        with self.assertRaisesRegex(RemoteError, 'COMMIT_FAILED'):
            client.request('COMMIT', token)
        self.assertEqual((self.sd/'idempotent.bin').read_bytes(), b'created concurrently')
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_twenty_upload_download_roundtrips(self):
        client = self.client('write-short')
        source, downloaded = self.root/'source', self.root/'downloaded'
        payload = self.payload[:14339]
        source.write_bytes(payload)
        for index in range(20):
            target = f'/images/stress-{index}.jpg'
            client.upload(source, target)
            client.download(target, downloaded)
            self.assertEqual(downloaded.read_bytes(), payload)
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_raw_upload_path_and_checksum_validation(self):
        client = self.client()
        for target in ('/.KFS-12345678.PART', '/bad?.jpg', '/bad:thing', '/bad. '):
            with self.subTest(target=target), self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
                client.request('PUT', target.encode().hex(), 0, '00000000')
        ready = client.request('PUT', '/bad-crc.bin'.encode().hex(), 128, '00000000')
        client.request('WRITE', ready[1], 0, self.payload[:128].hex())
        with self.assertRaisesRegex(RemoteError, 'SIZE_OR_CRC_MISMATCH'):
            client.request('VERIFY', ready[1], 0)
        self.assertFalse((self.sd/'bad-crc.bin').exists())
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_upload_recovers_lost_ack_and_commit_replies(self):
        source = self.root/'source'
        source.write_bytes(self.payload)
        for fault in ('upload-drop-ack', 'upload-corrupt-ack', 'upload-duplicate-write', 'upload-drop-commit'):
            with self.subTest(fault=fault):
                client = self.client(fault)
                client.upload(source, '/'+fault+'.bin')
                self.assertEqual((self.sd/(fault+'.bin')).read_bytes(), self.payload)
                self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_lost_ready_can_abort_and_lost_terminal_is_explicitly_uncertain(self):
        source = self.root/'source'
        source.write_bytes(self.payload)
        client = self.client('upload-drop-ready')
        with self.assertRaisesRegex(RemoteError, 'did not reply'):
            client.upload(source, '/lost-ready.bin')
        self.assertFalse((self.sd/'lost-ready.bin').exists())
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))
        client = self.client('upload-drop-terminal')
        with self.assertRaises(UploadUncertain):
            client.upload(source, '/uncertain.bin')
        self.assertEqual((self.sd/'uncertain.bin').read_bytes(), self.payload)

    def test_stale_partial_cleanup_and_upload_expiry(self):
        source = self.root/'source'
        source.write_bytes(self.payload)
        client = self.client('abort-fail')
        client.progress = lambda **info: client.cancel.set() if info.get('done', 0) >= 512 else None
        with self.assertRaises(Cancelled):
            client.upload(source, '/cancelled.bin')
        leftovers = list(self.sd.rglob('.kfs-*.part'))
        self.assertEqual(len(leftovers), 1)
        cleaner = self.client()
        cleaner.clean_partial('/'+leftovers[0].name)
        self.assertFalse(leftovers[0].exists())
        with self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
            cleaner.request('CLEAN', '/binary.bin'.encode().hex())
        expired = self.client('upload-expire')
        with self.assertRaisesRegex(RemoteError, 'NO_SESSION'):
            expired.upload(source, '/expired-upload.bin')
        self.assertFalse((self.sd/'expired-upload.bin').exists())
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_upload_roundtrip_empty_binary_nested_and_jpeg(self):
        client = self.client()
        for remote, data in [('/new-empty.txt', b''), ('/images/new.jpg', bytes(range(256))*56+b'\xff\x00+++'),
                             ('/new.bin', self.payload)]:
            with self.subTest(remote=remote):
                local = self.root/'source'
                local.write_bytes(data)
                self.assertEqual(client.upload(local, remote), remote)
                self.assertEqual((self.sd/remote[1:]).read_bytes(), data)
                downloaded = self.root/'downloaded'
                client.download(remote, downloaded)
                self.assertEqual(downloaded.read_bytes(), data)
                self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_upload_recovers_short_writes_and_fails_safely_on_permanent_errors(self):
        local = self.root/'source'
        local.write_bytes(self.payload)
        for fault in ('write-short', 'write-error'):
            with self.subTest(fault=fault):
                client = self.client(fault)
                client.upload(local, '/'+fault+'.bin')
                self.assertEqual((self.sd/(fault+'.bin')).read_bytes(), self.payload)
        for fault, error in [('write-permanent', 'WRITE_FAILED'), ('sync-fail', 'SYNC_FAILED'),
                             ('verify-corrupt', 'VERIFY_CRC_MISMATCH'), ('commit-fail', 'COMMIT_FAILED'),
                             ('no-space', 'OUT_OF_SPACE')]:
            with self.subTest(fault=fault):
                client = self.client(fault)
                with self.assertRaisesRegex(RemoteError, error):
                    client.upload(local, '/'+fault+'.bin')
                self.assertFalse((self.sd/(fault+'.bin')).exists())
                self.assertFalse(list(self.sd.rglob('.kfs-*.part')))

    def test_upload_does_not_replace_existing_files_and_cleans_cancellation(self):
        client = self.client()
        local = self.root/'source'
        local.write_bytes(self.payload)
        for remote in ('/binary.bin', '/BINARY.BIN'):
            with self.assertRaisesRegex(RemoteError, 'TARGET_EXISTS'):
                client.upload(local, remote)
        self.assertEqual((self.sd/'binary.bin').read_bytes(), self.payload)
        def cancel(**info):
            if info.get('done', 0) >= 512:
                client.cancel.set()
        client.progress = cancel
        with self.assertRaises(Cancelled):
            client.upload(local, '/cancelled.bin')
        self.assertFalse((self.sd/'cancelled.bin').exists())
        self.assertFalse(list(self.sd.rglob('.kfs-*.part')))
        client.cancel.clear()
        client.progress = lambda **info: None
        self.assertEqual(len(client.list_directory('/')), 3)

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
