/*!A cross-platform build utility based on Lua
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2015 - 2019, TBOOX Open Source Group.
 *
 * @author      OpportunityLiu
 * @file        file_read.c
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME "file_read"
#define TB_TRACE_MODULE_DEBUG (0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "prefix.h"
#ifdef TB_CONFIG_OS_WINDOWS
#    include "../winos/ansi.h"
#    include <io.h>
#else
#    include <unistd.h>
#endif
#include "file.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

// num of bytes read to guess encoding
#define CHECK_SIZE (1024)

// is utf-8 tail character
#define IS_UTF8_TAIL(c) (c >= 0x80 && c < 0xc0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */
typedef enum __xm_pushline_state_e
{
    PL_EOF,
    PL_FIN,
    PL_CONL,
    PL_FAIL,

} xm_pushline_state_e;

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */

static tb_size_t xm_io_file_detect_charset(tb_byte_t const** data_ptr, tb_long_t size)
{
    // check
    tb_assert(data_ptr && *data_ptr);

    tb_byte_t const* data    = *data_ptr;
    tb_size_t        charset = TB_CHARSET_TYPE_NONE;
    do
    {
        if (size >= 3 && data[0] == 239 && data[1] == 187 && data[2] == 191) // utf-8 with bom
        {
            charset = TB_CHARSET_TYPE_UTF8;
            data += 3; // skip bom
            break;
        }
        if (size >= 2)
        {
            if (data[0] == 254 && data[1] == 255) // utf16be
            {
                charset = TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_BE;
                data += 2; // skip bom
                break;
            }
            else if (data[0] == 255 && data[1] == 254) // utf16le
            {
                charset = TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_LE;
                data += 2; // skip bom
                break;
            }
        }

        tb_sint16_t utf16be_conf = 0;
        tb_sint16_t utf16le_conf = 0;
        tb_sint16_t utf8_conf    = 0;
        tb_sint16_t ascii_conf   = 0;
        tb_sint16_t zero_count   = 0;
        for (tb_long_t i = 0; i < (size - 4) && i < CHECK_SIZE; i++)
        {
            if (data[i] == 0) zero_count++;

            if (data[i] < 0x80)
                ascii_conf++;
            else
                ascii_conf = TB_MINS16;

            if (i % 2 == 0)
            {
                if (data[i] == 0) utf16be_conf++;
                if (data[i + 1] == 0) utf16le_conf++;
            }

            if (IS_UTF8_TAIL(data[i]))
                ;
            else if (data[i] < 0x80)
                utf8_conf++;
            else if (data[i] >= 0xc0 && data[i] < 0xe0 && IS_UTF8_TAIL(data[i + 1]))
                utf8_conf++;
            else if (data[i] >= 0xe0 && data[i] < 0xf0 && IS_UTF8_TAIL(data[i + 1]) && IS_UTF8_TAIL(data[i + 2]))
                utf8_conf++;
            else if (data[i] >= 0xf0 && data[i] < 0xf8 && IS_UTF8_TAIL(data[i + 1]) && IS_UTF8_TAIL(data[i + 2]) &&
                     IS_UTF8_TAIL(data[i + 3]))
                utf8_conf++;
            else
                utf8_conf = TB_MINS16;
        }

        if (ascii_conf > 0 && zero_count <= 1)
        {
            charset = TB_CHARSET_TYPE_UTF8;
            break;
        }
        if (utf8_conf > 0 && zero_count <= 1)
        {
            charset = TB_CHARSET_TYPE_UTF8;
            break;
        }
        if (utf16be_conf > 0 && utf16be_conf > utf16le_conf)
        {
            charset = TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_BE;
            break;
        }
        if (utf16le_conf > 0 && utf16le_conf >= utf16be_conf)
        {
            charset = TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_LE;
            break;
        }
        if (utf8_conf > 0)
        {
            charset = TB_CHARSET_TYPE_UTF8;
            break;
        }
#ifdef TB_CONFIG_OS_WINDOWS
        charset = XM_IO_FILE_ENCODING_ANSI;
#else
        charset = XM_IO_FILE_ENCODING_BINARY;
#endif
    } while (0);

    *data_ptr = data;
    return charset;
}

