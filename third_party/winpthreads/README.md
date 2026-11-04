# winpthreads (MinGW x86-64)

`libpthread.a` stammt aus dem offiziellen MSYS2-Paket
`mingw-w64-x86_64-winpthreads-14.0.0.r598.ge3d18511f-1` für mingw64 (MSVCRT).
Die SHA-256-Prüfsumme des Pakets ist
`d875a6a969f01cc64caeebd337430d00528964123ce29c02873bb5c6c3c914b8`,
die der Bibliothek `a035b2e3befbc0f8fe5e5cec46ac7c5a8911b170f1972fad503f49f7fc8176d6`.
Quelle: https://packages.msys2.org/packages/mingw-w64-x86_64-winpthreads

Der Lizenztext steht in `COPYING`. Die Datei `msvcrt-errno.def` ergänzt den
Import von `_set_errno`, der in manchen MinGW-Toolchains nicht enthalten ist.
