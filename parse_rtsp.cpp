#include <process.h>

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include "parse_rtsp.h"

// Forward function definitions:

// RTSP 'response handlers':
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);

// Other event handler functions:
void subsessionAfterPlaying(void* clientData); // called when a stream's subsession (e.g., audio or video substream) ends
void subsessionByeHandler(void* clientData); // called when a RTCP "BYE" is received for a subsession
void streamTimerHandler(void* clientData);
// called at the end of a stream's expected duration (if the stream has not already signaled its end using a RTCP "BYE")

// The main streaming routine (for each "rtsp://" URL):
void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL);

// Used to iterate through each stream's 'subsessions', setting up each one:
void setupNextSubsession(RTSPClient* rtspClient);

// Used to shut down and close a stream (including its "RTSPClient" object):
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

// A function that outputs a string that identifies each stream (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const RTSPClient& rtspClient) {
	return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const MediaSubsession& subsession) {
	return env << subsession.mediumName() << "/" << subsession.codecName();
}

void usage(UsageEnvironment& env, char const* progName) {
	env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
	env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

//char eventLoopWatchVariable = 0;

//int main(int argc, char** argv) {
//  // Begin by setting up our usage environment:
//  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
//  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);
//
//  // We need at least one "rtsp://" URL argument:
//  if (argc < 2) {
//    usage(*env, argv[0]);
//    return 1;
//  }
//
//  // There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
//  for (int i = 1; i <= argc-1; ++i) {
//    openURL(*env, argv[0], argv[i]);
//  }
//
//  // All subsequent activity takes place within the event loop:
//  env->taskScheduler().doEventLoop(&eventLoopWatchVariable);
//    // This function call does not return, unless, at some point in time, "eventLoopWatchVariable" gets set to something non-zero.
//
//  return 0;
//
//  // If you choose to continue the application past this point (i.e., if you comment out the "return 0;" statement above),
//  // and if you don't intend to do anything more with the "TaskScheduler" and "UsageEnvironment" objects,
//  // then you can also reclaim the (small) memory used by these objects by uncommenting the following code:
//  /*
//    env->reclaim(); env = NULL;
//    delete scheduler; scheduler = NULL;
//  */
//}

// Define a class to hold per-stream state that we maintain throughout each stream's lifetime:

class StreamClientState {
public:
	StreamClientState();
	virtual ~StreamClientState();

public:
	MediaSubsessionIterator* iter;
	MediaSession* session;
	MediaSubsession* subsession;
	TaskToken streamTimerTask;
	double duration;
};

// If you're streaming just a single stream (i.e., just from a single URL, once), then you can define and use just a single
// "StreamClientState" structure, as a global variable in your application.  However, because - in this demo application - we're
// showing how to play multiple streams, concurrently, we can't do that.  Instead, we have to have a separate "StreamClientState"
// structure for each "RTSPClient".  To do this, we subclass "RTSPClient", and add a "StreamClientState" field to the subclass:

class ourRTSPClient: public RTSPClient {
public:
	static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel = 0,
		char const* applicationName = NULL,
		portNumBits tunnelOverHTTPPortNum = 0);

	void set_rtsp_param(  rtsp_data_callback rtsp_data_cb,
		void* rtsp_data_cb_user,
		bool *is_need_shutdown_stream,
		CClientMutex *mutex);

protected:
	ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum);
	// called only by createNew();
	virtual ~ourRTSPClient();

public:
	StreamClientState scs;

	rtsp_data_callback rtsp_data_cb_;
	void* rtsp_data_cb_user_;
	bool *is_need_shutdown_stream_;
	CClientMutex *mutex_;

	bool has_audio_stream_;
};

// Define a data sink (a subclass of "MediaSink") to receive the data for each subsession (i.e., each audio or video 'substream').
// In practice, this might be a class (or a chain of classes) that decodes and then renders the incoming audio or video.
// Or it might be a "FileSink", for outputting the received data into a file (as is done by the "openRTSP" application).
// In this example code, however, we define a simple 'dummy' sink that receives incoming data, but does nothing with it.

class DummySink: public MediaSink {
public:
	static DummySink* createNew(UsageEnvironment& env,
		MediaSubsession& subsession, // identifies the kind of data that's being received
		char const* streamId = NULL); // identifies the stream itself (optional)

