[![Build Status:master](https://api.travis-ci.org/cyrusimap/zeroskip.svg?branch=master)](https://travis-ci.org/cyrusimap/zeroskip)

# Zeroskip

Zeroskip is an append only key-value database format. This repository
provides the library to create and use Zeroskip databases.

## Build and Install

On Unix-like systems (including Linux and Mac OS X), use the autotools
build system. To configure, build and install, run the following commands in the
top-level directory:

```
    $ ./configure
    $ make
    $ make install
```

If you've downloaded the source directly from the git source
repository, create the configure script by running `autoreconf` first:

```
    $ autoreconf --install
```

To uninstall `libzeroskip`, run:

```
    $ make uninstall
```

### Dependencies
Zeroskip needs `libz`(zlib) for crc32 functions and `libuuid` for
uuid_* functions. The build system uses `pkg-config` to determine the
`libs` and `include` paths. So please ensure that the the
`PKG_CONFIG_PATH` contains the right path. 

On `Solaris`(`Triton`) it is: `PKG_CONFIG_PATH=/opt/local/lib/pkgconfig`.

### Licence

Zeroskip is free software, released under the MIT licence as specified
in the LICENSE file.

