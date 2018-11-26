#ifndef VIDEO_DEVICE_H
#define VIDEO_DEVICE_H

/* video buffer structure */
struct buffer {
    void   *start;
    size_t  length;
    unsigned int dma_addr;
};

enum {
    SIMPLE_LB = 0,
    H264_LB,
    H263_LB,
} open_mode_t;


#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define V4L2MMAP_NBBUFFER 4

void open_capture_dev(char *name, int *fd);
int setup_capture_device(char *name, int fd, int *w, int *h, int fps, int pix_format);
struct buffer *init_capt_mmap(char *name, int fd, int *nbuff);
int xioctl(int fh, int request, void *arg);
void errno_exit(const char *s);
int dev_try_format(int fd, int w, int h, int fmtid);
void open_out_dev(char *name, int w, int h, int mode, int *fd, int pix_format);
struct buffer *init_out_mmap(int *fd, int *nbuff);
void uninit_out_mmap(int fd, struct buffer *pb, int nbuf);
void wrt_to_lpbck (int fd, unsigned char* data, int size_out, int nbuf, struct buffer *pb);
void *obtain_lbck_current_input_buf(int fd, int nbuf, struct buffer *pb, struct v4l2_buffer *buff);
void write_current_input_buf_to_lbck(int fd, struct v4l2_buffer *buff, int size_out);
unsigned int fourcc(char a, char b, char c, char d);
int init_mod(char *mod_name, char *arg);
int remove_mod(char *mod_name);

#endif
