#ifndef __MEDIA_FOUNDATION_ENCODER_H__
#define __MEDIA_FOUNDATION_ENCODER_H__

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <string>


class MediaFoundationEncoder
{
public:
	MediaFoundationEncoder();
	~MediaFoundationEncoder();

	HRESULT Initial( const std::wstring &outFile );

	HRESULT Start();
	HRESULT Stop();

	void WriteVideoSample(void *sample, size_t sample_size, LONGLONG time, LONGLONG duration);
	//	void WriteAudioSample(void* sample, size_t sample_size, LONGLONG time, LONGLONG duration);

	void SetVideoQuality( DWORD q );
	DWORD GetVideoQuality() const;

	void SetVideoFrameRate( tjs_real f );
	tjs_real GetVideoFrameRate() const ;

	void SetVideoWidth( int w );
	int GetVideoWidth() const;

	void SetVideoHeight( int w );
	int GetVideoHeight() const;

private:
	Microsoft::WRL::ComPtr<IMFSinkWriter> m_SinkWriter;
	Microsoft::WRL::ComPtr<IMFMediaType> m_VideoOutType;
    Microsoft::WRL::ComPtr<IMFMediaType> m_VideoInType;
	DWORD m_VideoStreamIndex;

	bool encoder_running_;

	DWORD video_quality_;		// クオリティ、0 - 100 ( default 50 )
	tjs_real video_frame_rate_;           // フレームレート(default 30)
	int video_width_;		// 画像幅 ( default 640 )
	int video_height_;		// 画像高さ ( default 480 )
};

#endif //  __MEDIA_FOUNDATION_ENCODER_H__