static tb_byte_t const* xm_io_file_find_lf(tb_byte_t const* buf, tb_size_t buflen, tb_size_t encoding)
{
    // check
    tb_assert(buf);

    tb_byte_t const* bufend  = buf + buflen;
    tb_bool_t        success = tb_false;
    switch (encoding)
    {
    case TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_LE:
    case TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_BE:
    {
        buflen &= ~1;
        tb_uint16_t const* wbuf    = (tb_uint16_t const*)buf;
        tb_uint16_t const* wbufend = (tb_uint16_t const*)(buf + buflen);
        tb_uint16_t        lf      = (encoding == (TB_CHARSET_TYPE_UTF16 | TB_CHARSET_TYPE_BE)) ? '\n' << 8 : '\n';
        while (wbuf != wbufend)
        {
            if (*wbuf == lf)
            {
                wbuf++;
                success = tb_true;
                break;
            }
            else wbuf++;
        }
        buf = (tb_byte_t const*)wbuf;
        break;
    }
    default:
    {
        while (buf != bufend)
        {
            if (*buf == '\n')
            {
                buf++;
                success = tb_true;
                break;
            }
            else buf++;
        }
    }
    break;
    }
    return success ? buf : tb_null;
}

static tb_int_t xm_io_file_buffer_pushline(luaL_Buffer* buf, xm_io_file* file, tb_char_t const* continuation, tb_bool_t keep_crlf)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_file(file) && !xm_io_file_is_closed(file));

    tb_size_t charset = file->encoding;
    tb_bool_t binary  = charset == XM_IO_FILE_ENCODING_BINARY || charset == XM_IO_FILE_ENCODING_UNKNOWN;
    if (binary)
    {
        continuation = "";
        keep_crlf    = tb_true;
    }

    tb_buffer_t readdata, transdata;
    tb_bool_t   rok = tb_buffer_init(&readdata);
    tb_bool_t   tok = tb_buffer_init(&transdata);
    tb_assert_and_check_return_val(rok && tok, 0);

    // transcode is redundant, write to transdata directly
    tb_byte_t readbuf[512];
    tb_bool_t notrans = charset == TB_CHARSET_TYPE_UTF8 || binary;
    while (1)
    {
        tb_long_t readsize = tb_file_read(file->file_ref, readbuf, tb_arrayn(readbuf));
        if (readsize <= 0) break;
        tb_byte_t const* lf_ptr = xm_io_file_find_lf(readbuf, readsize, charset);
        if (lf_ptr)
        {
            tb_size_t validlen = (tb_size_t)(lf_ptr - readbuf);
            // set pos to after lf
            tb_file_seek(file->file_ref, (tb_hong_t)validlen - readsize, TB_FILE_SEEK_CUR);
            if (notrans)
                tb_buffer_memncpyp(&transdata, tb_buffer_size(&transdata), readbuf, validlen);
            else
                tb_buffer_memncpyp(&readdata, tb_buffer_size(&readdata), readbuf, validlen);
            break;
        }
        // not line ending? copy to heap and continue
        if (notrans)
            tb_buffer_memncpyp(&transdata, tb_buffer_size(&transdata), readbuf, readsize);
        else
            tb_buffer_memncpyp(&readdata, tb_buffer_size(&readdata), readbuf, readsize);
    }

    tb_size_t len = 0;
    tb_int_t  result = 0;
    tb_size_t conlen = tb_strlen(continuation);
    do
    {
        if (tb_buffer_size(notrans ? &transdata : &readdata) == 0)
        {
            result = PL_EOF;
            break;
        }

        if (notrans)
        {
            len = tb_buffer_size(&transdata);
            tb_buffer_memncpyp(&transdata, len, (tb_byte_t const*)"\0\0", 2);
        }
        else
        {
            len = tb_buffer_size(&readdata);
            tb_buffer_memncpyp(&readdata, len, (tb_byte_t const*)"\0\0", 2);
        }

        if (notrans)
        {
        }
#ifdef TB_CONFIG_OS_WINDOWS
        else if (charset == XM_IO_FILE_ENCODING_ANSI)
        {
            tb_long_t        dst_size = 0;
            tb_size_t        dst_maxn = len * 3;
            tb_char_t*       dst_data = (tb_char_t*)tb_buffer_resize(&transdata, dst_maxn + 1);
            tb_char_t const* src_data = (tb_char_t const*)tb_buffer_data(&readdata);
            tb_assert(src_data && dst_data);
            if (dst_data && dst_maxn && (dst_size = xm_mbstoutf8(dst_data, src_data, dst_maxn, GetACP())) >= 0 &&
                dst_size < dst_maxn)
            {
                len = dst_size;
            }
            else
            {
                result = PL_FAIL;
                break;
            }
        }
#endif
        else
        {
            //  convert string
            tb_long_t        dst_size = 0;
            tb_size_t        dst_maxn = len * 3;
            tb_byte_t*       dst_data = tb_buffer_resize(&transdata, dst_maxn + 1);
            tb_byte_t const* src_data = (tb_byte_t const*)tb_buffer_data(&readdata);
            tb_assert(src_data && dst_data);
            if (dst_data && dst_maxn &&
                (dst_size = tb_charset_conv_data(charset, TB_CHARSET_TYPE_UTF8, src_data, len, dst_data, dst_maxn)) >= 0 && dst_size < dst_maxn)
            {
                len = dst_size;
            }
            else
            {
                result = PL_FAIL;
                break;
            }
        }
        tb_byte_t* buf = tb_buffer_data(&transdata);
        tb_assert(buf);
        if (buf[len - 1] != '\n')
        {
            // end of file, no lf found
            result = PL_FIN;
        }
        else if (len > 1)
        {
            if (!binary && buf[len - 2] == '\r')
            {
                buf[len - 2] = '\n';
                len--;
            }
            tb_bool_t con_line = conlen && len >= conlen + 1 && tb_strncmp(continuation, (tb_char_t const*)(buf + len - conlen - 1), conlen) == 0;
            if (!keep_crlf && !con_line) len--;
            if (con_line) len -= conlen + 1;
            buf[len] = '\0';
            result   = con_line ? PL_CONL : PL_FIN;
        }
        else
        {
            // a single '\n'
            if (!keep_crlf) len = 0;
            result = PL_FIN;
        }
    } while (0);

    if (result == PL_FIN || result == PL_CONL) 
        luaL_addlstring(buf, (tb_char_t const*)tb_buffer_data(&transdata), len);

    tb_buffer_exit(&readdata);
    tb_buffer_exit(&transdata);
    return result;
}

