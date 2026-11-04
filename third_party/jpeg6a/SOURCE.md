# Independent JPEG Group JPEG software, release 6a

This directory contains the unmodified library sources from
https://www.ijg.org/files/jpegsrc.v6a.tar.gz, release 6a (7 February 1996).
The source archive SHA-256 is
`0e963fdd29d5049f61f3dc137bdc9437473a0ad505dbcfc8925f0fcad46c5929`.
The upstream [README](README) contains the copyright, redistribution terms,
and acknowledgements. This software is based in part on the work of the
Independent JPEG Group.

`jconfig.h` is this project's flat-memory configuration; it is not an upstream
file. The library uses the upstream `jmemnobs.c` memory backend. The GIMEX
integration targets the 32-bit Windows ABI: its original DLL requests IJG
version 61 and 464-byte decompressor and 360-byte compressor records.
