#pragma once

#include <exception>
#include <memory>
#include <vector>
#include <vector>

#include "cinder/Cinder.h"
#include "cinder/CurrentFunction.h"
#include "cinder/Thread.h"

#if defined( CINDER_MAC )
#define _OSX
#endif

#if defined( __x86_64 )
#define _x64
#endif

#include "PvApi.h"

#include "ChannelCache.h"
#include "SurfaceCache.h"

namespace mndl { namespace pvapi {

typedef std::shared_ptr< class CapturePvApi > CapturePvApiRef;

class CapturePvApi
{
  public:
	class Device
	{
	  public:
		unsigned long getId() const { return mId; }
		std::string getCameraName() const { return mCameraName; }
		std::string getModelName() const { return mModelName; }
		std::string getSerialNumber() const { return mSerialNumber; }
		std::string getFirmwareVersion() const { return mFirmwareVersion; }

	  protected:
		unsigned long mId;
		std::string mCameraName;
		std::string mModelName;
		std::string mSerialNumber;
		std::string mFirmwareVersion;

		friend class CapturePvApi;
	};
	typedef std::shared_ptr< Device > DeviceRef;

	static void init();
	static void cleanup();

	//! Returns the number of devices connected.
	static size_t getNumDevices();

	//! Returns a vector of all devices connected to the system. If \a forceRefresh then the system will be polled for connected devices.
	static const std::vector< DeviceRef > & getDevices( bool forceRefresh = false, float timeoutSeconds = 1.0f );

	static CapturePvApiRef create( const DeviceRef &device = DeviceRef() )
	{ return CapturePvApiRef( new CapturePvApi( device ) ); }

	virtual ~CapturePvApi();

	tPvUint32 getAttr( const std::string &name ) const;
	void setAttr( const std::string &name, tPvUint32 value ) const;

	void start();
	void stop();

	bool checkNewFrame() const;
	ci::Channel8uRef getChannel() const;
	ci::Channel8uRef getChannel8u() const;
	ci::Channel16uRef getChannel16u() const;
	ci::Surface8uRef getSurface() const;
	ci::Surface8uRef getSurface8u() const;

	//! Returns the maximum size of the captured image in pixels.
	int32_t getSensorWidth() const { return mSensorWidth; }
	//! Returns the maximum height of the captured image in pixels.
	int32_t getSensorHeight() const { return mSensorHeight; }
	//! Returns the maximum size of the captured image in pixels.
	ci::ivec2 getSensorSize() const { return ci::ivec2( getSensorWidth(), getSensorHeight() ); }

	//! Returns the width of the captured image in pixels.
	int32_t getWidth() const { return mRoi.getWidth(); }
	//! Returns the height of the captured image in pixels.
	int32_t getHeight() const { return mRoi.getHeight(); }
	//! Returns the size of the captured image in pixels.
	ci::ivec2 getSize() const { return mRoi.getSize(); }
	//! Returns the aspect ratio of the capture imagee, which is its width / height
	float getAspectRatio() const { return getWidth() / (float)getHeight(); }
	//! Returns the bounding rectangle of the captured image, which is Area( 0, 0, width, height )
	ci::Area getBounds() const { return mRoi; }

	enum class PixelFormat
	{
		MONO8,
		MONO16,
		MONO12PACKED,
		RGB24,
		NOT_SUPPORTED
	};

	tPvHandle getPvHandle() const { return mHandle; }

 protected:
	static bool sDevicesEnumerated;
	static std::vector< DeviceRef > sDevices;
	static void enumerateDevices( bool forceRefresh = false, float timeoutSeconds = 1.0f );

	CapturePvApi( const DeviceRef &device );

	DeviceRef mDevice;

	void getSupportedPixelFormats();

	void openDevice();
	void closeDevice();

	tPvHandle mHandle = 0;
	tPvUint32 mSensorFrameSize;
	tPvUint32 mSensorWidth;
	tPvUint32 mSensorHeight;

	ci::Area mRoi;

	ChannelCache8uRef mChannelCache8u;
	ChannelCache16uRef mChannelCache16u;
	SurfaceCache8uRef mSurfaceCache8u;
	ci::Channel8uRef mCurrentChannel8u;
	ci::Channel16uRef mCurrentChannel16u;
	ci::Surface8uRef mCurrentSurface8u;

	void threadedFunc();

	std::shared_ptr< std::thread > mThread;
	mutable std::mutex mMutex;
	mutable bool mHasNewFrame = false;
	bool mThreadShouldQuit = false;

	PixelFormat mPixelFormat;

	static void cameraLinkCallback( void *context, tPvInterface interface,
									tPvLinkEvent event, unsigned long id );
};

class CapturePvApiExc : public std::exception
{
  public:
	CapturePvApiExc( const std::string &log )
	{
		mMessage = "CapturePvApi: " + log;
	}

	virtual const char * what() const throw()
	{
		return mMessage.c_str();
	}

  private:
	std::string mMessage;
};

void checkPvApiError( const tPvErr &err, const std::string &functionName,
		const std::string &fileName, const size_t &lineNumber );
void throwOnPvApiError( const tPvErr &err, const std::string &functionName,
		const std::string &fileName, const size_t &lineNumber );

#define THROW_ON_PVAPI_ERROR( err ) mndl::pvapi::throwOnPvApiError( err, CINDER_CURRENT_FUNCTION, __FILE__, __LINE__ )
#define CHECK_PVAPI_ERROR( err ) mndl::pvapi::checkPvApiError( err, CINDER_CURRENT_FUNCTION, __FILE__, __LINE__ )

} } // mndl::pvapi