static tb_int_t xm_io_file_read_all(lua_State* lua, xm_io_file* file, tb_char_t const* continuation)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_file(file) && !xm_io_file_is_closed(file));

    luaL_Buffer sbuf, *buf = &sbuf;
    luaL_buffinit(lua, buf);
    tb_bool_t has_content = tb_false;
    while (1)
    {
        switch (xm_io_file_buffer_pushline(buf, file, continuation, tb_true))
        {
        case PL_EOF:
            luaL_pushresult(buf);
            if (!has_content) lua_pushliteral(lua, "");
            return 1;
        case PL_FIN:
        case PL_CONL: 
            has_content = tb_true; 
            continue;
        case PL_FAIL:
        default:
            luaL_pushresult(buf); 
            xm_io_file_return_error(lua, file, "failed to readline");
            break;
        }
    }
}

static tb_int_t xm_io_file_read_line(lua_State* lua, xm_io_file* file, tb_char_t const* continuation, tb_bool_t keep_crlf)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_file(file) && !xm_io_file_is_closed(file));

    luaL_Buffer sbuf, *buf = &sbuf;
    luaL_buffinit(lua, buf);
    tb_bool_t has_content = tb_false;
    while (1)
    {
        switch (xm_io_file_buffer_pushline(buf, file, continuation, keep_crlf))
        {
        case PL_EOF:
            luaL_pushresult(buf);
            if (!has_content) lua_pushnil(lua);
            return 1;
        case PL_FIN: luaL_pushresult(buf); return 1;
        case PL_CONL: has_content = tb_true; continue;
        case PL_FAIL:
        default: 
            luaL_pushresult(buf); 
            xm_io_file_return_error(lua, file, "failed to readline");
            break;
        }
    }
}

