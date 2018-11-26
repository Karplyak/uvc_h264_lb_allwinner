#ifndef CSC_H
#define CSC_H

#define uint8 unsigned char

void uyvy422toNV12(int width, int height, unsigned char *FrameIn, unsigned char *FrameOut);
void uyvy422to420(int width, int height, unsigned char *FrameIn, unsigned char *FrameOut);
void yuyv422toNV12(int width, int height, unsigned char *FrameIn, unsigned char *FrameOut);

int UYVYToNV12_neon(const uint8 *src_uyvy, int src_stride_uyvy,
               uint8 *dst_y, int dst_stride_y,
               uint8 *dst_uv, int dst_stride_uv,
               int width, int height);

int UYVYTo420P_neon(const uint8 *src_uyvy, int src_stride_uyvy,
               uint8 *dst_y, int dst_stride_y,
               uint8 *dst_u, int dst_stride_u,
               uint8 *dst_v, int dst_stride_v,
               int width, int height);

int YUYVToNV12_neon(const uint8 *src_uyvy, int src_stride_uyvy,
               uint8 *dst_y, int dst_stride_y,
               uint8 *dst_uv, int dst_stride_uv,
               int width, int height);

int YUYVTo420P_neon(const uint8 *src_uyvy, int src_stride_uyvy,
               uint8 *dst_y, int dst_stride_y,
               uint8 *dst_u, int dst_stride_u,
               uint8 *dst_v, int dst_stride_v,
               int width, int height);


#endif