#include <byteswap.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <linux/videodev2.h>

#include "video_device.h"

/*
 *
 */
void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(-1);
}

/*
 *
 */
int xioctl(int fh, int request, void *arg) {
    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

/*
 *
 */
int dev_try_format(int fd, int w, int h, int fmtid) {
    struct v4l2_format fmt;

    fmt.fmt.pix.width       = w;
    fmt.fmt.pix.height      = h;
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = fmtid;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (xioctl(fd, VIDIOC_TRY_FMT, &fmt) < 0) {
       return -1;
    }

    if(fmt.fmt.pix.pixelformat != fmtid) {
        return -1;
    }

    if (xioctl(fd, VIDIOC_S_FMT, &fmt)<0) {
        return -1;
    }
    return 0;
}

/*
 *
 */
void open_capture_dev(char *name, int *fd) { 
    int i;
    v4l2_std_id std_id;
    struct v4l2_capability cap;
    /* open device NONEBLOCK mode to avoid -EAGAIN and CPU load */
    *fd = open(name, O_RDWR /*| O_NONBLOCK*/, 0);
    if (-1 == *fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* get standard (wait for it to be locked onto a signal) */
    if (-1 == xioctl(*fd, VIDIOC_G_STD, &std_id))
        perror("VIDIOC_G_STD");

    for (i = 0; std_id == V4L2_STD_ALL && i < 10; i++) {
        usleep(100000);
        xioctl(*fd, VIDIOC_G_STD, &std_id);
    }
    /* set the standard to the detected standard (this is critical for autodetect) */
    if (std_id != V4L2_STD_UNKNOWN) {
        if (-1 == xioctl(*fd, VIDIOC_S_STD, &std_id))
            perror("VIDIOC_S_STD");
        if (std_id & V4L2_STD_NTSC)
            printf("found NTSC TV decoder\n");
        if (std_id & V4L2_STD_SECAM)
            printf("found SECAM TV decoder\n");
        if (std_id & V4L2_STD_PAL)
            printf("found PAL TV decoder\n");
    }

    /* ensure device has video capture capability */
    if (-1 == xioctl(*fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n",name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", name);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", name);
        exit(EXIT_FAILURE);
    }
}


/*
 *
 */
int setup_capture_device(char *name, int fd, int *w, int *h, int fps, int pix_format) {
    struct v4l2_streamparm parm;
    struct v4l2_format fmt;

/*
    // USB camera and so
    CLEAR(parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.denominator = fps;
    parm.parm.capture.timeperframe.numerator = 1;

    if (-1 == xioctl(fd, VIDIOC_S_PARM, &parm))
        perror("VIDIOC_S_PARM");
 */   

    /* get framerate */
    CLEAR(parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_G_PARM, &parm))
            perror("VIDIOC_G_PARM");

    /* set format */
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = *w;
    fmt.fmt.pix.height      = *h;
    fmt.fmt.pix.pixelformat = pix_format;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    /* get and display format */
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
            errno_exit("VIDIOC_G_FMT");

    // adjust height / width
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;    

    printf("Capture %s: %dx%d %c%c%c%c %2.2ffps\n", name,
            fmt.fmt.pix.width, fmt.fmt.pix.height,
            (fmt.fmt.pix.pixelformat >> 0) & 0xff,
            (fmt.fmt.pix.pixelformat >> 8) & 0xff,
            (fmt.fmt.pix.pixelformat >> 16) & 0xff,
            (fmt.fmt.pix.pixelformat >> 24) & 0xff,
            (float)parm.parm.capture.timeperframe.denominator /
            (float)parm.parm.capture.timeperframe.numerator
            );  
            
    return 0;          
}

/*
 *
 */
struct buffer *init_capt_mmap(char *name, int fd, int *nbuff) {
    int i;
    struct v4l2_requestbuffers req;
    struct buffer *pb;

    /* request buffers */
    CLEAR(req);
    req.count = V4L2MMAP_NBBUFFER;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", name);
        exit(EXIT_FAILURE);
    }

    /* allocate buffers */
    pb = calloc(req.count, sizeof(*pb));
    if (!pb) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    /* mmap buffers */
    for (*nbuff = 0; *nbuff < req.count; ++(*nbuff)) {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = *nbuff;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                errno_exit("VIDIOC_QUERYBUF");

        pb[*nbuff].length = buf.length;
        pb[*nbuff].start = mmap(NULL /* start anywhere */,
                                          buf.length,
                                          PROT_READ | PROT_WRITE /* required */,
                                          MAP_SHARED /* recommended */,
                                          fd, buf.m.offset);

        if (MAP_FAILED == pb[*nbuff].start)
            errno_exit("mmap");

        pb[*nbuff].dma_addr = buf.m.offset;    
    }

    /* queue buffers */
    for (i = 0; i < *nbuff; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");
    }

    return pb;
}

