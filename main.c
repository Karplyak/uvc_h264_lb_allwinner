/*
 * Copyright (c) 2014 Jens Kuske <jenskuske@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

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

#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <linux/videodev2.h>

#include "ve.h"
#include "video_device.h"
#include "h264enc.h"
#include "csc.h"

#define USE_V4L_DEV
//#define USE_FPS_MEASUREMENT

#define DEF_VIDEO_DEV 	"/dev/video0"
#define DEF_VIDEO_H		640
#define DEF_VIDEO_W		480
#define DEF_PIX_FMT		"UYVY"

#define LB_DRV_NAME 	"v4l2loopback"
#define LB_NAME_OFFSET	3 // starts with /dev/videoN(offset)

static char VIDEO_DEV[20] = DEF_VIDEO_DEV;


#define N_LB_DEV    2
/* graphic thread init struct */
static struct pthr_start {
    char *lb_name;
    int lb_codec;
    int lb_fd;
    int lb_nbuf;
    int lb_w;
    int lb_h;
    void *lb_pbuf;
    int tofile;
    int file_fd;
    char *fname;
    int pix_format;
} th_start[] = {
    {
        .lb_name = "/dev/video3",
        .lb_codec = SIMPLE_LB,
        .lb_w = -1,
        .lb_h = -1,
        .tofile = 0,
        .file_fd = -1,
        .pix_format = V4L2_PIX_FMT_YUV420,
    },
    {
        .lb_name = "/dev/video4",
        .lb_codec = H264_LB,
        .lb_w = -1,
        .lb_h = -1,
        .tofile = 1,
        .file_fd = -1,
        .fname = "out_sunxi_tst.mkv",
        .pix_format = V4L2_PIX_FMT_H264,
    }
};

#ifdef USE_FPS_MEASUREMENT
# ifndef MAX
#  define MAX(x, y) ( ((x)>(y))?(x):(y) )
# endif

# ifndef MIN
#  define MIN(x, y) ( ((x)<(y))?(x):(y) )
# endif
/*
*/
unsigned long int time_diff(struct timespec *ts1, struct timespec *ts2) {
    static struct timespec ts;
    ts.tv_sec = MAX(ts2->tv_sec, ts1->tv_sec) - MIN(ts2->tv_sec, ts1->tv_sec);
    ts.tv_nsec = MAX(ts2->tv_nsec, ts1->tv_nsec) - MIN(ts2->tv_nsec, ts1->tv_nsec);

    if (ts.tv_sec > 0) {
        ts.tv_sec--;
        ts.tv_nsec += 1000000000;
    }

    return((ts.tv_sec * 1000000000) + ts.tv_nsec);
}
#endif

/*
 *
 */
static int read_frame(int fd, void *buffer, int size) {
	int total = 0, len;
	while (total < size)
	{
		len = read(fd, buffer + total, size - total);
		if (len == 0)
			return 0;
		total += len;
	}
	return 1;
}

/*
 *
 */
