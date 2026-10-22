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
import io
from PIL import Image

PROJECT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--gcs', type=Path, default=PROJECT.parents[2] / 'KestrelSAT-GCS')
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(args.gcs))
import serial
from kestrelsat.camera_files import CameraClient, new_capture
from kestrelsat.remote_files import Client, Cancelled, RemoteError, UploadUncertain, OperationUncertain, encode, decode


class FirmwareProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.build.name) / 'file-service'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I', str(PROJECT / 'include'), str(PROJECT / 'test/host_service.cpp'),
                        '-o', str(cls.exe)], check=True)
        vendor = PROJECT/'lib/Arducam_Mega-3.0.0/src'
        driver = Path(cls.build.name)/'camera-driver.o'
        subprocess.run(['gcc', '-DESP32', '-w', '-I', str(vendor/'Arducam'), '-c',
                        str(vendor/'Arducam/ArducamCamera.c'), '-o', str(driver)], check=True)
        cls.hardware = Path(cls.build.name)/'camera-hardware'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(PROJECT/'include'),
                        '-I', str(vendor), str(PROJECT/'test/host_camera_hardware.cpp'), str(driver),
                        '-o', str(cls.hardware)], check=True)

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

    def camera(self, fault=''):
        peer = self.client(fault)
        return CameraClient(peer.port, timeout=.12, attempts=4)

    def test_camera_production_adapter_with_vendor_driver(self):
        subprocess.run([str(self.hardware)], check=True, timeout=5)

    def test_camera_capture_save_verify_download_and_duplicate_protection(self):
        client = self.camera()
        info = client.camera_info()
        self.assertTrue(info.ready and info.sd_ready)
        self.assertIn(5, info.modes)
        pending = new_capture(5)
        initial = client.start_capture(pending)
        self.assertNotEqual(initial['state'], 'SAVED')
        self.assertFalse((self.sd/pending['path'][1:]).exists())
        with self.assertRaisesRegex(RemoteError, 'BUSY'):
            client.request('OPEN', '/binary.bin'.encode().hex())
        result = client.wait_capture(pending, initial, poll_interval=.001)
        self.assertEqual(result['state'], 'SAVED')
        image = self.sd/pending['path'][1:]
        self.assertEqual(image.stat().st_size, result['total'])
        self.assertEqual(f'{zlib.crc32(image.read_bytes()):08x}', result['crc'])
        destination = self.root/'downloaded.jpg'
        client.download(pending['path'], destination)
        self.assertEqual(destination.read_bytes(), image.read_bytes())
        self.assertFalse(list((self.sd/'images').glob('.kfs-*.part')))
        client.hello()
        self.assertEqual(client.start_capture(pending)['state'], 'SAVED')
        image.unlink()
        # Replaying the original capture must not take a new photo after deletion.
        self.assertEqual(client.start_capture(pending)['state'], 'SAVED')
        self.assertFalse(image.exists())
        changed = dict(pending, path='/images/other.jpg')
        with self.assertRaisesRegex(RemoteError, 'JOB_MISMATCH'):
            client.start_capture(changed)

    def test_camera_lost_start_ack_recovers_without_duplicate_capture(self):
        client = self.camera('camera-drop-start')
        pending = new_capture(3)
        initial = client.start_capture(pending)
        result = client.wait_capture(pending, initial, poll_interval=.001)
        self.assertEqual(result['state'], 'SAVED')
        self.assertEqual(len(list((self.sd/'images').glob('*_'+pending['job']+'.jpg'))), 1)

        client = self.camera('camera-drop-status')
        pending = new_capture(5)
        with self.assertRaises(OperationUncertain):
            client.start_capture(pending)
        self.assertEqual(len(list((self.sd/'images').glob('*_'+pending['job']+'.jpg'))), 1)

    def test_camera_fifo_prefix_padding_and_split_markers_preserve_real_jpeg(self):
        stream = io.BytesIO()
        Image.new('RGB', (96, 96), (50, 100, 150)).save(stream, 'JPEG')
        photo = stream.getvalue()
        # EOI inside an APP/EXIF-like payload must not truncate the outer JPEG.
        metadata = b'thumbnail\xff\xd8embedded\xff\xd9metadata'
        photo = photo[:2]+b'\xff\xe1'+(len(metadata)+2).to_bytes(2, 'big')+metadata+photo[2:]
        for prefix in (b'', b'\x00'*255, b'\xff\x00junk', b'\x00'*((257-len(photo)) % 256)):
            with self.subTest(prefix=len(prefix)):
                raw = prefix+photo+b'\x00\xff\x00\xff'*4
                (self.sd/'fifo.bin').write_bytes(raw)
                client = self.camera('camera-fixture')
                pending = new_capture(0)
                result = client.wait_capture(pending, client.start_capture(pending), poll_interval=.001)
                self.assertEqual(result['state'], 'SAVED', result)
                path = self.sd/pending['path'][1:]
                self.assertEqual(path.read_bytes(), photo)
                self.assertEqual(result['total'], len(photo))
                self.assertEqual(result['crc'], f'{zlib.crc32(photo):08x}')
                detail = client.capture_diagnostics(pending['job'])
                self.assertEqual((detail['fifo_bytes'], detail['read_bytes'], detail['saved_bytes'],
                                  detail['prefix_bytes']), (len(raw), len(raw), len(photo), len(prefix)))
                with Image.open(path) as decoded:
                    decoded.load(); self.assertEqual(decoded.size, (96, 96))
                downloaded = self.root/(pending['job']+'.jpg')
                client.download(pending['path'], downloaded)
                self.assertEqual(downloaded.read_bytes(), photo)
                self.assertEqual(client.capture_diagnostics(pending['job'])['error'], 'NONE')

    def test_camera_truncated_or_malformed_fifo_does_not_publish(self):
        stream = io.BytesIO(); Image.new('RGB', (96, 96)).save(stream, 'JPEG')
        photo = stream.getvalue()
        for raw in (photo[:-1], b'\x00'*300, b'\xff\xd8\xff\xd9', b'\xff\xd8garbage'+b'\x00'*300,
                    b'\xff\xd8\xff\xe1\x00\x01', b'\xff\xd8\xff\xe1\xff\xff'+photo[2:]):
            with self.subTest(length=len(raw)):
                (self.sd/'fifo.bin').write_bytes(raw)
                client = self.camera('camera-fixture'); pending = new_capture(0)
                result = client.wait_capture(pending, client.start_capture(pending), poll_interval=.001)
                self.assertEqual(result['error'], 'INVALID_JPEG')
                self.assertFalse((self.sd/pending['path'][1:]).exists())
                self.assertFalse(list((self.sd/'images').glob('.kfs-*.part')))
                detail = client.capture_diagnostics(pending['job'])
                self.assertIsNotNone(detail)
                if raw.startswith(b'\xff\xd8garbage'):
                    # The full block was consumed before parsing found byte 3 bad.
                    self.assertEqual(detail['read_bytes'], 256)
                    self.assertEqual(detail['first_hex'], raw[:8].hex())
                    self.assertEqual(detail['last_hex'], raw[248:256].hex())
                self.assertTrue(client.list_directory('/'))

    def test_camera_rejections_preserve_files_and_active_transfers(self):
        client = self.camera()
        pending = new_capture(5)
        opened = client.request('OPEN', '/binary.bin'.encode().hex())
        with self.assertRaisesRegex(RemoteError, 'BUSY'):
            client.start_capture(pending)
        client.request('CLOSE', opened[1])
        existing = self.sd/pending['path'][1:]
        existing.write_bytes(b'preserve this file')
        with self.assertRaisesRegex(RemoteError, 'TARGET_EXISTS'):
            client.start_capture(pending)
        self.assertEqual(existing.read_bytes(), b'preserve this file')
        for path in ('/elsewhere.jpg', '/images/nested/photo.jpg', '/images/.kfs-bad.jpg'):
            with self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
                client.request('CAPTURE', pending['job'], 5, path.encode().hex())
        with self.assertRaisesRegex(RemoteError, 'BAD_RESOLUTION'):
            client.request('CAPTURE', pending['job'], 13, '/images/test.jpg'.encode().hex())
        for fault, reason in [('camera-absent', 'CAMERA_UNAVAILABLE'), ('camera-sd-absent', 'SD_UNAVAILABLE')]:
            with self.subTest(fault=fault), self.assertRaisesRegex(RemoteError, reason):
                self.camera(fault).start_capture(new_capture(5))

    def test_camera_timeout_sd_failures_and_bad_jpeg_do_not_publish(self):
        for fault, reason in [('camera-timeout', 'CAMERA_TIMEOUT'), ('camera-full', 'OUT_OF_SPACE'),
                ('camera-start-fail', 'CAMERA_START_FAILED'), ('camera-read-fail', 'CAMERA_READ_FAILED'),
                ('camera-write-fail', 'SD_WRITE_FAILED'), ('camera-invalid-jpeg', 'INVALID_JPEG'),
                ('camera-corrupt-sd', 'VERIFY_CRC_MISMATCH'), ('camera-verify-fail', 'SD_VERIFY_READ_FAILED'),
                ('camera-publish-fail', 'PUBLISH_FAILED'), ('camera-sync-fail', 'SD_SYNC_FAILED'),
                ('camera-too-large', 'BAD_IMAGE_SIZE'), ('camera-slow-job', 'JOB_TIMEOUT')]:
            with self.subTest(fault=fault):
                client = self.camera(fault)
                pending = new_capture(5)
                result = client.wait_capture(pending, client.start_capture(pending), poll_interval=.001)
                self.assertEqual((result['state'], result['error']), ('ERROR', reason))
                self.assertFalse((self.sd/pending['path'][1:]).exists())
                self.assertFalse(list((self.sd/'images').glob('.kfs-*.part')))
                self.assertTrue(client.list_directory('/'))

    def test_camera_failed_cleanup_preserves_partial_for_explicit_removal(self):
        client = self.camera('camera-cleanup-fail')
        pending = new_capture(5)
        result = client.wait_capture(pending, client.start_capture(pending), poll_interval=.001)
        self.assertEqual(result['error'], 'INVALID_JPEG_CLEANUP_FAILED')
        partial = '/images/.kfs-'+pending['job']+'.part'
        self.assertTrue((self.sd/partial[1:]).exists())
        client.clean_partial(partial)
        self.assertFalse((self.sd/partial[1:]).exists())

    def test_camera_abort_and_publication_collision_are_safe(self):
        client = self.camera()
        pending = new_capture(5)
        client.start_capture(pending)
        reply = client.request('CAMABORT', 'ffffffff', expected={'CAMJOB'})
        self.assertEqual(reply[2], 'UNKNOWN')
        reply = client.request('CAMABORT', pending['job'], expected={'CAMJOB'})
        self.assertEqual(reply[2], 'CANCELLED')
        self.assertFalse(list((self.sd/'images').glob('.kfs-*.part')))
        self.assertEqual(client.start_capture(pending)['state'], 'CANCELLED')
        pending = new_capture(5)
        initial = client.start_capture(pending)
        target = self.sd/pending['path'][1:]
        target.write_bytes(b'created while camera was running')
        result = client.wait_capture(pending, initial, poll_interval=.001)
        self.assertEqual(result['error'], 'PUBLISH_FAILED')
        self.assertEqual(target.read_bytes(), b'created while camera was running')
        self.assertEqual(client.capture_status('12345678', '/images/no-such-job.jpg')['state'], 'UNKNOWN')

    def test_rename_delete_files_and_preserve_existing_targets(self):
        client = self.client()
        opened = client.request('OPEN', '/binary.bin'.encode().hex())
        with self.assertRaisesRegex(RemoteError, 'BUSY'):
            client.delete_file('/binary.bin')
        client.request('CLOSE', opened[1])
        client.rename_file('/binary.bin', '/renamed.bin')
        self.assertFalse((self.sd/'binary.bin').exists())
        self.assertEqual((self.sd/'renamed.bin').read_bytes(), self.payload)
        with self.assertRaisesRegex(RemoteError, 'TARGET_EXISTS'):
            client.rename_file('/renamed.bin', '/empty.txt')
        with self.assertRaisesRegex(RemoteError, 'NOT_A_FILE'):
            client.delete_file('/images')
        with self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
            client.request('RENAME', '/renamed.bin'.encode().hex(), '/images/moved.bin'.encode().hex())
        with self.assertRaisesRegex(RemoteError, 'BAD_PATH'):
            client.request('DELETE', '/.kfs-12345678.part'.encode().hex())
        client.delete_file('/renamed.bin')
        self.assertFalse((self.sd/'renamed.bin').exists())
        self.assertTrue((self.sd/'empty.txt').exists())

    def test_management_lost_replies_recover_and_unknown_is_explicit(self):
        for op in ('delete', 'rename'):
            for fault in ('manage-drop-reply', 'manage-drop-all', 'manage-expire-reply'):
                with self.subTest(op=op, fault=fault):
                    path = self.sd/f'{op}-{fault}.bin'
                    path.write_bytes(self.payload)
                    client = self.client(fault)
                    operation = (lambda: client.delete_file('/'+path.name)) if op == 'delete' else (
                        lambda: client.rename_file('/'+path.name, '/renamed-'+path.name))
                    if fault == 'manage-drop-all':
                        with self.assertRaises(OperationUncertain): operation()
                    else:
                        operation()
                    self.assertFalse(path.exists())
                    if op == 'rename':
                        self.assertEqual((self.sd/('renamed-'+path.name)).read_bytes(), self.payload)

    def test_delete_duplicate_after_metadata_query_preserves_recreated_file(self):
        request = encode(0x12345678, 'DELETE', '/binary.bin'.encode().hex()).decode().strip()
        # Drive the same service directly to place metadata queries between retries.
        process = subprocess.Popen([str(self.exe), str(self.sd)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, text=True)
        try:
            process.stdin.write(request+'\n'); process.stdin.flush()
            first = process.stdout.readline()
            self.assertEqual(decode(first.encode())[1], ['DELETED'])
            (self.sd/'binary.bin').write_bytes(b'new file under the old name')
            process.stdin.write(encode(44, 'HELLO').decode()+request+'\n'); process.stdin.flush()
            process.stdout.readline()
            self.assertEqual(process.stdout.readline(), first)
            self.assertEqual((self.sd/'binary.bin').read_bytes(), b'new file under the old name')
        finally:
            process.stdin.close(); process.stdout.close(); process.terminate(); process.wait(timeout=2)

    def test_management_long_paths_and_backend_failures(self):
        parent = self.sd/('p'*175)
        parent.mkdir()
        (parent/'original.txt').write_bytes(b'long path')
        client = self.client()
        old = '/'+parent.name+'/original.txt'
        new = '/'+parent.name+'/renamed.txt'
        client.rename_file(old, new)
        self.assertEqual((parent/'renamed.txt').read_bytes(), b'long path')
        client.delete_file(new)
        for op, fault in [('DELETE', 'delete-fail'), ('RENAME', 'rename-fail')]:
            peer = self.client(fault)
            args = ['/binary.bin'.encode().hex()]
            if op == 'RENAME': args.append('/new.bin'.encode().hex())
            with self.assertRaisesRegex(RemoteError, op+'_FAILED'):
                peer.request(op, *args)
            self.assertEqual((self.sd/'binary.bin').read_bytes(), self.payload)

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
