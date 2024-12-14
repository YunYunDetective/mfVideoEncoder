
#include <windows.h>
#include <mfapi.h>
#include <string>

#include "ncbind.hpp"

#include "TVPWindowsException.h"
#include "MediaFoundationEncoder.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")


#define SAFE_DELETE( obj ) if( obj ) { delete obj; obj = NULL; }


static HRESULT sStartupResult = S_OK;


//----------------------------------------------------------------------
class mfVideoEncoder {
	iTJSDispatch2 *objthis_;

	MediaFoundationEncoder *video_encoder_;

	DWORD video_quality_;     ///< クオリティ、0 - 100 ( default 50 )
	tjs_real video_frame_rate_;  ///< フレームレート(default: 30)
	long video_width_;		///< 画像幅 ( default 640 )
	long video_height_;		///< 画像高さ ( default 480 )
	LONGLONG video_frames_;
	std::wstring filename_;

public:
	mfVideoEncoder( iTJSDispatch2 *objthis )
		: objthis_(objthis)
		, video_encoder_(nullptr)
		, video_quality_(50), video_frame_rate_(30), video_width_(640), video_height_(480), video_frames_(0) {
	}

	virtual ~mfVideoEncoder() {
		close();
	}

	static tjs_error factory(mfVideoEncoder **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis) {
		if (FAILED( sStartupResult )) 
			TVPThrowExceptionMessage(sStartupResult);

		mfVideoEncoder *self = new mfVideoEncoder(objthis);
		if( self ) {
			*result = self;
			return TJS_S_OK;
		} else {
			return TJS_E_FAIL;
		}
	}

	long getVideoQuality() const { return video_quality_; }
	void setVideoQuality( long q ) { video_quality_ = q; }
	tjs_real getVideoFrameRate() const { return video_frame_rate_; }
	void setVideoFrameRate(tjs_real f) { video_frame_rate_ = f; }
	long getVideoWidth() const { return video_width_; }
	void setVideoWidth( long w ) { video_width_ = w; }
	long getVideoHeight() const { return video_height_; }
	void setVideoHeight( long h ) { video_height_ = h; }

	static bool catchDeleteVideoEncoder(void *data, const tTVPExceptionDesc & desc) {
		auto self = static_cast<mfVideoEncoder*>(data);
		SAFE_DELETE(self->video_encoder_);
		return true;
	}

	bool open( const tjs_char *filename ) {
		this->filename_ = filename;

		if (filename_.ends_with(L".mp4")
			|| filename_.ends_with(L".wmv")
			|| filename_.ends_with(L".mpg")
			|| filename_.ends_with(L".mpeg")) {
			TVPDoTryBlock(_open, catchDeleteVideoEncoder, nullptr, this);
			return true;
		} else {
			ttstr msg = L"invalid file extension: ";
			msg += filename;
			TVPThrowExceptionMessage(msg.c_str());
			return false;
		}
	}

	static void _open(void *data) {
		auto self = static_cast<mfVideoEncoder*>(data);
		self->__open();
	}

	void __open() {
		if (video_encoder_)
			delete video_encoder_;

		video_frames_ = 0;
		video_encoder_ = new MediaFoundationEncoder();
		video_encoder_->SetVideoQuality( getVideoQuality() );
		video_encoder_->SetVideoFrameRate( getVideoFrameRate() );
		video_encoder_->SetVideoWidth( getVideoWidth() );
		video_encoder_->SetVideoHeight( getVideoHeight() );

		HRESULT hr;
		hr = video_encoder_->Initial(filename_);
		if (FAILED(hr))
			TVPThrowExceptionMessage(hr);

		hr = video_encoder_->Start();
		if (FAILED(hr))
			TVPThrowExceptionMessage(hr);
	}

	void close() {
		TVPDoTryBlock(_close, catchDeleteVideoEncoder, nullptr, this);
	}

	static void _close(void *data) {
		auto self = static_cast<mfVideoEncoder*>(data);
		self->__close();
	}

	void __close() {
		if( video_encoder_ ) 
			video_encoder_->Stop();
		SAFE_DELETE(video_encoder_);
	}

	void EncodeVideoSample( iTJSDispatch2 *layer ) {
		if(! video_encoder_)
			TVPThrowExceptionMessage(L"video not opened");

		ncbPropAccessor obj(layer);
		const int imageWidth = obj.getIntValue(L"imageWidth");
		const int imageHeight= obj.getIntValue(L"imageHeight");
		// レイヤーサイズチェック
		if( imageWidth != video_width_ || imageHeight != video_height_ ) {
			TVPThrowExceptionMessage( L"invalid layer size");
		}

		const unsigned char* imageBuffer = (const unsigned char*)obj.GetValue(L"mainImageBuffer", ncbTypedefs::Tag<tjs_int64>());
		if( imageBuffer == NULL ) {
			TVPThrowExceptionMessage( L"layer image is NULL");
		}
		const tjs_int imagePitch = obj.GetValue(L"mainImageBufferPitch", ncbTypedefs::Tag<tjs_int>());

		// 吉里吉里のバッファは DIB と同じ構造なのでこの処理で通る
		int size = imageHeight * -imagePitch;
		const unsigned char* buffer = imageBuffer + (imageHeight-1) * imagePitch;

		LONGLONG curTime = LONGLONG(video_frames_ * 10000000LL / video_frame_rate_);
		video_frames_++;
		LONGLONG nextTime = LONGLONG(video_frames_ * 10000000LL / video_frame_rate_);
		LONGLONG duration = nextTime - curTime;

		video_encoder_->WriteVideoSample( (void*)buffer, size, curTime, duration );
	}

	static tjs_error encodeVideoSample( tTJSVariant *result, tjs_int numparams, tTJSVariant **params, mfVideoEncoder *self ) {
		if( numparams < 1 ) {
			return TJS_E_BADPARAMCOUNT;
		}
		if( params[0]->Type() != tvtObject || params[0]->AsObjectNoAddRef()->IsInstanceOf(0,NULL,NULL,L"Layer",NULL) == false ) {
			return TJS_E_INVALIDTYPE;
		}
		self->EncodeVideoSample( *params[0] );
		return TJS_S_OK;
	}
};

//----------------------------------------------------------------------
NCB_REGISTER_CLASS(mfVideoEncoder) {
	Factory(&ClassT::factory);

	NCB_METHOD(open);
	NCB_METHOD(close);
	RawCallback(TJS_W("encodeVideoSample"), &Class::encodeVideoSample, 0);

	NCB_PROPERTY(videoQuality, getVideoQuality, setVideoQuality);
	NCB_PROPERTY(videoFrameRate, getVideoFrameRate, setVideoFrameRate);
	NCB_PROPERTY(videoWidth, getVideoWidth, setVideoWidth);
	NCB_PROPERTY(videoHeight, getVideoHeight, setVideoHeight);
}


/**
 * 登録処理前
 */
void PreRegisterCallback()
{
	CoInitialize( NULL );
	sStartupResult = MFStartup(MF_VERSION);
}

/**
 * 開放処理後
 */
void PostUnregisterCallback()
{
	if (SUCCEEDED(sStartupResult))
		MFShutdown();
	CoUninitialize();
}

NCB_PRE_REGIST_CALLBACK(PreRegisterCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregisterCallback);


