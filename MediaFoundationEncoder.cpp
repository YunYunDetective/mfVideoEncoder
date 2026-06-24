#define NOMINMAX

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>

#include "tp_stub.h"
#include "TVPWindowsException.h"
#include "MediaFoundationEncoder.h"

//----------------------------------------------------------------------

MediaFoundationEncoder::MediaFoundationEncoder()
	: m_VideoStreamIndex(0)
	, encoder_running_(false)
	, video_quality_(50)
	, video_frame_rate_(30)
	, video_width_(640)
	, video_height_(480)
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
	if (video_width_ <= 0 || video_height_ <= 0) {
		TVPThrowExceptionMessage(L"invalid video size");
	}
	if (video_frame_rate_ <= 0) {
		TVPThrowExceptionMessage(L"invalid video frame rate");
	}

	// Media Foundation の H.264/WMV 経路では奇数サイズを受け付けないケースがあるため、
	// サイズ調整は上位層で行い、ここでは条件を満たしていなければ失敗させる。
	if ((video_width_ & 1) != 0 || (video_height_ & 1) != 0) {
		TVPThrowExceptionMessage(L"video width and height must be even");
	}

	HRESULT hr;

	// SinkWriter 作成
	hr = MFCreateSinkWriterFromURL(outFile.c_str(), NULL, NULL, &m_SinkWriter);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"MFCreateSinkWriterFromURL: ");

	// 出力
	hr = MFCreateMediaType(&m_VideoOutType);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"MFCreateMediaType: ");

	m_VideoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	if (outFile.ends_with(L".mp4"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	else if (outFile.ends_with(L".wmv"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
	else if (outFile.ends_with(L".mpg") || outFile.ends_with(L".mpeg"))
		m_VideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MPEG2);

	LONG bitrate;

	// 参考値: 1080p30・H.264・quality=50 で 0.07 bits / pixel を基準
	constexpr float kRefBpp = 0.07f; // 1080p30 H.264 Quality=50
	constexpr float kRefPixels = 1920.0f * 1080; // 基準解像度

	const float qualityFactor = std::pow(2.0f, (static_cast<int>(video_quality_) - 50) / 25.0f);
	const float resolutionFactor = std::pow(kRefPixels / (video_width_ * video_height_), 0.25f);

	float codecFactor = 1.0f; // H.264
	// if (codec == kHEVC) codecFactor = 0.55f; // 目安: H.265 ≈45–60 %
	// if (codec == kAV1) codecFactor = 0.45f; // 目安: AV1 ≈40–50 %

	const float bpp = kRefBpp * qualityFactor * resolutionFactor * codecFactor;
	const double pixelsPerSec = static_cast<double>(video_width_) * video_height_ * video_frame_rate_;
	bitrate = static_cast<LONG>(bpp * pixelsPerSec + 0.5); // 四捨五入

	m_VideoOutType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
	MFSetAttributeSize(m_VideoOutType.Get(), MF_MT_FRAME_SIZE, video_width_, video_height_);
	MFSetAttributeRatio(m_VideoOutType.Get(), MF_MT_FRAME_RATE, LONG(video_frame_rate_ * 1000000), 1000000);
	MFSetAttributeRatio(m_VideoOutType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	m_VideoOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

	hr = m_SinkWriter->AddStream(m_VideoOutType.Get(), &m_VideoStreamIndex);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSinkWriter::AddStream: ");

	// 入力 (未圧縮ビデオ、ARGB32)
	hr = MFCreateMediaType(&m_VideoInType);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"MFCreateMediaType: ");

	m_VideoInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	m_VideoInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);

	LONG stride = static_cast<LONG>(video_width_ * 4);
	m_VideoInType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(-stride));
	MFSetAttributeSize(m_VideoInType.Get(), MF_MT_FRAME_SIZE, video_width_, video_height_);
	MFSetAttributeRatio(m_VideoInType.Get(), MF_MT_FRAME_RATE, LONG(video_frame_rate_ * 1000000), 1000000);
	MFSetAttributeRatio(m_VideoInType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	m_VideoInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

	hr = m_SinkWriter->SetInputMediaType(m_VideoStreamIndex, m_VideoInType.Get(), NULL);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSinkWriter::SetInputMediaType: ");

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
		TVPThrowExceptionMessage(hr, L"IMFSinkWriter::BeginWriting: ");

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
			TVPThrowExceptionMessage(hr, L"IMFSinkWriter::Finalize: ");

		return hr;
	} else {
		return S_OK;
	}
}

void
MediaFoundationEncoder::WriteVideoSample(void *sample, size_t sample_size, LONGLONG time, LONGLONG duration)
{
	if (sample == nullptr) {
		TVPThrowExceptionMessage(L"video sample is NULL");
	}
	if (m_SinkWriter.Get() == nullptr) {
		TVPThrowExceptionMessage(L"video not initialized");
	}
	if (video_width_ <= 0 || video_height_ <= 0) {
		TVPThrowExceptionMessage(L"invalid video size");
	}
	if ((video_width_ & 1) != 0 || (video_height_ & 1) != 0) {
		TVPThrowExceptionMessage(L"video width and height must be even");
	}

	const size_t sourceWidthBytes = static_cast<size_t>(video_width_) * 4;
	const size_t destStride = sourceWidthBytes;
	const size_t destFrameBufferSize = destStride * static_cast<size_t>(video_height_);

	if (destFrameBufferSize > std::numeric_limits<DWORD>::max()) {
		TVPThrowExceptionMessage(L"video sample is too large");
	}
	if ((sample_size % static_cast<size_t>(video_height_)) != 0) {
		TVPThrowExceptionMessage(L"invalid video sample pitch");
	}

	const size_t sourcePitch = sample_size / static_cast<size_t>(video_height_);
	if (sourcePitch < sourceWidthBytes) {
		TVPThrowExceptionMessage(L"invalid video sample pitch");
	}

	HRESULT hr;

	Microsoft::WRL::ComPtr<IMFSample> pSample;
	hr = MFCreateSample(&pSample);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"MFCreateSample: ");

	Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
	DWORD frameBufferSize = static_cast<DWORD>(destFrameBufferSize);
	hr = MFCreateAlignedMemoryBuffer(frameBufferSize, 64, &pBuffer);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"MFCreateAlignedMemoryBuffer: ");

	BYTE* pData = NULL;
	DWORD cbMaxLength = 0, cbCurrentLength = 0;
	hr = pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFMediaBuffer::Lock: ");

	const BYTE* source = static_cast<const BYTE*>(sample);

	for (int y = 0; y < video_height_; ++y) {
		const BYTE* srcRow = source + static_cast<size_t>(y) * sourcePitch;
		BYTE* dstRow = pData + static_cast<size_t>(y) * destStride;

		std::memcpy(dstRow, srcRow, sourceWidthBytes);
	}

	hr = pBuffer->Unlock();
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFMediaBuffer::Unlock: ");

	hr = pBuffer->SetCurrentLength(frameBufferSize);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFMediaBuffer::SetCurrentLength: ");

	hr = pSample->AddBuffer(pBuffer.Get());
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSample::AddBuffer: ");

	hr = pSample->SetSampleTime(time);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSample::SetSampleTime: ");

	hr = pSample->SetSampleDuration(duration);
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSample::SetSampleDuration: ");

	hr = m_SinkWriter->WriteSample(m_VideoStreamIndex, pSample.Get());
	if (FAILED(hr))
		TVPThrowExceptionMessage(hr, L"IMFSinkWriter::WriteSample: ");
}
