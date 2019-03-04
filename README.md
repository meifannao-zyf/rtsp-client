# rtsp-client
live555 rtsp client muiltthreading

此处仅仅上传live555的调用部分代码，live555源码需要自行引入

live555的源码引入时要修改两个地方：
1. BasicTaskScheduler0.cpp 文件 line 76，doEventLoop接口之中仅保留SingleStep的接口调用，其余部分需要注释掉。
2. MediaSession.cpp 文件 line 553，MediaSubsessionIterator构造函数需要修改 reset() 变为 fNextPtr = fOurSession.fSubsessionsHead
