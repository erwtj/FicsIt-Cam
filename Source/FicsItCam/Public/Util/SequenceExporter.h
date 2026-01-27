#pragma once

#if PLATFORM_WINDOWS
extern "C" {
	#include "libavcodec/avcodec.h"
	#include "libavformat/avformat.h"
	#include "libswscale/swscale.h"
	#include "libswresample/swresample.h"
	#include "libavutil/opt.h"
}
#endif

#include "CoreMinimal.h"
#include "PixelFormat.h"

class FSequenceExporter {
protected:
	bool bFinished = false;
	
public:
	virtual ~FSequenceExporter() {
		if (!bFinished) Finish();
	}

	virtual bool Init() = 0;
	virtual void AddFrame(EPixelFormat Format, void* ptr, FIntPoint ReadSize, FIntPoint Size) = 0;
	virtual void Finish() {
		bFinished = true;
	};
};

#if PLATFORM_WINDOWS
class FSequenceMP4Exporter : public FSequenceExporter {
private:
	FIntPoint ImageSize;
	int FPS;
	FString Path;
	AVFormatContext* FormatContext = nullptr;
	AVStream* VideoStream = nullptr;
	AVStream* AudioStream = nullptr;
	const AVCodec* VideoCodec = nullptr;
	const AVCodec* AudioCodec = nullptr;
	AVCodecContext* VideoCodecContext = nullptr;
	AVCodecContext* AudioCodecContext = nullptr;
	AVPacket* VideoPacket = nullptr;
	AVPacket* AudioPacket = nullptr;
	AVFrame* VideoFrame = nullptr;
	AVFrame* AudioFrame = nullptr;
	SwsContext* SwsContext = nullptr;
	SwrContext* SwrContext = nullptr;
	IFileHandle* File = nullptr;
	int64 VideoFrameNr = 0;
	uint64 AudioSampleCount = 0;
	int AudioSampleRate = 44100;
	
public:
	FSequenceMP4Exporter(FIntPoint ImageSize, int FPS, FString InPath, int AudioSampleRate);
	~FSequenceMP4Exporter();
	
	virtual bool Init() override;
	virtual void AddFrame(EPixelFormat Format, void* ptr, FIntPoint ReadSize, FIntPoint Size) override;
	virtual void Finish() override;

	void AddAudioFrame(float* Samples, int SampleCount);
	
	void ReadVideoBuffer();
	void ReadAudioBuffer();
};
#endif

class FSequenceImageExporter : public FSequenceExporter {
private:
	FString Path;
	FIntPoint ImageSize;
	FDateTime StartTime;
	uint64 Increment = 0;

public:
	FSequenceImageExporter(FString InPath, FIntPoint InImageSize);

	virtual bool Init() override;
	virtual void AddFrame(EPixelFormat Format, void* ptr, FIntPoint ReadSize, FIntPoint Size) override;
	virtual void Finish() override;
};