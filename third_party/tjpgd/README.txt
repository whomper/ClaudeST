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

Changed for the ST's 68000 (20 Oct 2026; JD_FORMAT 0, JD_USE_SCALE and
JD_FASTDECODE >= 1 only, the configuration here):
- block_idct is the AAN ("IFAST") transform in 16-bit arithmetic, each
  product a muls.w (IMUL), with the descale rounded (+16 >> 5); the
  quantisers are scaled by Aan[] into 16-bit multipliers once, in
  create_qt_tbl, which keeps the raw ones too (qtraw).
- At 1/2 and 1/4 scale mcu_load folds each coefficient into a 4x4 or 2x2
  block (Fold2, Fold4, multipliers from make_qtred: Red2, Red4) and
  idct4 / idct2 transform that: the exact average of each 2x2 or 4x4
  square of the full block, without the 8x8 transform.
- mcu_output makes RGB from Y, Cb and Cr with tables (CrR, CbB, CrG, CbG,
  made once), each sample's chroma terms once for the pixels sharing it.
Measured (PICBENCH.TTP in EWeb, Hatari's cycle-exact 8 MHz ST, a 480x320
photo shown at 192x128): 18.1 s to decode before, 12.8 s after; the
error against libjpeg-turbo's exact decode is smaller than before
(common/tests/pic: 4:2:0 mean 2.64, was 2.83). Keep these changes too.