	void set_rtsp_param(  rtsp_data_callback rtsp_data_cb,
		void* rtsp_data_cb_user,
		bool *is_need_shutdown_stream_,
		CClientMutex *mutex);

private:
	DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId);
	// called only by "createNew()"
	virtual ~DummySink();

	static void afterGettingFrame(void* clientData, unsigned frameSize,
		unsigned numTruncatedBytes,
	struct timeval presentationTime,
		unsigned durationInMicroseconds);
	void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
	struct timeval presentationTime, unsigned durationInMicroseconds);

private:
	// redefined virtual functions:
	virtual Boolean continuePlaying();

private:
	u_int8_t* fReceiveBuffer;
	MediaSubsession& fSubsession;
	char* fStreamId;

	rtsp_data_callback rtsp_data_cb_;
	void* rtsp_data_cb_user_;
	bool *is_need_shutdown_stream_;
	CClientMutex *mutex_;

};

#define RTSP_CLIENT_VERBOSITY_LEVEL 1 // by default, print verbose output from each "RTSPClient"

static unsigned rtspClientCount = 0; // Counts how many streams (i.e., "RTSPClient"s) are currently in use.

void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL) {
	// Begin by creating a "RTSPClient" object.  Note that there is a separate "RTSPClient" object for each stream that we wish
	// to receive (even if more than stream uses the same "rtsp://" URL).
	RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
	if (rtspClient == NULL) {
		env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
		return;
	}

	++rtspClientCount;

	// Next, send a RTSP "DESCRIBE" command, to get a SDP description for the stream.
	// Note that this command - like all RTSP commands - is sent asynchronously; we do not block, waiting for a response.
	// Instead, the following function call returns immediately, and we handle the RTSP response later, from within the event loop:
	rtspClient->sendDescribeCommand(continueAfterDESCRIBE); 
}


// Implementation of the RTSP 'response handlers':

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
			delete[] resultString;
			break;
		}

		char* const sdpDescription = resultString;
		env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

		// Create a media session object from this SDP description:
		scs.session = MediaSession::createNew(env, sdpDescription);
		delete[] sdpDescription; // because we don't need it anymore
		if (scs.session == NULL) {
			env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env.getResultMsg() << "\n";
			break;
		} else if (!scs.session->hasSubsessions()) {
			env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
			break;
		}

		// Then, create and set up our data source objects for the session.  We do this by iterating over the session's 'subsessions',
		// calling "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command, on each one.
		// (Each 'subsession' will have its own data source.)
		scs.iter = new MediaSubsessionIterator(*scs.session);
		setupNextSubsession(rtspClient);
		return;
	} while (0);

	// An unrecoverable error occurred with this stream.
	shutdownStream(rtspClient);
}

// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP, change the following to True:
#define REQUEST_STREAMING_OVER_TCP False

void setupNextSubsession(RTSPClient* rtspClient) {
	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

	scs.subsession = scs.iter->next();
	if (scs.subsession != NULL) {
		if (!scs.subsession->initiate()) {
			env << *rtspClient << "Failed to initiate the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
			setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
		} else {
			env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
			if (scs.subsession->rtcpIsMuxed()) {
				env << "client port " << scs.subsession->clientPortNum();
			} else {
				env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1;
			}
			env << ")\n";

			if(!strcmp(scs.subsession->mediumName(), "audio"))
				((ourRTSPClient*)rtspClient)->has_audio_stream_ = true; 

			// Continue setting up this subsession, by sending a RTSP "SETUP" command:
			rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP);
		}
		return;
	}

	// We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY" command to start the streaming:
	if (scs.session->absStartTime() != NULL) {
		// Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
	} else {
		scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
	}
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
			break;
		}

		env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
		if (scs.subsession->rtcpIsMuxed()) {
			env << "client port " << scs.subsession->clientPortNum();
		} else {
			env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1;
		}
		env << ")\n";

		// Having successfully setup the subsession, create a data sink for it, and call "startPlaying()" on it.
		// (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
		// after we've sent a RTSP "PLAY" command.)

		scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());
		// perhaps use your own custom "MediaSink" subclass instead
		if (scs.subsession->sink == NULL) {
			env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
				<< "\" subsession: " << env.getResultMsg() << "\n";
			break;
		}

		((DummySink*)scs.subsession->sink)->set_rtsp_param(((ourRTSPClient*)rtspClient)->rtsp_data_cb_, ((ourRTSPClient*)rtspClient)->rtsp_data_cb_user_,
			((ourRTSPClient*)rtspClient)->is_need_shutdown_stream_, ((ourRTSPClient*)rtspClient)->mutex_);
		env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
		scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the "RTSPClient" from the subsession 
		scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
			subsessionAfterPlaying, scs.subsession);
		// Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
		if (scs.subsession->rtcpInstance() != NULL) {
			scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
		}
	} while (0);
	delete[] resultString;

	// Set up the next subsession, if any:
	setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
	Boolean success = False;

	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
			break;
		}

		// Set a timer to be handled at the end of the stream's expected duration (if the stream does not already signal its end
		// using a RTCP "BYE").  This is optional.  If, instead, you want to keep the stream active - e.g., so you can later
		// 'seek' back within it and do another RTSP "PLAY" - then you can omit this code.
		// (Alternatively, if you don't want to receive the entire stream, you could set this timer for some shorter value.)
		if (scs.duration > 0) {
			unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's expected duration.  (This is optional.)
			scs.duration += delaySlop;
			unsigned uSecsToDelay = (unsigned)(scs.duration*1000000);
			scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
		}

		env << *rtspClient << "Started playing session";
		if (scs.duration > 0) {
			env << " (for up to " << scs.duration << " seconds)";
		}
		env << "...\n";

		success = True;
	} while (0);
	delete[] resultString;

	if (!success) {
		// An unrecoverable error occurred with this stream.
		shutdownStream(rtspClient);
	}
}


