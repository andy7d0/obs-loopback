#include <obs-module.h>

/*******************
	service
******************/

struct loopback {
	char *url;
};

static const char *loopback_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("LoopbackStreamingServer");
}

static void loopback_update(void *data, obs_data_t *settings)
{
	struct loopback *service = (loopback *)data;

	bfree(service->url);

	service->url = bstrdup(obs_data_get_string(settings, "url"));
}

static void loopback_destroy(void *data)
{
	struct loopback *service = (loopback *)data;

	bfree(service->url);
	bfree(service);
}

static void *loopback_create(obs_data_t *settings, obs_service_t *service)
{
	struct loopback *data = (loopback *)bzalloc(sizeof(struct loopback));
	loopback_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

static obs_properties_t *loopback_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_text(ppts, "url", "video:audio", OBS_TEXT_DEFAULT);

	return ppts;
}

static const char *loopback_url(void *data)
{
	struct loopback *service = (loopback *)data;
	return service->url;
}

static const char *loopback_key(void *data)
{
	UNUSED_PARAMETER(data);
//	struct loopback *service = data;
	return "";
}

static const char* loopback_output_type(void* data) {
	UNUSED_PARAMETER(data);
	return "LoopbackSink";
}

struct obs_service_info loopback_service = {
	.id = "loopback",
	.get_name = loopback_name,
	.create = loopback_create,
	.destroy = loopback_destroy,
	.update = loopback_update,
	.get_properties = loopback_properties,
	.get_url = loopback_url,
	.get_key = loopback_key,
	.get_output_type = loopback_output_type,
};


/***************
output
****************/

