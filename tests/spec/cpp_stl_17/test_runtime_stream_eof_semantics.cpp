#include <boost/test/unit_test.hpp>
#include <sstream>
#include "kaitai/kaitaistream.h"

BOOST_AUTO_TEST_CASE(test_runtime_stream_short_read_rewinds_seekable_stream) {
    std::istringstream is("abcd", std::ios::binary);
    kaitai::kstream ks(&is);

    BOOST_CHECK_THROW(ks.read_bytes(5), std::istream::failure);
    BOOST_CHECK_EQUAL(ks.pos(), 0);
}

BOOST_AUTO_TEST_CASE(test_runtime_stream_aligns_after_bit_reads) {
    std::string data;
    data.push_back(static_cast<char>(0xAA));
    data.push_back(static_cast<char>(0xBB));

    std::istringstream is(data, std::ios::binary);
    kaitai::kstream ks(&is);

    BOOST_CHECK_EQUAL(ks.read_bits_int_be(4), 0xA);
    BOOST_CHECK_EQUAL(ks.read_bytes(1), std::string(1, static_cast<char>(0xBB)));
}