// Implementation of the other event handlers:

void subsessionAfterPlaying(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

	// Begin by closing this subsession's stream:
	Medium::close(subsession->sink);
	subsession->sink = NULL;

	// Next, check whether *all* subsessions' streams have now been closed:
	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) return; // this subsession is still active
	}

	// All subsessions' streams have now been closed, so shutdown the client:
	shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
	UsageEnvironment& env = rtspClient->envir(); // alias

	env << *rtspClient << "Received RTCP \"BYE\" on \"" << *subsession << "\" subsession\n";

	// Now act as if the subsession had closed:
	subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
	ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
	StreamClientState& scs = rtspClient->scs; // alias

	scs.streamTimerTask = NULL;

	// Shut down the stream:
	shutdownStream(rtspClient);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {

	//((ourRTSPClient*)rtspClient)->mutex_->get_mutex();

	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

	// First, check whether any subsessions have still to be closed:
	if (scs.session != NULL) { 
		Boolean someSubsessionsWereActive = False;
		MediaSubsessionIterator iter(*scs.session);
		MediaSubsession* subsession;

		while ((subsession = iter.next()) != NULL) {
			if (subsession->sink != NULL) {
				Medium::close(subsession->sink);
				subsession->sink = NULL;

				if (subsession->rtcpInstance() != NULL) {
					subsession->rtcpInstance()->setByeHandler(NULL, NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
				}

				someSubsessionsWereActive = True;
			}
		}

		if (someSubsessionsWereActive) {
			// Send a RTSP "TEARDOWN" command, to tell the server to shutdown the stream.
			// Don't bother handling the response to the "TEARDOWN".
			rtspClient->sendTeardownCommand(*scs.session, NULL);
		}
	}

	env << *rtspClient << "Closing the stream.\n";

	Medium::close(rtspClient);
	// Note that this will also cause this stream's "StreamClientState" structure to get reclaimed.

	//((ourRTSPClient*)rtspClient)->mutex_->release_mutex();


	// if (--rtspClientCount == 0) {
	// The final stream has ended, so exit the application now.
	// (Of course, if you're embedding this code into your own application, you might want to comment this out,
	// and replace it with "eventLoopWatchVariable = 1;", so that we leave the LIVE555 event loop, and continue running "main()".)
	//    exit(exitCode);
	// }
}


// Implementation of "ourRTSPClient":

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
		return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

void ourRTSPClient::set_rtsp_param(  rtsp_data_callback rtsp_data_cb,
	void* rtsp_data_cb_user,
	bool *is_need_shutdown_stream,
	CClientMutex *mutex)
{
	rtsp_data_cb_ = rtsp_data_cb;
	rtsp_data_cb_user_ = rtsp_data_cb_user;
	is_need_shutdown_stream_ = is_need_shutdown_stream;
	mutex_ = mutex;

	return;
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
	: RTSPClient(env,rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) ,
	rtsp_data_cb_(NULL),
	rtsp_data_cb_user_(NULL),
	is_need_shutdown_stream_(NULL),
	mutex_(NULL),
	has_audio_stream_(false)
{
}

ourRTSPClient::~ourRTSPClient() {
}


// Implementation of "StreamClientState":

StreamClientState::StreamClientState()
	: iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
	delete iter;
	if (session != NULL) {
		// We also need to delete "session", and unschedule "streamTimerTask" (if set)
		UsageEnvironment& env = session->envir(); // alias

		env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}


// Implementation of "DummySink":

// Even though we're not going to be doing anything with the incoming data, we still need to receive it.
// Define the size of the buffer that we'll use:
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 1024*1024

DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId) {
	return new DummySink(env, subsession, streamId);
}

void DummySink::set_rtsp_param(  rtsp_data_callback rtsp_data_cb,
	void* rtsp_data_cb_user,
	bool *is_need_shutdown_stream,
	CClientMutex *mutex)
{
	rtsp_data_cb_ = rtsp_data_cb;
	rtsp_data_cb_user_ = rtsp_data_cb_user;
	is_need_shutdown_stream_ = is_need_shutdown_stream;
	mutex_ = mutex;

	return;
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId)
	: MediaSink(env),
	fSubsession(subsession),
	rtsp_data_cb_(NULL),
	rtsp_data_cb_user_(NULL),
	is_need_shutdown_stream_(NULL),
	mutex_(NULL)
{
		fStreamId = strDup(streamId);
		fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
}

DummySink::~DummySink() {
	delete[] fReceiveBuffer;
	delete[] fStreamId;
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
struct timeval presentationTime, unsigned durationInMicroseconds) {
	DummySink* sink = (DummySink*)clientData;
	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
//#define DEBUG_PRINT_EACH_RECEIVED_FRAME 1

void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
	// We've just received a frame of data.  (Optionally) print out information about it:

	if(!strcmp(fSubsession.mediumName(), "video"))
	{
		if(!strcmp(fSubsession.codecName(), "H264"))	/* h264��Ƶ֧�� */
		{	
			SLive_RtspDataInfo rtsp_data_info;
			rtsp_data_info.data_type = LIVE_RTSP_DATA_TYPE_V;
			rtsp_data_info.video_param.video_encode_type = LIVE_ENCODE_V_H264;
			rtsp_data_info.video_param.pts = (presentationTime.tv_sec + presentationTime.tv_usec/(double)1000000)*90000;	//90KHz������

			if(fReceiveBuffer[0] == 0x65 || fReceiveBuffer[0] == 0x25 || fReceiveBuffer[0] == 0x67 || fReceiveBuffer[0] == 0x68)
			{
				rtsp_data_info.video_param.is_i_frame = true;
			}

			rtsp_data_cb_(fReceiveBuffer, frameSize, rtsp_data_info, rtsp_data_cb_user_);

		}
		else if(!strcmp(fSubsession.codecName(), "MP2T"))	/* TS�鲥��֧�� */
		{
			SLive_RtspDataInfo rtsp_data_info;
			rtsp_data_info.video_param.video_encode_type = LIVE_ENCODE_V_MP2T;

			rtsp_data_cb_(fReceiveBuffer, frameSize, rtsp_data_info, rtsp_data_cb_user_);
		}
		else
		{
			/* do something */
		}
	}
	if(!strcmp(fSubsession.mediumName(), "audio"))
	{
		SLive_RtspDataInfo rtsp_data_info;
		if(!strcmp(fSubsession.codecName(), "PCMA"))				//g711a ��Ƶ֧��
		{
			rtsp_data_info.data_type = LIVE_RTSP_DATA_TYPE_A;
			rtsp_data_info.audio_param.audio_encode_type = LIVE_ENCODE_A_PCMA;
			rtsp_data_info.audio_param.channels = fSubsession.numChannels();
			rtsp_data_info.audio_param.samples_rate = fSubsession.rtpTimestampFrequency();
			rtsp_data_info.audio_param.pts = (presentationTime.tv_sec + presentationTime.tv_usec/(double)1000000)*90000; //90KHz��������

			rtsp_data_cb_(fReceiveBuffer, frameSize, rtsp_data_info, rtsp_data_cb_user_);

		}
		else if(!strcmp(fSubsession.codecName(), "MPEG4-GENERIC"))	//aac ��Ƶ֧��
		{
			rtsp_data_info.data_type = LIVE_RTSP_DATA_TYPE_A;
			rtsp_data_info.audio_param.audio_encode_type = LIVE_ENCODE_A_AAC;
			rtsp_data_info.audio_param.channels = fSubsession.numChannels();
			rtsp_data_info.audio_param.samples_rate = fSubsession.rtpTimestampFrequency();
			rtsp_data_info.audio_param.pts = (presentationTime.tv_sec + presentationTime.tv_usec/(double)1000000)*90000; //90KHz��������

			rtsp_data_cb_(fReceiveBuffer, frameSize, rtsp_data_info, rtsp_data_cb_user_);

		}
		else
		{
			//
		}
	}

	*is_need_shutdown_stream_ = true;

	// Then continue, to request the next frame of data:
	mutex_->get_mutex();
	continuePlaying();
	mutex_->release_mutex();
}

Boolean DummySink::continuePlaying() {
	if (fSource == NULL) return False; // sanity check (should not happen)

	// Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
	fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE,
		afterGettingFrame, this,
		onSourceClosure, this);
	return True;
}



