"""Host tests of production plugin definitions, without Calibre or a device."""
import ast
import datetime
import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zlib

class Clock:
    now = 0.0
    def monotonic(self): return self.now
    def sleep(self, duration): self.now += duration

class Port:
    def __init__(self, responses=(), fail_open=False):
        self.responses = iter(responses)
        self.fail_open = fail_open
        self.closed = False
        self.writes = []
        self.resets = 0
    def open(self):
        assert self.dtr is False and self.rts is False
        if self.fail_open: raise OSError('busy')
    def reset_input_buffer(self): self.resets += 1
    def readline(self): return next(self.responses, b'')
    def write(self, data): self.writes.append(data)
    def close(self): self.closed = True

class PluginTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.ns = dict(os=os, struct=struct, time=self.clock, zlib=zlib,
                       datetime=datetime, tempfile=tempfile, DevicePlugin=object)
        tree = ast.parse(Path(__file__).with_name('__init__.py').read_text())
        # Compile the actual classes/functions; omit only Calibre imports/bootstrap.
        tree.body = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef, ast.Assign))]
        exec(compile(tree, 'production_plugin', 'exec'), self.ns)
    def connect(self, port):
        module = types.SimpleNamespace(Serial=lambda **kwargs: port)
        with patch.dict(sys.modules, serial=module): return self.ns['_Conn']('/dev/cu.test')
    def test_waits_for_status_not_boot_log(self):
        port = Port([b'', b'booting\n', b'STATUS:heap=123\n'])
        conn = self.connect(port)
        self.assertEqual(port.writes, [b'CMNDS'] * 3)
        self.assertEqual(port.resets, 2)
        self.assertFalse(port.closed)
        conn.close()
        self.assertTrue(port.closed)
    def test_timeout_closes_port(self):
        port = Port()
        with self.assertRaises(TimeoutError): self.connect(port)
        self.assertTrue(port.closed)
        self.assertLess(self.clock.now, 21)
    def test_open_failure_closes_port(self):
        port = Port(fail_open=True)
        with self.assertRaises(OSError): self.connect(port)
        self.assertTrue(port.closed)
    def test_upload_reports_real_paperio_path(self):
        plugin = self.ns['MicroreaderPlugin']()
        calls = []
        plugin._conn = types.SimpleNamespace(upload=lambda *args, **kwargs: calls.append(args))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'book.epub'
            path.write_bytes(b'epub fixture')
            result = plugin.upload_books([str(path)], ['book.epub'])
        self.assertEqual(result, [('/books/book.epub', 12, None)])
        self.assertEqual(calls[0][1], 'book.epub')
        self.assertEqual(plugin.FORMATS, ['epub'])
        self.assertEqual(plugin.name, 'Microreader')
    def test_download_and_delete_use_reported_path(self):
        plugin = self.ns['MicroreaderPlugin']()
        paths = []
        plugin._conn = types.SimpleNamespace(download=lambda p: paths.append(p) or b'book', delete=paths.append)
        out = io.BytesIO()
        plugin.get_file('books/book.epub', out)
        plugin.delete_books(['books/book.epub'])
        self.assertEqual(paths, ['/books/book.epub', '/books/book.epub'])
        self.assertEqual(out.getvalue(), b'book')
    def test_list_reads_paperio_three_field_rows(self):
        conn = self.ns['_Conn'].__new__(self.ns['_Conn'])
        conn._s = Port([b'BOOKS:\n', b'/books/a.epub|Title|Author\n', b'END\n'])
        self.assertEqual(conn.list_books(), [('/books/a.epub', 'Title', ['Author'], 0, 0)])

if __name__ == '__main__': unittest.main()