/******************************************************************************
    obs-v4l2sink
    Copyright (C) 2018 by CatxFish
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/


#include <obs-frontend-api.h>
#include <obs-module.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <string.h>
#include <errno.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>

#define V4L2SINK_SUCCESS_OPEN  0
#define V4L2SINK_ERROR_OPEN    1
#define V4L2SINK_ERROR_FORMAT  2
#define V4L2SINK_ERROR_OTHER   3


struct LoopbackSink_data{
	obs_output_t *output = nullptr;
	bool active = false;
	int v4l2_fd = 0;
	int width = 0;
	int height = 0;
	int frame_size = 0;
	uint32_t format = V4L2_PIX_FMT_YUYV;
	pa_simple* pa = nullptr;
	pa_sample_spec ss;
	size_t audioframe_size = 0;
	int audio_fd = 0;
};

static inline enum video_format v4l2_to_obs_video_format(uint_fast32_t format)
{
	switch (format) {
	case V4L2_PIX_FMT_YVYU:   return VIDEO_FORMAT_YVYU;
	case V4L2_PIX_FMT_YUYV:   return VIDEO_FORMAT_YUY2;
	case V4L2_PIX_FMT_UYVY:   return VIDEO_FORMAT_UYVY;
	case V4L2_PIX_FMT_NV12:   return VIDEO_FORMAT_NV12;
	case V4L2_PIX_FMT_YUV420: return VIDEO_FORMAT_I420;
	case V4L2_PIX_FMT_YVU420: return VIDEO_FORMAT_I420;
#ifdef V4L2_PIX_FMT_XBGR32
	case V4L2_PIX_FMT_XBGR32: return VIDEO_FORMAT_BGRX;
#endif
	case V4L2_PIX_FMT_BGR32:  return VIDEO_FORMAT_BGRA;
#ifdef V4L2_PIX_FMT_ABGR32
	case V4L2_PIX_FMT_ABGR32: return VIDEO_FORMAT_BGRA;
#endif
	default:                  return VIDEO_FORMAT_NONE;
	}
}

#define V4L2SINK_NV12   "NV12"
#define V4L2SINK_YUV420 "YUV420"
#define V4L2SINK_YUY2   "YUY2"
#define V4L2SINK_RGB32  "RGB32"


static inline uint32_t string_to_v4l2_format(const char* format)
{
	if(strcmp(format, V4L2SINK_NV12)==0)
		return V4L2_PIX_FMT_NV12;
	else if(strcmp(format, V4L2SINK_YUV420)==0)
		return V4L2_PIX_FMT_YUV420;
	else if (strcmp(format, V4L2SINK_RGB32)==0)
		return V4L2_PIX_FMT_BGR32;
	else
		return V4L2_PIX_FMT_YUYV;
}


bool v4l2device_set_format(void *data,struct v4l2_format *format)
{
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	format->fmt.pix.width = out_data->width;
	format->fmt.pix.height = out_data->height;
	format->fmt.pix.pixelformat = out_data->format;
	format->fmt.pix.sizeimage = out_data->frame_size;
	return true;
}

int v4l2device_framesize(void *data)
{	
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	switch(out_data->format){
	
	case V4L2_PIX_FMT_YVYU:   
	case V4L2_PIX_FMT_YUYV:   
	case V4L2_PIX_FMT_UYVY: 
		return out_data->width * out_data->height * 2;
	case V4L2_PIX_FMT_YUV420: 
	case V4L2_PIX_FMT_YVU420:
		return out_data->width * out_data->height * 3 / 2;
#ifdef V4L2_PIX_FMT_XBGR32
	case V4L2_PIX_FMT_XBGR32:
#endif
#ifdef V4L2_PIX_FMT_ABGR32
	case V4L2_PIX_FMT_ABGR32: 
#endif
	case V4L2_PIX_FMT_BGR32:
		return out_data->width * out_data->height * 4;				
	}
	return 0;
}

static enum audio_format pulse_to_obs_audio_format(pa_sample_format_t format)
{
	switch (format) {
	case PA_SAMPLE_U8:
		return AUDIO_FORMAT_U8BIT;
	case PA_SAMPLE_S16LE:
		return AUDIO_FORMAT_16BIT;
	case PA_SAMPLE_S32LE:
		return AUDIO_FORMAT_32BIT;
	case PA_SAMPLE_FLOAT32LE:
		return AUDIO_FORMAT_FLOAT;
	default:
		return AUDIO_FORMAT_UNKNOWN;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

static enum speaker_layout
pulse_channels_to_obs_speakers(uint_fast32_t channels)
{
	switch (channels) {
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	}

	return SPEAKERS_UNKNOWN;
}



int v4l2device_open(void *data)
{
	int error;

	printf("open LOOPBACK\n");
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	
	struct v4l2_format v4l2_fmt;
	int width,height,ret = 0;
	struct v4l2_capability capability;
	enum video_format format;
	
	obs_service_t *service = obs_output_get_service(out_data->output);
	if (!service)
		return false;
		
	video_t *video = obs_output_video(out_data->output);
	audio_t *audio = obs_output_audio(out_data->output);

	char *url = bstrdup(obs_service_get_url(service));
	char* videodev = strtok(url,":");
	char* audiodev = strtok(NULL,":");

	printf("LOOPBACK: %s %s %s\n", url, videodev, audiodev);
	
	out_data->ss.format = PA_SAMPLE_S16LE;
	out_data->ss.rate = 44100;
	out_data->ss.channels = 1;
	
	out_data->audioframe_size = pa_frame_size(&out_data->ss);
	
	struct audio_convert_info aci = {
	        .samples_per_sec = out_data->ss.rate,
		.format = pulse_to_obs_audio_format(out_data->ss.format),
		.speakers = pulse_channels_to_obs_speakers(out_data->ss.channels),
	};
	obs_output_set_audio_conversion(out_data->output, &aci);

	out_data->pa = NULL;
	out_data->v4l2_fd = open(videodev, O_RDWR);

	if(audiodev[0] == '/') {
		out_data->audio_fd = open(audiodev, O_RDWR);
		if(out_data->audio_fd < 0) {
			printf("audio device open fail\n");
			return V4L2SINK_ERROR_OPEN;
		}
	} else {
		pa_buffer_attr pa_buff = {
			.maxlength =  (uint32_t) -1,
			.tlength =  (uint32_t) pa_usec_to_bytes(20*1000,&out_data->ss),
			.prebuf =  (uint32_t) -1,
			.minreq =  (uint32_t) -1,
			.fragsize =  (uint32_t) -1,
		};
		out_data->pa = pa_simple_new(NULL, "OBS-stream"
			, PA_STREAM_PLAYBACK
			, audiodev
			, "OBS_playback", &out_data->ss, NULL, &pa_buff, &error);
	}

	bfree(url);

	if (!out_data->pa && !out_data->audio_fd)
	{
		printf("pa_simple_new() failed: %s\n", pa_strerror(error));
		return V4L2SINK_ERROR_OPEN;
	}

	out_data->format = V4L2_PIX_FMT_YUV420;

	out_data->frame_size = v4l2device_framesize(data);	

	if(out_data->v4l2_fd  < 0){
		printf("v4l2 device open fail\n");
		return V4L2SINK_ERROR_OPEN;
	}

	if (ioctl(out_data->v4l2_fd, VIDIOC_QUERYCAP, &capability) < 0){ 
		printf("v4l2 device qureycap fail\n");		
		return V4L2SINK_ERROR_FORMAT;
	}

	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(out_data->v4l2_fd, VIDIOC_G_FMT, &v4l2_fmt);

	if(ret<0){		
		printf("v4l2 device getformat fail\n");
		return V4L2SINK_ERROR_FORMAT;
	}

	v4l2device_set_format(data,&v4l2_fmt);
	ret = ioctl(out_data->v4l2_fd, VIDIOC_S_FMT, &v4l2_fmt);

	if(ret<0){		
		printf("v4l2 device setformat fail\n");
		return V4L2SINK_ERROR_FORMAT;
	}

	ret = ioctl(out_data->v4l2_fd, VIDIOC_G_FMT, &v4l2_fmt);

	if(ret<0){		
		printf("v4l2 device getformat fail\n");
		return V4L2SINK_ERROR_FORMAT;
	}

	if(out_data->format != v4l2_fmt.fmt.pix.pixelformat){
		printf("v4l2 format not support\n");
		return V4L2SINK_ERROR_FORMAT;
	}


	width = (int32_t)obs_output_get_width(out_data->output);
	height = (int32_t)obs_output_get_height(out_data->output);
	format = v4l2_to_obs_video_format(v4l2_fmt.fmt.pix.pixelformat);

	if(format == VIDEO_FORMAT_NONE){
		printf("v4l2 conversion format not support\n");
		return V4L2SINK_ERROR_FORMAT;
	}
	
	if(width!= v4l2_fmt.fmt.pix.width ||
	height!= v4l2_fmt.fmt.pix.height ||
	format!= video_output_get_format(video)){
		struct video_scale_info conv;
		conv.format = format;
		conv.width = v4l2_fmt.fmt.pix.width;	
		conv.height = v4l2_fmt.fmt.pix.height;
		out_data->frame_size = v4l2_fmt.fmt.pix.sizeimage;
		obs_output_set_video_conversion(out_data->output,&conv);
	}
	else
		obs_output_set_video_conversion(out_data->output,NULL);
		
	return V4L2SINK_SUCCESS_OPEN;
}

static bool v4l2device_close(void *data)
{
	printf("LOOPBACK CLOSE");
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	if(out_data->v4l2_fd)
		close(out_data->v4l2_fd);
	if(out_data->pa) {
		int error;
		pa_simple_drain(out_data->pa, &error);
		pa_simple_free(out_data->pa);
	}
	if(out_data->audio_fd)
		close(out_data->audio_fd);
	out_data->pa = NULL;
	out_data->audio_fd = 0;
	out_data->v4l2_fd = 0;
	return true;
}

static const char *LoopbackSink_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("LoopbackSink");
}

static void LoopbackSink_destroy(void *data)
{
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	if (out_data){
		bfree(out_data);
	}
}
static void *LoopbackSink_create(obs_data_t *settings, obs_output_t *output)
{
	LoopbackSink_data *data = (LoopbackSink_data *)bzalloc(sizeof(
		struct LoopbackSink_data));
	data->output = output;
	UNUSED_PARAMETER(settings);
	return data;
}

static bool LoopbackSink_start(void *data)
{
	printf("start LOOPBACK\n");
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;
	out_data->width = (int32_t)obs_output_get_width(out_data->output);
	out_data->height = (int32_t)obs_output_get_height(out_data->output);
	int ret = v4l2device_open(data);

	if(ret!= V4L2SINK_SUCCESS_OPEN){
		if(out_data->pa){
			pa_simple_free(out_data->pa);
			out_data->pa = NULL;
		}
		switch (ret) {
		case V4L2SINK_ERROR_OPEN: 
			//v4l2sink_signal_stop("device open failed", true);
			printf("device open failed");
			obs_output_signal_stop(out_data->output, OBS_OUTPUT_CONNECT_FAILED);
			break;
		case V4L2SINK_ERROR_FORMAT:
			//v4l2sink_signal_stop("format not support", true);
			printf("format not supported");
			obs_output_signal_stop(out_data->output, OBS_OUTPUT_UNSUPPORTED);
			break;
		default:
			//v4l2sink_signal_stop("device open failed", true);
			printf("device open failed");
			obs_output_signal_stop(out_data->output, OBS_OUTPUT_ERROR);
		}
		return false;
	}
	
	if(!obs_output_can_begin_data_capture(out_data->output,0)){
		//v4l2sink_signal_stop("start failed", true);
		printf("start failed");
		//obs_output_signal_stop(out_data->output, ???);
		return false;
	}

	printf("started LOOPBACK\n");
	out_data->active = true;
	return obs_output_begin_data_capture(out_data->output, 0);
}

static void LoopbackSink_stop(void *data, uint64_t ts)
{
	LoopbackSink_data *out_data = (LoopbackSink_data*)data;

	if(out_data->active){
		out_data->active = false;
		obs_output_end_data_capture(out_data->output);	
		v4l2device_close(data);
		
		//v4l2sink_signal_stop("stop", false);
		printf("stop");
		obs_output_signal_stop(out_data->output, OBS_OUTPUT_SUCCESS);
	}
	
}

static obs_properties_t* LoopbackSink_getproperties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t* props = obs_properties_create();

	return props;
}

static void v4l2sink_videotick(void *param, struct video_data *frame)
{
	LoopbackSink_data *out_data = (LoopbackSink_data*)param;
	if(out_data->active){
		size_t bytes = write(out_data->v4l2_fd
			, frame->data[0], 
			out_data->frame_size);
	}
}

static void pulse_audiotick(void *param, struct audio_data *frames)
{
	LoopbackSink_data *out_data = (LoopbackSink_data*)param;
	if(out_data->active){

		size_t bytes = out_data->audioframe_size * frames->frames;

		int error;
		if(out_data->pa)
			pa_simple_write(out_data->pa
				, frames->data[0]
				, bytes
				, &error);
		else {
			write(out_data->audio_fd, frames->data[0], bytes);
		}
	}
}

struct obs_output_info loopback_output = {
	.id = "LoopbackSink",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE,
	.get_name = LoopbackSink_getname,
	.create = LoopbackSink_create,
	.destroy = LoopbackSink_destroy,
	.start = LoopbackSink_start,
	.stop = LoopbackSink_stop,
	.raw_video = v4l2sink_videotick,
	.raw_audio = pulse_audiotick,
	.get_properties = LoopbackSink_getproperties,
};


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("loopback-services", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Loopback services";
}

const char *get_module_name(void)
{
	return "loopback-services";
}

bool obs_module_load(void)
{
	printf("REG lb-service");
	obs_register_service(&loopback_service);
	printf("REG lb-output");
	obs_register_output(&loopback_output);
	
	//obs_output_set_service(&loopback_output, &loopback_service);
	
	return true;
}

void obs_module_unload(void)
{
}

