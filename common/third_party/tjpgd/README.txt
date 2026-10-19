TJpgDec R0.03, the Tiny JPEG Decompressor by ChaN (elm-chan.org/fsw/tjpgd/),
(C) ChaN 2021; its license is at the top of tjpgd.c (no restriction on use;
redistributions keep the notice). Taken from github.com/Bodmer/TJpg_Decoder
(71bfc26) with Bodmer's byte-swap option taken out again, so it is ChaN's
code, but for LDB_WORD, which reads its two bytes one at a time (GCC
merged them into a word read, an address error on a 68000 at an odd
address), and mcu_load at 1/8 scale, which reads the AC elements past
without clearing or de-quantizing them and stores only the DC value (all
that scale uses: the result is the same, several times faster on a
68000); tjpgdcnf.h is EContact's configuration (RGB888, scaling, table
Huffman decoding). EContact decodes contact photos with it (src/photo.c),
EWeb a page's JPEGs (common/src/pic.c). Moved here from EContact (19 Oct
2026): keep the two changes if it is ever updated.
