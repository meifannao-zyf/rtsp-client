#pragma once

#ifdef RTSP_PARSE_EXPORT 
#define RTSP_PARSE_API __declspec(dllexport)
#else
#define RTSP_PARSE_API __declspec(dllimport)
#endif

typedef enum ELive_RtspDataType
{
	LIVE_RTSP_DATA_TYPE_INVALID = -1,
	LIVE_RTSP_DATA_TYPE_V,
	LIVE_RTSP_DATA_TYPE_A
}ELive_RtspDataType;

typedef enum ELive_RtspVideoEncodeType
{
	LIVE_ENCODE_V_INVALID = -1,
	LIVE_ENCODE_V_H264,
	LIVE_ENCODE_V_MP2T
}ELive_RtspVideoEncodeType;

typedef enum ELive_RtspAudioEncodeType
{
	LIVE_ENCODE_A_INVAID = -1,
	LIVE_ENCODE_A_PCMA,
	LIVE_ENCODE_A_PCMU,
	LIVE_ENCODE_A_AAC
}ELive_RtspAudioEncodeType;

typedef struct ELive_VideoParam
{
	ELive_RtspVideoEncodeType video_encode_type;
	std::string sps_pps_ext;
	bool is_i_frame;
	__int64 pts;

	ELive_VideoParam()
	{
		video_encode_type = LIVE_ENCODE_V_INVALID;
		is_i_frame = false;
		pts = 0;
	}
}ELive_VideoParam;

typedef struct ELive_AudioParam
{
	ELive_RtspAudioEncodeType audio_encode_type;
	int channels;
	int samples_rate;
	__int64 pts;

	ELive_AudioParam()
	{
		audio_encode_type = LIVE_ENCODE_A_INVAID;
		channels = 1;			//g711默认配置
		samples_rate = 8000;	
		pts = 0;
	}
}ELive_AudioParam;

typedef struct SLive_RtspDataInfo
{
	ELive_RtspDataType data_type;					//数据类型
	ELive_VideoParam video_param;
	ELive_AudioParam audio_param;

	SLive_RtspDataInfo()
	{
		data_type = LIVE_RTSP_DATA_TYPE_INVALID;
	}

}SLive_RtspDataInfo;

typedef void (__stdcall *rtsp_data_callback)(unsigned char* data, int data_len, SLive_RtspDataInfo data_info, void* user_param);