int main(const int argc, const char **argv) {
	int nframes_ps = 0;
	int in = -1, out = -1;
	int size_out;
	char input_file[50] = "";
	char output_file[50] = "";
	int height, width;
	int video_fd;
	struct buffer *buffers;
	static int n_buffers;
	/* V4L2 */
	enum v4l2_buf_type type;
	struct v4l2_buffer buf;
	int i, cnt;
	char mod_param[128];
	int opt;
	int cap_dev_pix_fmt =  v4l2_fourcc(DEF_PIX_FMT[0], DEF_PIX_FMT[1], DEF_PIX_FMT[2], DEF_PIX_FMT[3]);

	width = DEF_VIDEO_W;
	height = DEF_VIDEO_H;

	while ((opt = getopt(argc, argv, "v:i:o:w:h:f:")) != -1) {
        switch (opt) {
            case 'v':
                strcpy(VIDEO_DEV, optarg);
                break;
            case 'i':
                strcpy(input_file, optarg);
                break;
            case 'o':
                strcpy(output_file, optarg);
                break;
            case 'w':
                width = atoi(optarg);
                break;   
            case 'h':
                height = atoi(optarg);
                break; 
            case 'f':
                cap_dev_pix_fmt = v4l2_fourcc(optarg[0], optarg[1], optarg[2], optarg[3]);
                break;             
                    
            default:
                printf("Usage: %s -v videodev -i input file -o output file -w width -h height -f format\n", argv[0]);
                exit(0);
                break;    
        }
    }

    if (strlen(input_file) > 0) {
	    if (strcmp(input_file, "-") != 0) {
			if ((in = open(argv[1], O_RDONLY)) == -1) {
				printf("could not open input file\n");
				return EXIT_FAILURE;
			}
		} else {
			in = 0;
		}
	}

	if (strlen(output_file) > 0) {
		if ((out = open(argv[4], O_CREAT | O_RDWR | O_TRUNC,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
			printf("could not open output file\n");
			return EXIT_FAILURE;
		}
	}

#if defined(USE_V4L_DEV)
	open_capture_dev(VIDEO_DEV, &video_fd);
	if (video_fd < 0) {
		errno_exit("video device");
	}

	if (dev_try_format(video_fd,  width, height, cap_dev_pix_fmt)) {
		printf("Incompattible capture pixel format!\n");
		close(video_fd);
        goto app_exit;
	}

    setup_capture_device(VIDEO_DEV, video_fd, &width, &height, 30, cap_dev_pix_fmt);
    buffers = init_capt_mmap(VIDEO_DEV, video_fd, &n_buffers);
#endif	

	struct h264enc_params params;
	params.src_width = (width + 15) & ~15;
	params.width = width;
	params.src_height = (height + 15) & ~15;
	params.height = height;
	params.src_format = H264_FMT_NV12;
	params.profile_idc = 77;
	params.level_idc = 41;
	params.entropy_coding_mode = H264_EC_CABAC;
	params.qp = 24;
	params.keyframe_interval = 25;
	params.work_mode = ENC_MODE_STREAMING;

	if (!ve_open()) {
		printf("Failed to open CedarX device %s\n", "/dev/cedar_dev");
		return EXIT_FAILURE;
	}

	h264enc *encoder = h264enc_new(&params);

	if (encoder == NULL) {
		printf("could not create encoder\n");
		goto err;
	} else {
		printf("H264 encoder initialized: %dx%d\n", width, height);
	}

	void* output_buf = h264enc_get_bytestream_buffer(encoder);

	int input_size = params.src_width * (params.src_height + params.src_height / 2);
	void* input_buf = h264enc_get_input_buffer(encoder);
	size_out = ((width) * (height) * 12 / 8);

	if (in > 0 && out > 0) {
		printf("Runnig h264 encoding from file %s...\n", input_file);
		while (read_frame(in, input_buf, input_size)) {
			if (h264enc_encode_picture(encoder)) {
				write(out, output_buf, h264enc_get_bytestream_length(encoder));
			} else {
				printf("encoding error\n");
			}
		}
		printf("Done!\n");
		goto complete;
	}

#if defined(USE_V4L_DEV)
	printf("Runnig h264 encoding from V4L device %s...\n", VIDEO_DEV);
	// rmmod
    remove_mod(LB_DRV_NAME);
    cnt = sprintf(mod_param, "video_nr=");
    for (i = 0;i < N_LB_DEV;i++) {
        if (i != 0) {
            strcat(mod_param, ",");
            cnt++;
        }
        cnt += sprintf(mod_param + cnt, "%d", i + LB_NAME_OFFSET);
    }
    // insmod
    init_mod("//usr//lib//"LB_DRV_NAME".ko", mod_param);

    for (i = 0;i < N_LB_DEV;i++) {
    	th_start[i].lb_w = width;
        th_start[i].lb_h = height;
    	open_out_dev(th_start[i].lb_name, width, height, th_start[i].lb_codec, &th_start[i].lb_fd, th_start[i].pix_format);
    	th_start[i].lb_pbuf = init_out_mmap(&th_start[i].lb_fd, &th_start[i].lb_nbuf);

    	if (!th_start[i].lb_pbuf) {
            printf("No buffers for %s\n", th_start[i].lb_name);
            exit(EXIT_FAILURE);
        }

        /* open the file for writing codec bitstream */
        if (th_start[i].tofile == 1) {
            th_start[i].file_fd = open(th_start[i].fname, O_WRONLY | O_CREAT | O_TRUNC, 0755);
            if (-1 == th_start[i].file_fd) {
                printf("Failed to open file for writing %s\n", th_start[i].fname);
                exit(EXIT_FAILURE);
            }
        }
    }

    /* start capture */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");

    
    while (1) {
        fd_set fds;
        struct timeval tv;
        int r;
#ifdef USE_FPS_MEASUREMENT
        struct timespec tm[2];
        clock_gettime(CLOCK_MONOTONIC, &tm[0]);
#endif        

        FD_ZERO(&fds);
        FD_SET(video_fd, &fds);

        /* Timeout. */
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        r = select(video_fd + 1, &fds, NULL, NULL, &tv);
        if (-1 == r) {
            if (EINTR == errno)  
                continue;
            
            errno_exit("select");
        }
        if (0 == r) {
            fprintf(stderr, "select timeout\n");
            exit(EXIT_FAILURE);
        }

        /* dequeue captured buffer */
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (-1 == xioctl(video_fd, VIDIOC_DQBUF, &buf)) {
            if (errno == EAGAIN) {
                //printf("EAGAIN\n");
                continue;
            }
            errno_exit("VIDIOC_DQBUF");
        }
        assert(buf.index < n_buffers);

        for (i = 0;i < N_LB_DEV;i++) {
        	int len = 0;
        	void *pb = NULL;

        	if (th_start[i].lb_codec == H264_LB) {  
#if defined(CPU_HAS_NEON) 		        	
        			int src_stride = width*2;
        			int dst_stride_y = width;
        			int dst_stride_uv = width;
        			int uv_offset = width*height;
#endif        			        	 
        		if (cap_dev_pix_fmt == V4L2_PIX_FMT_UYVY) {		
#if defined(CPU_HAS_NEON) 		        	
        			UYVYToNV12_neon(buffers[buf.index].start, src_stride,
		               		   input_buf, dst_stride_y,
		               		   input_buf + uv_offset, dst_stride_uv,
		                       width, height);
#else
        			uyvy422toNV12(width, height, buffers[buf.index].start, input_buf);
#endif        			
		    	} else if (cap_dev_pix_fmt == V4L2_PIX_FMT_YUYV) {
#if defined(CPU_HAS_NEON) 
		    		YUYVToNV12_neon(buffers[buf.index].start, src_stride,
		               		   input_buf, dst_stride_y,
		               		   input_buf + uv_offset, dst_stride_uv,
		                       width, height);
#else		    		
		    		yuyv422toNV12(width, height, buffers[buf.index].start, input_buf);
#endif
		    	} else {
		    		continue;
		    	}
   		
        		if (h264enc_encode_picture(encoder)) {       			
					len = h264enc_get_bytestream_length(encoder);
					pb = output_buf;
				} 	

				wrt_to_lpbck(th_start[i].lb_fd, 
							  pb,
							  len, 
							  n_buffers, 
							  th_start[i].lb_pbuf);

        	} else if (th_start[i].lb_codec == SIMPLE_LB) {
        		struct v4l2_buffer dev_ibuf;
    			CLEAR(dev_ibuf);
        		pb = obtain_lbck_current_input_buf(th_start[i].lb_fd, n_buffers, th_start[i].lb_pbuf, &dev_ibuf);

        		if (th_start[i].pix_format == cap_dev_pix_fmt) {
        			len = buf.bytesused;
        			memcpy(pb, buffers[buf.index].start, len);
        		} else {
#if defined(CPU_HAS_NEON)        			
        			int src_stride = width*2;
        			int dst_stride_y = width;
        			int dst_stride_uv = width/2;
        			int u_offset = width*height;
        			int v_offset = u_offset + (u_offset/4);

        			UYVYTo420P_neon(buffers[buf.index].start, src_stride,
		               		   pb, dst_stride_y,
		               		   pb + u_offset, dst_stride_uv,
		               		   pb + v_offset, dst_stride_uv,
		                       width, height);

        		}
#else 
        		uyvy422to420(width, height, buffers[buf.index].start, pb);
#endif
				len = size_out;				
				write_current_input_buf_to_lbck(th_start[i].lb_fd, &dev_ibuf, len);
			}
			
			if (th_start[i].tofile == 1) {
				write(th_start[i].file_fd, pb, len);
			}
        }

#ifdef USE_FPS_MEASUREMENT
            /* measure fps of the capture device */
            clock_gettime(CLOCK_MONOTONIC, &tm[1]);

            int dt_ms = (time_diff(&tm[0], &tm[1])) / 1000000;
            if (dt_ms >= 1000) {
                printf("CAPTURE FPS: %d\n",nframes_ps);
                nframes_ps = 0;
            }
#endif
        /* queue buffer */
        if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");

        nframes_ps++;
    }

	printf("Done!\n");
	for (i = 0;i < N_LB_DEV;i++) {
        uninit_out_mmap(th_start[i].lb_fd, th_start[i].lb_pbuf, th_start[i].lb_nbuf);
        
        if (th_start[i].tofile == 1) {
            close(th_start[i].file_fd);
        }
    }
#endif	

complete:
	h264enc_free(encoder);

err:
	ve_close();
app_exit:    
	close(out);
	close(in);

	return EXIT_SUCCESS;
}