/*
*/
void open_out_dev(char *name, int w, int h, int mode, int *fd, int pix_format) {      
    struct v4l2_capability cap_2;
    struct v4l2_format fmt_2;

    *fd = open(name, O_RDWR | O_NONBLOCK);
        
    if (-1 == *fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    CLEAR(cap_2);

    if (-1 == xioctl(*fd, VIDIOC_QUERYCAP, &cap_2)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n",name);
            exit(EXIT_FAILURE);
        } else 
            errno_exit("VIDIOC_QUERYCAP");
    }
            
    if (!(cap_2.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        fprintf(stderr, "%s is no video capture device\n",name);
        exit(EXIT_FAILURE);
    }
    
    CLEAR(fmt_2);
        
    fmt_2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt_2.fmt.pix.width = w;
    fmt_2.fmt.pix.height = h;
    fmt_2.fmt.pix.pixelformat = pix_format;
    fmt_2.fmt.pix.field = V4L2_FIELD_ANY;
        
    if (-1 == xioctl(*fd, VIDIOC_S_FMT, &fmt_2)) {
        errno_exit("VIDIOC_S_FMT");
    }

    printf("Output %s: %dx%d %c%c%c%c\n", name,
            fmt_2.fmt.pix.width, fmt_2.fmt.pix.height,
            (fmt_2.fmt.pix.pixelformat >> 0) & 0xff,
            (fmt_2.fmt.pix.pixelformat >> 8) & 0xff,
            (fmt_2.fmt.pix.pixelformat >> 16) & 0xff,
            (fmt_2.fmt.pix.pixelformat >> 24) & 0xff
            );  
}

/*
*/
struct buffer *init_out_mmap(int *fd, int *nbuff) {
    unsigned int i;
    enum v4l2_buf_type type2;
    struct v4l2_requestbuffers rb2;
    struct buffer *pb;

    CLEAR(rb2);

    rb2.count = V4L2MMAP_NBBUFFER;
    rb2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb2.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(*fd, VIDIOC_REQBUFS, &rb2)) {
        if (EINVAL == errno) {
            fprintf(stderr, "does not support memory mapping\n");
            exit(EXIT_FAILURE);
        } else 
            errno_exit("VIDIOC_REQBUFS");

    }

    // allocate buffers
    pb = calloc(rb2.count, sizeof(*pb));

    if (!pb) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (*nbuff = 0; *nbuff < rb2.count; ++(*nbuff)) {
        struct v4l2_buffer buff2;

        CLEAR(buff2);

        buff2.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buff2.memory      = V4L2_MEMORY_MMAP;
        buff2.index       = *nbuff;

        if (-1 == xioctl(*fd, VIDIOC_QUERYBUF, &buff2))
                errno_exit("VIDIOC_QUERYBUF");

        pb[*nbuff].length = buff2.length;
        pb[*nbuff].start = mmap(NULL,
                                      buff2.length,
                                      PROT_READ | PROT_WRITE /* required */,
                                      MAP_SHARED /* recommended */,
                                      *fd, buff2.m.offset);

        if (MAP_FAILED == pb[*nbuff].start)
                errno_exit("mmap");

        pb[*nbuff].dma_addr = buff2.m.offset;    
    }
    
    // queue buffers   
    for (i = 0; i < *nbuff; ++i) {
        struct v4l2_buffer buff2;

        CLEAR(buff2);
        buff2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buff2.memory = V4L2_MEMORY_MMAP;
        buff2.index = i;
        if (-1 == xioctl(*fd, VIDIOC_QBUF, &buff2))
            errno_exit("VIDIOC_QBUF");
    }

    type2 = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (-1 == xioctl(*fd, VIDIOC_STREAMON, &type2))
        errno_exit("VIDIOC_STREAMON");   

    return pb;             
}

