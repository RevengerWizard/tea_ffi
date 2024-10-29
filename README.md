# tea_ffi

[LuaJIT]: https://luajit.org/
[Teascript]: https://github.com/RevengerWizard/teascript
[libffi]: https://sourceware.org/libffi/

[cffi-lua]: https://github.com/q66/cffi-lua
[lua-ffi]: https://github.com/zhaojh329/lua-ffi

> [!WARNING]
> This is work-in progress. Due to current constraints in Teascript and C API, FFI resources aren't freed correctly

tea_ffi is a portable C FFI for [Teascript] based on [libffi] and an API design similar to that of [LuaJIT], but written using the Teascript C API.

It aims to be the standard FFI module interface to C and other future Teascript implementations.

This project was inspired by [cffi-lua] and [lua-ffi].

## Example

```tea
import ffi

// Define a C struct
ffi.cdef(```
typedef struct {
    int x;
    int y;
    const char* name;
} Point;
```)

const p = ffi.cnew("Point", {
    x = 10,
    y = 20,
    name = "origin"
})

print("Point: x=%d, y=%d, name=%s".format(p.x, p.y, ffi.string(p.name)))
```

## Basic types supported

Signed and unsigned platform-sized C types:

`void` `bool` `char` `short` `int` `long` `float` `double`

The C99 `<stdint.h>` types are also available to use:

`int8_t` `int16_t` `int32_t` `int64_t` `uint8_t` `uint16_t` `uint32_t` `uint64_t` `size_t`

## Building

Make sure to have the latest commit of [Teascript] compiled.

You may need to modify the Makefile to point to the correct library paths used by your system.

```bash
git clone https://github.com/RevengerWizard/tea_ffi && cd tea_ffi
make
```

## License

Licenced under MIT License. [Copy of the license can be found here](https://github.com/RevengerWizard/tea_ffi/blob/master/LICENSE)