static tb_int_t xm_io_file_read_n(lua_State* lua, xm_io_file* file, tb_char_t const* continuation, tb_long_t n)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_file(file) && !xm_io_file_is_closed(file));

    // check continuation
    if (*continuation != '\0') 
        xm_io_file_return_error(lua, file, "continuation is not supported for read number of bytes");

    // check encoding
    tb_size_t charset = file->encoding;
    tb_bool_t binary  = charset == XM_IO_FILE_ENCODING_BINARY || charset == XM_IO_FILE_ENCODING_UNKNOWN;
    if (!binary)
        xm_io_file_return_error(lua, file, "read number of bytes only allows binary file, reopen with 'rb' and try again");

    if (n == 0)
    {
        tb_byte_t buf[1];
        tb_long_t readsize = tb_file_read(file->file_ref, buf, 1);
        if (readsize > 0)
        {
            lua_pushliteral(lua, "");
            tb_file_seek(file->file_ref, -readsize, TB_FILE_SEEK_CUR);
            lua_pushlstring(lua, (tb_char_t const*)buf, readsize);
        }
        else lua_pushnil(lua);
        return 1;
    }
    else
    {
        tb_buffer_t buf;
        tb_bool_t   ok = tb_buffer_init(&buf);
        tb_assert_and_check_return_val(ok, 0);
        tb_byte_t* bufptr = tb_buffer_resize(&buf, n + 1);
        tb_assert(bufptr);
        tb_long_t readsize = tb_file_read(file->file_ref, bufptr, n);
        if (readsize > 0)
            lua_pushlstring(lua, (tb_char_t const*)bufptr, readsize);
        else
            lua_pushnil(lua);
        tb_buffer_exit(&buf);
        return 1;
    }
}

static tb_size_t xm_io_file_std_buffer_pushline(luaL_Buffer* buf, xm_io_file* file, tb_char_t const* continuation, tb_bool_t keep_crlf)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_std(file) && !xm_io_file_is_closed(file));

    // get input buffer
    tb_char_t strbuf[8192];
    tb_size_t buflen = 0;
    tb_size_t result = PL_FAIL;
    if (tb_stdfile_gets(file->std_ref, strbuf, tb_arrayn(strbuf) - 1))
        buflen = tb_strlen(strbuf);
    else
        return PL_EOF;

    tb_size_t conlen = tb_strlen(continuation);
    if (buflen > 0 && strbuf[buflen - 1] != '\n')
    {
        // end of file, no lf found
        result = PL_FIN;
    }
    else if (buflen > conlen)
    {
        tb_bool_t con_line = conlen && buflen >= conlen + 1 && tb_strncmp(continuation, (strbuf + buflen - conlen - 1), conlen) == 0;
        if (!keep_crlf && !con_line) buflen--;
        if (con_line) buflen -= conlen + 1;
        result = con_line ? PL_CONL : PL_FIN;
    }
    else
    {
        // a single '\n'
        if (!keep_crlf) buflen = 0;
        result = PL_FIN;
    }

    if (result == PL_FIN || result == PL_CONL) luaL_addlstring(buf, strbuf, buflen);
    return result;
}

static tb_int_t xm_io_file_std_read_line(lua_State* lua, xm_io_file* file, tb_char_t const* continuation, tb_bool_t keep_crlf)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_std(file) && !xm_io_file_is_closed(file));

    luaL_Buffer sbuf, *buf = &sbuf;
    luaL_buffinit(lua, buf);
    tb_bool_t has_content = tb_false;
    while (1)
    {
        switch (xm_io_file_std_buffer_pushline(buf, file, continuation, keep_crlf))
        {
        case PL_EOF:
            luaL_pushresult(buf);
            if (!has_content) lua_pushnil(lua);
            return 1;
        case PL_FIN: 
            luaL_pushresult(buf); 
            return 1;
        case PL_CONL: 
            has_content = tb_true; 
            continue;
        case PL_FAIL:
        default: 
            luaL_pushresult(buf); 
            xm_io_file_return_error(lua, file, "failed to readline");
            break;
        }
    }
}

static tb_int_t xm_io_file_std_read_all(lua_State* lua, xm_io_file* file, tb_char_t const* continuation)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_std(file) && !xm_io_file_is_closed(file));

    luaL_Buffer sbuf, *buf = &sbuf;
    luaL_buffinit(lua, buf);
    tb_bool_t has_content = tb_false;
    while (1)
    {
        switch (xm_io_file_std_buffer_pushline(buf, file, continuation, tb_true))
        {
        case PL_EOF:
            luaL_pushresult(buf);
            if (!has_content) lua_pushliteral(lua, "");
            return 1;
        case PL_FIN:
        case PL_CONL: 
            has_content = tb_true;
            continue;
        case PL_FAIL:
        default:
            luaL_pushresult(buf); 
            xm_io_file_return_error(lua, file, "failed to readline");
            break;
        }
    }
}