CRTSPClient::CRTSPClient():
is_need_shutdown_stream_(false),
	rtsp_live_client_(NULL),
	event_loop_execute_(0)
{
	return;
}

CRTSPClient::~CRTSPClient()
{
	return;
}

typedef struct RTSPClientThreadParam_S
{
	rtsp_data_callback rtsp_data_cb;
	void* user_param;
	bool* is_need_shutdown_stream;
	void** rtsp_live_client;
	char* event_loop_execute;
	char url[256];
	CClientMutex* mutex;
}RTSPClientThreadParam_S;


void CRTSPClient::run(std::string url, rtsp_data_callback rtsp_data_cb, void* user_param)
{
	std::string url_t = url;
	if(url_t[url_t.size()-1] == '\n')
	{
		url = url_t.substr(0, url_t.size()-1);
	}


	RTSPClientThreadParam_S *thread_param = (RTSPClientThreadParam_S*) malloc(sizeof(RTSPClientThreadParam_S));
	if(NULL == thread_param) return;
	memset(thread_param, 0, sizeof(RTSPClientThreadParam_S));
	thread_param->rtsp_data_cb = rtsp_data_cb;
	thread_param->user_param = user_param;
	thread_param->is_need_shutdown_stream = &is_need_shutdown_stream_;
	thread_param->rtsp_live_client = &rtsp_live_client_;
	thread_param->event_loop_execute = &event_loop_execute_;
	thread_param->mutex = &mutex_;
	if(url.size() > 256)
	{
		memcpy(thread_param->url, url.c_str(), 256);
	}
	else
	{
		memcpy(thread_param->url, url.c_str(), url.size());
	}

	uintptr_t thread_h = _beginthreadex(NULL, 0, open_rtsp_thread, thread_param, 0, NULL);
	CloseHandle((HANDLE)thread_h);

	return;
}

