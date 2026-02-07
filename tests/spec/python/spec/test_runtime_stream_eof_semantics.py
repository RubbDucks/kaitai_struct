import io
import unittest

from kaitaistruct import KaitaiStream, EndOfStreamError


class _NonSeekableBytesIO(io.BytesIO):
    def seekable(self):
        return False

    def seek(self, *args, **kwargs):
        raise io.UnsupportedOperation("not seekable")

    def tell(self):
        raise io.UnsupportedOperation("not seekable")


class TestRuntimeStreamEofSemantics(unittest.TestCase):
    def test_short_read_rewinds_seekable_stream(self):
        ks = KaitaiStream(io.BytesIO(b"abcd"))

        with self.assertRaises(EndOfStreamError):
            ks.read_bytes(5)

        self.assertEqual(ks.pos(), 0)

    def test_short_read_non_seekable_stream_has_no_rewind(self):
        ks = KaitaiStream(_NonSeekableBytesIO(b"abcd"))

        with self.assertRaises(EndOfStreamError):
            ks.read_bytes(5)

    def test_read_bytes_aligns_after_bit_reads(self):
        ks = KaitaiStream(io.BytesIO(b"\xAA\xBB"))
        self.assertEqual(ks.read_bits_int_be(4), 0xA)
        self.assertEqual(ks.read_bytes(1), b"\xBB")
