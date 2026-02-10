--
-- String decoder functions
--

local stringdecode = {}

local has_iconv, iconv = pcall(require, "iconv")
local is_windows = package.config:sub(1, 1) == "\\"

local function command_ok(...)
    local ok, how, code = ...
    if type(ok) == "number" then
        return ok == 0
    end
    if ok == true then
        return code == nil or code == 0
    end
    return how == "exit" and code == 0
end

local function null_device()
    if is_windows then
        return "NUL"
    end
    return "/dev/null"
end

local iconv_cli_status_ok = command_ok(os.execute("iconv --version >" .. null_device() .. " 2>" .. null_device()))
local has_iconv_cli = iconv_cli_status_ok

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

local function shell_quote(value)
    local text = tostring(value)
    if is_windows then
        return "\"" .. text:gsub("\"", "\"\"") .. "\""
    end
    return "'" .. text:gsub("'", "'\\''") .. "'"
end

local function decode_via_iconv_cli(str, encoding)
    if not has_iconv_cli then
        return nil
    end

    local input_tmp = os.tmpname()
    local output_tmp = os.tmpname()
    local input = io.open(input_tmp, "wb")
    if not input then
        return nil
    end
    input:write(str)
    input:close()

    local stderr_redirect = "2>" .. null_device()
    local cmd = "iconv -f " .. shell_quote(encoding) ..
        " -t UTF-8 " .. shell_quote(input_tmp) ..
        " > " .. shell_quote(output_tmp) ..
        " " .. stderr_redirect
    local status_ok = command_ok(os.execute(cmd))
    if status_ok then
        local output = io.open(output_tmp, "rb")
        if output then
            local converted = output:read("*a")
            output:close()
            os.remove(input_tmp)
            os.remove(output_tmp)
            return converted
        end
    end

    os.remove(input_tmp)
    os.remove(output_tmp)
    return nil
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
    if converted == nil then
        converted = decode_via_iconv_cli(str, encoding)
    end
    if converted ~= nil then
        return converted
    end

    error("Encoding " .. encoding .. " not supported")
end

return stringdecode