/*
*/
void uninit_out_mmap(int fd, struct buffer *pb, int nbuf) {
    unsigned int i;
    enum v4l2_buf_type type2;
    struct v4l2_requestbuffers rb2;
    
    type2 = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type2))
            errno_exit("VIDIOC_STREAMOFF");
    
    for (i = 0; i < nbuf; ++i) {
        if (-1 == munmap(pb[i].start, pb[i].length))
            errno_exit("munmap");
    }
    
    // free buffers

    CLEAR(rb2);
    
    rb2.count = 0;
    rb2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb2.memory = V4L2_MEMORY_MMAP;
        
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &rb2))
        errno_exit("VIDIOC_REQBUFS");
        
    nbuf = 0;
        
    free(pb);
}

/*
 *
 */
void wrt_to_lpbck (int fd, unsigned char* data, int size_out, int nbuf, struct buffer *pb) {       
    int size = 0;
    if (nbuf > 0) {      
        struct v4l2_buffer buff2;
        CLEAR(buff2);
        buff2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buff2.memory = V4L2_MEMORY_MMAP;
                
        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buff2)) {
                errno_exit("VIDIOC_DQBUF");
        } else if (buff2.index < nbuf) {
            buff2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            size = size_out;
            if (size > buff2.length)
            {
                fprintf(stderr, "Device %s buffer truncated available: %d needed: %d \n", "/dev/video2", buff2.length, size);
                size = buff2.length;
            }

            memcpy(pb[buff2.index].start, data, size);

            buff2.bytesused = size;     
            
            if (-1 == xioctl(fd, VIDIOC_QBUF, &buff2))
                    errno_exit("VIDIOC_QBUF");
        }
    }    
}

/*
 *
 */
void *obtain_lbck_current_input_buf(int fd, int nbuf, struct buffer *pb, struct v4l2_buffer *buff) {
    unsigned char *retval = NULL;

    buff->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    buff->memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_DQBUF, buff)) {
                errno_exit("VIDIOC_DQBUF");
    } else if (buff->index < nbuf) {
        buff->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        retval = pb[buff->index].start;
    }
    return (void *)retval;
}

/*
 *
 */
void write_current_input_buf_to_lbck(int fd, struct v4l2_buffer *buff, int size_out) {
    if (size_out > buff->length) 
        size_out = buff->length;

    buff->bytesused = size_out; 

     if (-1 == xioctl(fd, VIDIOC_QBUF, buff))
        errno_exit("VIDIOC_QBUF");
}


#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)
#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)

/*
 *
 */
int init_mod(char *mod_name, char *arg) {
    const char *params;
    int fd;
    size_t image_size;
    struct stat st;
    void *image;

    params = arg;
    fd = open(mod_name, O_RDONLY);
    fstat(fd, &st);
    image_size = st.st_size;
    image = malloc(image_size);
    read(fd, image, image_size);
    close(fd);
    if (init_module(image, image_size, params) != 0) {
        free(image);
        perror("init module");
        return EXIT_FAILURE;
    }
    free(image);
    return EXIT_SUCCESS;
}

/*
 *
 */
int remove_mod(char *mod_name) {
   
    if (delete_module(mod_name, O_NONBLOCK) != 0) {
        perror("delete_module");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
