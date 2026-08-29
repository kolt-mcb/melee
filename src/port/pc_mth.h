/**
 * @file pc_mth.h
 * @brief Native player for Nintendo MTH movies (MvOpen.mth and friends).
 *
 * On GameCube the movies are streamed off the disc by lbmthp.c and decoded by
 * the THP hardware decoder into three I8 planes (Y, Cb, Cr) that the TEV
 * combines back into RGB. None of that exists here: THPVideoDecode is a
 * console SDK entry point backed by hardware, and the streaming path runs on
 * DVD interrupts and OS alarms.
 *
 * So this file replaces the whole chain with a plain demuxer plus libjpeg.
 * An MTH file is a header followed by a singly-linked chain of chunks:
 *
 *     header  0x00 "MTHP"
 *             0x0C max chunk size   0x10 width      0x14 height
 *             0x18 frame rate       0x1C frame count
 *             0x20 offset of chunk 0  0x28 size of chunk 0
 *     chunk   0x00 u32 size of the NEXT chunk
 *             0x04 one baseline JPEG frame, padded to 32 bytes
 *
 * All values are big-endian. Chunk sizes only ever point forwards, so seeking
 * means walking the chain -- which is what the console did too.
 *
 * The one trap: THP-family JPEG omits the 0xFF 0x00 byte stuffing that the
 * JPEG standard requires inside entropy-coded data, because the hardware
 * decoder is told the length up front and never has to scan for markers.
 * libjpeg does scan, so it stops at the first raw 0xFF. pc_mth restores the
 * stuffing before handing the frame over.
 */
#ifndef PORT_PC_MTH_H
#define PORT_PC_MTH_H

/** Open a movie by disc filename ("MvOpen.mth"). `loop` restarts at the end.
 *  Returns 1 on success. Any previously open movie is closed first. */
int pc_mth_open(const char* filename, int loop);

/** Close the movie and release its buffers. Safe to call when not open. */
void pc_mth_close(void);

/** 1 while a movie is open. */
int pc_mth_active(void);

/** Frame count from the header, or 0 when no movie is open. */
int pc_mth_frame_count(void);

/** Decode frame `frame`, walking the chunk chain to reach it. Frames past the
 *  end either wrap (loop) or stick on the last frame and raise the finished
 *  flag. Returns 1 when a frame is ready to draw. */
int pc_mth_set_frame(int frame);

/** 1 once a non-looping movie has run past its last frame. */
int pc_mth_finished(void);

/** The current frame as tightly packed 24-bit RGB, or NULL if none is
 *  decoded. The buffer belongs to pc_mth and is valid until the next
 *  pc_mth_set_frame or pc_mth_close. */
const unsigned char* pc_mth_rgb(int* width, int* height);

#endif /* PORT_PC_MTH_H */
