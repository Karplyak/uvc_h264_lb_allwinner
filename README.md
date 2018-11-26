# uvc_h264_lb_allwinner

This software is the simple program for capturing frames from the UVC device, encode them using Alwinner's harware H264 encoder and place to the loopback video device.

One can use it for streaming software based on Allwiner A20, A33, H3 and others. Currently this software was tested on A20 CubieBoard2.

#### How it works:
* Capture a frame from the UVC device
* Perform color space conversion if needed
* Encode to H264 bitstream
* Place it to the v4l2loopback device

#### What color space conversion is needed for:
Color space conversion is needed for Allwinner's VE (VPU). It takes only [NV12](https://wiki.videolan.org/YUV/#NV12) and NV16 color formats as input. As long as UVC capture device has YUV422 output (YUYV or UYVY), the frame must be converted to NV12. There are used 2 approaches: CPU and NEON. The second one is much faster if your CPU has ARM SIMD.

Color space conversion from 422 to [I420](https://wiki.videolan.org/YUV/#I420) is used for the other software like OpenH264 encoder or linphone/pjsip using it as input.

#### What is needed to make it working:
* [mainline linux kernel 4.19](https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.19.4.tar.xz)
* [cedar_dev kernel module for the mainline kernel (4.19)](https://github.com/uboborov/sunxi-cedar-mainline.git) 
* [v4l2loopback kernel module](https://github.com/umlaeute/v4l2loopback.git)

After compiling **v4l2loopback** must be placed into /usr/lib/v4l2loopback.ko. Kernel module **sunxi_cedar.ko** you're free to place anywhere you want.

#### What should be done before building the app:
* Before the building you should set the correct path to your compiler in the Makefile
* Define the NEON option
* Edit **main.c** and specify the number of loopback devices and their names (/dev/videoN). Default settings are /dev/video3 for YUV420P and /dev/video4 for H264 
* Define USE_FPS_MEASUREMENT if you want to see fps output to stdout
* Set .tofile = 1 and specify .fname = "some_file.mkv" if you want to store loopback output to the file

#### How to build it:
Just run `make` command in the source dir

#### How to run it:
* Load cedrus driver - `insmod sunxi_cedar.ko`
* Run application - `./h264enc -v /dev/videoN -w [WIDTH] -h [HEIGHT] -f [PIXEL FORMAT]`
* * -v - UVC video input device for capturing (usb webcam or DVR or so)
  * -w - frame width
  * -h - frame height
  * -f - pixel format. Default value UYVY. Supported values: YUYV and UYVY
  
*The app loads and unloads loopback driver (/usr/lib/v4l2loopback.ko) automatically at start

#### How to use it:
The app could be used with streaming software. It was successfully tested with [v4l2rtspserver](https://github.com/mpromonet/v4l2rtspserver.git)
* Run the app: `h264enc -v /dev/video0 -w 640 -h 480 -f UYVY &`
* Run the v4l2rtspserver: `v4l2rtspserver /dev/video4 -W 720 -H 480 -F 25 -P 8554 -u live.cam2`

To capture RTSP H264 stream on the other side run `mpv rtsp://Your_board_IP_address/live.cam2` 