static tb_int_t xm_io_file_std_read_n(lua_State* lua, xm_io_file* file, tb_char_t const* continuation, tb_long_t n)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_std(file) && !xm_io_file_is_closed(file));

    // check continuation
    if (*continuation != '\0') 
        xm_io_file_return_error(lua, file, "continuation is not supported for std streams");

    // io.read(0)
    if (n == 0)
    {
        tb_char_t ch;
        if (!tb_stdfile_peek(file->std_ref, &ch))
            lua_pushnil(lua);
        else
            lua_pushliteral(lua, "");
        return 1;
    }

    // io.read(n)
    tb_buffer_t buf;
    if (tb_buffer_init(&buf))
    {
        tb_byte_t* buf_ptr = tb_buffer_resize(&buf, (tb_size_t)n);
        tb_assert(buf_ptr);

        if (tb_stdfile_read(file->std_ref, buf_ptr, (tb_size_t)n))
            lua_pushlstring(lua, (tb_char_t const*)buf_ptr, (size_t)n);
        else lua_pushnil(lua);
        tb_buffer_exit(&buf);
    }
    else lua_pushnil(lua);
    return 1;
}

static tb_int_t xm_io_file_std_read_num(lua_State* lua, xm_io_file* file, tb_char_t const* continuation)
{
    // check
    tb_assert(lua && file && continuation && xm_io_file_is_std(file) && !xm_io_file_is_closed(file));

    // check continuation
    if (*continuation != '\0') 
        xm_io_file_return_error(lua, file, "continuation is not supported for std streams");

    // read number
    tb_char_t strbuf[512];
    if (tb_stdfile_gets(file->std_ref, strbuf, tb_arrayn(strbuf)))
    {
        lua_pushnumber(lua, tb_s10tod(strbuf));
        return 1;
    }
    else
    {
        lua_pushnil(lua);
        return 0;
    }
}

/*
 * file:read([mode, [continuation]])
 * file:read("all", "\\")
 * file:read("L")
 * file:read(10)
 */
tb_int_t xm_io_file_read(lua_State* lua)
{
    // check
    tb_assert_and_check_return_val(lua, 0);

    xm_io_file*      file         = xm_io_getfile(lua);
    tb_char_t const* mode         = luaL_optstring(lua, 2, "l");
    tb_char_t const* continuation = luaL_optstring(lua, 3, "");
    tb_assert_and_check_return_val(mode && continuation, 0);

    tb_long_t count = -1;
    if (lua_isnumber(lua, 2))
    {
        count = (tb_long_t)lua_tointeger(lua, 2);
        if (count < 0) xm_io_file_return_error(lua, file, "invalid read size, must be positive nubmber or 0");
    }
    else if (*mode == '*')
        mode++;

    if (xm_io_file_is_file(file))
    {
        if (xm_io_file_is_closed_file(file)) xm_io_file_return_error_closed(lua);
        if (file->encoding == XM_IO_FILE_ENCODING_UNKNOWN)
        {
            // detect encoding
            tb_byte_t        buffer[1024];
            tb_byte_t const* buffer_ptr = buffer;

            // save offset
            tb_hong_t offset = tb_file_offset(file->file_ref);
            tb_file_seek(file->file_ref, 0, TB_FILE_SEEK_BEG);
            tb_long_t size = tb_file_read(file->file_ref, buffer, 1024);
            if (size <= 0)
                file->encoding = XM_IO_FILE_ENCODING_BINARY;
            else
            {
                file->encoding = xm_io_file_detect_charset(&buffer_ptr, size);
                if (offset == 0) offset += buffer_ptr - buffer; // skip bom if we are at the begining
            }
            // restore offset
            tb_file_seek(file->file_ref, offset, TB_FILE_SEEK_BEG);
        }
        if (count >= 0) return xm_io_file_read_n(lua, file, continuation, count);
        switch (*mode)
        {
        case 'a': return xm_io_file_read_all(lua, file, continuation);
        case 'L': return xm_io_file_read_line(lua, file, continuation, tb_true);
        case 'n': xm_io_file_return_error(lua, file, "read number is not implemented");
        case 'l': return xm_io_file_read_line(lua, file, continuation, tb_false);
        default: 
            xm_io_file_return_error(lua, file, "unknonwn read mode");
            return 0;
        }
    }
    if (count >= 0) return xm_io_file_std_read_n(lua, file, continuation, count);
    switch (*mode)
    {
    case 'a': return xm_io_file_std_read_all(lua, file, continuation);
    case 'L': return xm_io_file_std_read_line(lua, file, continuation, tb_true);
    case 'n': return xm_io_file_std_read_num(lua, file, continuation);
    case 'l': return xm_io_file_std_read_line(lua, file, continuation, tb_false);
    default: 
        xm_io_file_return_error(lua, file, "unknonwn read mode");
        return 0;
    }
}
