--
-- String decoder functions
--

local stringdecode = {}

local has_iconv, iconv = pcall(require, "iconv")

-- From http://lua-users.org/wiki/LuaUnicode
local function utf8_to_32(utf8str)
    assert(type(utf8str) == "string")
    local res, seq, val = {}, 0, nil

    for i = 1, #utf8str do
        local c = string.byte(utf8str, i)
        if seq == 0 then
            table.insert(res, val)
            seq = c < 0x80 and 1 or c < 0xE0 and 2 or c < 0xF0 and 3 or
                  c < 0xF8 and 4 or
                error("Invalid UTF-8 character sequence")
            val = c & (2^(8-seq) - 1)
        else
            val = (val << 6) | (c & 0x3F)
        end

        seq = seq - 1
    end

    table.insert(res, val)

    return res
end

local function decode_via_iconv(str, encoding)
    if not has_iconv then
        return nil
    end

    local cd = iconv.new("UTF-8", encoding)
    if not cd then
        return nil
    end

    local converted, err = cd:iconv(str)
    if err then
        error("Encoding " .. encoding .. " conversion failed: " .. tostring(err))
    end

    return converted
end

function stringdecode.decode(str, encoding)
    local enc = encoding and encoding:lower() or "ascii"

    if enc == "ascii" then
        return str
    elseif enc == "utf-8" then
        local code_points = utf8_to_32(str)
        return utf8.char(table.unpack(code_points))
    end

    local converted = decode_via_iconv(str, encoding)
    if converted ~= nil then
        return converted
    end

    error("Encoding " .. encoding .. " not supported")
end

return stringdecode