void CRTSPClient::stop()
{
	event_loop_execute_ = 1;
	while(0 != event_loop_execute_)
	{
		Sleep(2);
	}

	return;
}

bool CRTSPClient::has_audio_stream()
{
	if(NULL == rtsp_live_client_)
	{
		return false;
	}

	return ((ourRTSPClient*)rtsp_live_client_)->has_audio_stream_;
}

unsigned CRTSPClient::open_rtsp_thread(void* param)
{
	if(NULL == param)
	{
		return 1;
	}

	RTSPClientThreadParam_S* thread_param = (RTSPClientThreadParam_S*) param;

	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	// There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
	RTSPClient* rtspClient = ourRTSPClient::createNew(*env, thread_param->url, RTSP_CLIENT_VERBOSITY_LEVEL, "rtsp_client");
	if (rtspClient == NULL)
	{
		return 1;
	}

	*(thread_param->rtsp_live_client) = (void*)rtspClient;
	((ourRTSPClient*)rtspClient)->set_rtsp_param(thread_param->rtsp_data_cb, thread_param->user_param, thread_param->is_need_shutdown_stream, thread_param->mutex);

	// Next, send a RTSP "DESCRIBE" command, to get a SDP description for the stream.
	// Note that this command - like all RTSP commands - is sent asynchronously; we do not block, waiting for a response.
	// Instead, the following function call returns immediately, and we handle the RTSP response later, from within the event loop:
	rtspClient->sendDescribeCommand(continueAfterDESCRIBE); 

	// All subsequent activity takes place within the event loop:
	while(true)
	{
		if(*(thread_param->event_loop_execute) == 1) break;

		thread_param->mutex->get_mutex();
		env->taskScheduler().doEventLoop(thread_param->event_loop_execute);
		thread_param->mutex->release_mutex();
	}
	// This function call does not return, unless, at some point in time, "eventLoopWatchVariable" gets set to something non-zero.

	if (true == *(thread_param->is_need_shutdown_stream))
	{
		shutdownStream(rtspClient);
	}


	*(thread_param->is_need_shutdown_stream) = false;
	*(thread_param->rtsp_live_client) = NULL;
	*(thread_param->event_loop_execute) = 0;

	if(NULL != thread_param)
	{
		free(thread_param);
		thread_param = NULL;
	}

	return 0;
}