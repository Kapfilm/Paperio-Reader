# Acknowledgements

The idea of showing the initial DC scan of a progressive JPEG as a low-resolution
preview was inspired by the progressive JPEG fallbacks in
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and
[FreeInkBook](https://github.com/freeink-project/freeink-sdk).

This decoder is an independent implementation of the marker parsing, canonical
Huffman decoding, DC prediction, restart handling, and grayscale reconstruction
described by the JPEG interchange format. It does not include JPEGDEC or
FreeInkBook decoder source code.
