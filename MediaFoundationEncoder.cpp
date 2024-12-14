#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <algorithm>

#include "tp_stub.h"

#include "TVPWindowsException.h"
#include "MediaFoundationEncoder.h"


//----------------------------------------------------------------------
MediaFoundationEncoder::MediaFoundationEncoder()
	: encoder_running_(false), video_quality_(50), video_frame_rate_(30), video_width_(640), video_height_(480)
{
}

MediaFoundationEncoder::~MediaFoundationEncoder()
{
}

void
MediaFoundationEncoder::SetVideoQuality( DWORD q )
{
	video_quality_ = std::min(DWORD(100), std::max(DWORD(0), q));
}

DWORD
MediaFoundationEncoder::GetVideoQuality() const
{
	return video_quality_;
}

void
MediaFoundationEncoder::SetVideoFrameRate( tjs_real f )
{
	video_frame_rate_ = f;
}

tjs_real
MediaFoundationEncoder::GetVideoFrameRate() const
{
	return video_frame_rate_;
}


void
MediaFoundationEncoder::SetVideoWidth( int w )
{
	video_width_ = w;
}

int
MediaFoundationEncoder::GetVideoWidth() const
{
	return video_width_;
}

void
MediaFoundationEncoder::SetVideoHeight( int w )
{
	video_height_ = w;
}

int
MediaFoundationEncoder::GetVideoHeight() const
{
	return video_height_;
}

HRESULT
MediaFoundationEncoder::Initial( const std::wstring &outFile )
{
	HRESULT hr;

	hr = MFCreateSinkWriterFromURL(outFile.c_str(), NULL, NULL, &m_SinkWriter);

	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	auto qualityFactor = 0.5f * std::pow(2.0f, video_quality_ / 50.0f);
	LONG bitrate = static_cast<LONG>(0.07f * qualityFactor * video_width_ * video_height_ * video_frame_rate_);

	// 出力 (H.264)
	hr = MFCreateMediaType(&m_VideoOutType);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);
	m_VideoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	if (outFile.ends_with(L".mp4"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	else if (outFile.ends_with(L".wmv"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
	else if (outFile.ends_with(L".mpg")
			 || outFile.ends_with(L".mpeg"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MPEG2);

	m_VideoOutType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
	MFSetAttributeSize(m_VideoOutType.Get(), MF_MT_FRAME_SIZE, video_width_, video_height_);
	MFSetAttributeRatio(m_VideoOutType.Get(), MF_MT_FRAME_RATE, LONG(video_frame_rate_ * 1000000), 1000000);
	MFSetAttributeRatio(m_VideoOutType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	m_VideoOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr = m_SinkWriter->AddStream(m_VideoOutType.Get(), &m_VideoStreamIndex);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	// 入力 (未圧縮ビデオ、例：NV12)
	hr = MFCreateMediaType(&m_VideoInType);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);
	m_VideoInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	m_VideoInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
	MFSetAttributeSize(m_VideoInType.Get(), MF_MT_FRAME_SIZE, video_width_, video_height_);
	MFSetAttributeRatio(m_VideoInType.Get(), MF_MT_FRAME_RATE, LONG(video_frame_rate_ * 1000000), 1000000);
	MFSetAttributeRatio(m_VideoInType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	m_VideoInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	hr = m_SinkWriter->SetInputMediaType(m_VideoStreamIndex, m_VideoInType.Get(), NULL);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	return S_OK;
}

HRESULT
MediaFoundationEncoder::Start()
{
	if (m_SinkWriter.Get() == nullptr)
		return E_INVALIDARG;

	HRESULT hr;

	hr = m_SinkWriter->BeginWriting();
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	encoder_running_ = true;

	return S_OK;
}

HRESULT
MediaFoundationEncoder::Stop()
{
	if( encoder_running_ ) {
		encoder_running_ = false;

		if( m_SinkWriter.Get() == nullptr ) {
			return E_INVALIDARG;
		}
		HRESULT hr;
		hr = m_SinkWriter->Finalize();
		if (FAILED(hr))
			TVPThrowExceptionMessage(hr);
		return hr;
	} else {
		return S_OK;
	}
}

void
MediaFoundationEncoder::WriteVideoSample(void *sample, size_t sample_size, LONGLONG time, LONGLONG duration)
{
	HRESULT hr;

	Microsoft::WRL::ComPtr<IMFSample> pSample;
	hr = MFCreateSample(&pSample);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
	DWORD frameBufferSize = sample_size;
	hr = MFCreateMemoryBuffer(frameBufferSize, &pBuffer);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);

	BYTE* pData = NULL;
	DWORD cbMaxLength = 0, cbCurrentLength = 0;
	pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);
	memcpy(pData, sample, sample_size);
	pBuffer->Unlock();
	pBuffer->SetCurrentLength(frameBufferSize);

	pSample->AddBuffer(pBuffer.Get());

	pSample->SetSampleTime(time);
	pSample->SetSampleDuration(duration);

	hr = m_SinkWriter->WriteSample(m_VideoStreamIndex, pSample.Get());
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr);
}


