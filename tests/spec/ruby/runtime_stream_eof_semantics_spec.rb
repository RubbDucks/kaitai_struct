require 'kaitai/struct/struct'
require 'stringio'

RSpec.describe 'RuntimeStreamEofSemantics' do
  it 'rewinds seekable stream position on short read failure' do
    ks = Kaitai::Struct::Stream.new(StringIO.new('abcd'))

    expect { ks.read_bytes(5) }.to raise_error(EOFError, 'attempted to read 5 bytes, got only 4')
    expect(ks.pos).to eq 0
  end

  it 'aligns to next byte when switching from bit to byte reads' do
    ks = Kaitai::Struct::Stream.new([0xAA, 0xBB].pack('C*'))

    expect(ks.read_bits_int_be(4)).to eq 0xA
    expect(ks.read_bytes(1)).to eq [0xBB].pack('C*')
  end
end
