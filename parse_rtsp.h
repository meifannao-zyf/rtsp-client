#pragma once

/* 私有头文件 */
#include <string>
#include <Windows.h>
#include "common_rtsp.h"

class RTSP_PARSE_API CClientMutex
{
public:
	CClientMutex(){InitializeCriticalSection(&client_mutex_);}
	~CClientMutex(){DeleteCriticalSection(&client_mutex_);}

	__inline void get_mutex(){EnterCriticalSection(&client_mutex_);}
	__inline void release_mutex(){LeaveCriticalSection(&client_mutex_);}

private:

	CRITICAL_SECTION client_mutex_;
};

class RTSP_PARSE_API CRTSPClient
{
public:
	CRTSPClient();
	~CRTSPClient();

	void run(std::string url, rtsp_data_callback rtsp_data_cb, void* user_param);
	void stop();

	bool has_audio_stream();

private:
	static unsigned __stdcall open_rtsp_thread(void* param);

	bool is_need_shutdown_stream_;
	void* rtsp_live_client_;
	char event_loop_execute_;
	CClientMutex mutex_;
};