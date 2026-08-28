# Minimal DjVu fixture

`minimal.djvu` is `lizard2002.djvu` from the upstream
[DjVuLibre source tree](https://github.com/DjVuLibre/DjVuLibre/blob/master/doc/lizard2002.djvu).
It is distributed with DjVuLibre under GPL-2.0-or-later, the same licence family
as the bundled DjVuLibre source in this repository.

The fixture is 25,697 bytes and contains two pages.  The long-path regression
copies it to a temporary Unicode path longer than `MAX_PATH`, passes the normal
non-prefixed path to libdjvu, and verifies that page zero can be decoded with
non-zero dimensions.
