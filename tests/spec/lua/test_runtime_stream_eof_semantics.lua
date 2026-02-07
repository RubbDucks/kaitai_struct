local luaunit = require("luaunit")

require("kaitaistruct")
local stringstream = require("string_stream")

TestRuntimeStreamEofSemantics = {}

function TestRuntimeStreamEofSemantics:test_short_read_rewinds_seekable_stream()
    local ks = KaitaiStream(stringstream("abcd"))

    luaunit.assertErrorMsgContains("requested 5 bytes, but only 4 bytes available", function()
        ks:read_bytes(5)
    end)

    luaunit.assertEquals(ks:pos(), 0)
end

function TestRuntimeStreamEofSemantics:test_read_bytes_aligns_after_bit_reads()
    local ks = KaitaiStream(stringstream("\170\187"))

    luaunit.assertEquals(ks:read_bits_int_be(4), 0xA)
    luaunit.assertEquals(ks:read_bytes(1), "\187")
end
