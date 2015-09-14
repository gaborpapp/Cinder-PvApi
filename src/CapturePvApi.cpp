#include <cstring>

#include "cinder/Log.h"
#include "cinder/Utilities.h"
#include "cinder/app/App.h"

#include "CapturePvApi.h"

using namespace ci;

namespace mndl { namespace pvapi {

static const std::vector< std::string > sErrors = { "ePvErrSuccess",
	"ePvErrCameraFault", "ePvErrInternalFault", "ePvErrBadHandle",
	"ePvErrBadParameter", "ePvErrBadSequence", "ePvErrNotFound",
	"ePvErrAccessDenied", "ePvErrUnplugged", "ePvErrInvalidSetup",
	"ePvErrResources", "ePvErrBandwidth", "ePvErrQueueFull",
	"ePvErrBufferTooSmall=", "ePvErrCancelled", "ePvErrDataLost",
	"ePvErrDataMissing", "ePvErrTimeout", "ePvErrOutOfRange",
	"ePvErrWrongType", "ePvErrForbidden", "ePvErrUnavailable", "ePvErrFirewall"
};

static inline std::string getErrorString( const tPvErr &err )
{
	if ( err < sErrors.size() )
	{
		return sErrors[ err ];
	}
	else
	{
		return "Unknown error " + toString( err );
	}
}

void throwOnPvApiError( const tPvErr &err, const std::string &functionName,
		const std::string &fileName, const size_t &lineNumber )
{
	if ( err != ePvErrSuccess )
	{
		throw CapturePvApiExc( functionName + " [" + toString( lineNumber ) +
				"] " + getErrorString( err ) );
	}
}

void checkPvApiError( const tPvErr &err, const std::string &location,
		const std::string &fileName, const size_t &lineNumber )
{
	if ( err != ePvErrSuccess )
	{
		log::Entry( log::LEVEL_ERROR,
			log::Location( location, fileName, lineNumber ) ) <<
			getErrorString( err );
	}
}

// static
void CapturePvApi::init()
{
	tPvErr err = PvInitialize();
	THROW_ON_PVAPI_ERROR( err );
}

// static
void CapturePvApi::cleanup()
{
	PvUnInitialize();
}

bool CapturePvApi::sDevicesEnumerated = false;
std::vector< CapturePvApi::DeviceRef > CapturePvApi::sDevices;

// static
size_t CapturePvApi::getNumDevices()
{
	return PvCameraCount();
}

// static
void CapturePvApi::enumerateDevices( bool forceRefresh /* = false */, float
		timeoutSeconds /* = 1.0f */ )
{
	sDevices.clear();

	const size_t maxIterations = size_t( timeoutSeconds * ( 1000 / 100 ) );
	unsigned long numCameras;
	for ( size_t i = 0; i < maxIterations; i++ )
	{
		numCameras = PvCameraCount();
		if ( numCameras > 0 )
		{
			break;
		}
		ci::sleep( 100.0f );
	}

	tPvCameraInfoEx cameraList[ numCameras ];
	numCameras = PvCameraListEx( cameraList, numCameras, nullptr, sizeof( tPvCameraInfoEx ) );

	for ( unsigned long i = 0; i < numCameras; i++ )
	{
		DeviceRef device = std::make_shared< Device >();
		device->mId = cameraList[ i ].UniqueId;
		device->mCameraName = cameraList[ i ].CameraName;
		device->mSerialNumber = cameraList[ i ].SerialNumber;
		device->mFirmwareVersion = cameraList[ i ].FirmwareVersion;

		sDevices.emplace_back( device );
	}
}

// static
const std::vector< CapturePvApi::DeviceRef > & CapturePvApi::getDevices( bool forceRefresh /* = false */,
		float timeoutSeconds /* = 1.0f */ )
{
	if ( sDevicesEnumerated && ( ! forceRefresh ) )
	{
		return sDevices;
	}

	enumerateDevices( forceRefresh, timeoutSeconds );

	return sDevices;
}

CapturePvApi::CapturePvApi( const DeviceRef &device )
{
	if ( device )
	{
		mDevice = device;
	}
	else
	{
		if ( ! sDevicesEnumerated )
		{
			enumerateDevices();
		}

		if ( sDevices.empty() )
		{
			THROW_ON_PVAPI_ERROR( ePvErrNotFound );
		}
		else
		{
			mDevice = sDevices[ 0 ];
		}
	}

	// FIXME: PvApi does not allow registering the same function with different
	// user contexts for multiple times, so this won't work for a multi-camera setup.
	CHECK_PVAPI_ERROR( PvLinkCallbackRegister(
				CapturePvApi::cameraLinkCallback, ePvLinkAdd, this ) );
	CHECK_PVAPI_ERROR( PvLinkCallbackRegister(
				CapturePvApi::cameraLinkCallback, ePvLinkRemove, this ) );

	// FIXME: First add event does not fire. Maybe because of openDevice?
	openDevice();
	CHECK_PVAPI_ERROR( PvCaptureAdjustPacketSize( mHandle, 8228 ) );

	mSensorWidth = getAttr( "SensorWidth" );
	mSensorHeight = getAttr( "SensorHeight" );

	// TODO: support Roi change
	setAttr( "Width", mSensorWidth );
	setAttr( "Height", mSensorHeight );
	setAttr( "RegionX", 0 );
	setAttr( "RegionY", 0 );
	mRoi = Area( 0, 0, mSensorWidth, mSensorHeight );

	mChannelCache8u = std::make_shared< ChannelCache8u >( mSensorWidth, mSensorHeight, 4 );
	mChannelCache16u = std::make_shared< ChannelCache16u >( mSensorWidth, mSensorHeight, 4 );
	mSurfaceCache8u = std::make_shared< SurfaceCache8u >( mSensorWidth, mSensorHeight, SurfaceChannelOrder::RGB, 4 );
}

CapturePvApi::~CapturePvApi()
{
	stop();
	closeDevice();

	CHECK_PVAPI_ERROR( PvLinkCallbackUnRegister(
				CapturePvApi::cameraLinkCallback, ePvLinkAdd ) );
	CHECK_PVAPI_ERROR( PvLinkCallbackUnRegister(
				CapturePvApi::cameraLinkCallback, ePvLinkRemove ) );
}

void CapturePvApi::openDevice()
{
	std::lock_guard< std::mutex > lock( mMutex );
	if ( mHandle == 0 )
	{
		THROW_ON_PVAPI_ERROR( PvCameraOpen( mDevice->mId, ePvAccessMaster, &mHandle ) );
	}
}

void CapturePvApi::closeDevice()
{
	std::lock_guard< std::mutex > lock( mMutex );
	if ( mHandle != 0 )
	{
		THROW_ON_PVAPI_ERROR( PvCameraClose( mHandle ) );
		mHandle = 0;
	}
}

void CapturePvApi::start()
{
	if ( mHandle == 0 )
	{
		return;
	}

	stop();

	mSensorFrameSize = getAttr( "TotalBytesPerFrame" );

	char buffer[ 512 ];
	CHECK_PVAPI_ERROR( PvAttrEnumGet( mHandle, "PixelFormat", buffer, 512, nullptr ) );
	std::string pixelFormat( buffer );
	mPixelFormat = PixelFormat::NOT_SUPPORTED;
	if ( pixelFormat == "Mono8" )
	{
		mPixelFormat = PixelFormat::MONO8;
	}
	else
	if ( pixelFormat == "Mono16" )
	{
		mPixelFormat = PixelFormat::MONO16;
	}
	else
	if ( pixelFormat == "Mono12Packed" )
	{
		mPixelFormat = PixelFormat::MONO12PACKED;
	}
	else
	if ( pixelFormat == "Rgb24" )
	{
		mPixelFormat = PixelFormat::RGB24;
	}

	mThreadShouldQuit = false;
	mHasNewFrame = false;
	mThread = std::make_shared< std::thread >( std::bind( &CapturePvApi::threadedFunc, this ) );
}

void CapturePvApi::stop()
{
	std::lock_guard< std::mutex > lock( mMutex );
	if ( mThread )
	{
		tPvErr err = PvCaptureQueueClear( mHandle );
		CHECK_PVAPI_ERROR( err );

		mThreadShouldQuit = true;
		mThread->join();
		mThread.reset();
	}
}

void CapturePvApi::threadedFunc()
{
	PvCaptureStart( mHandle );

	tPvFrame frame;
	memset( &frame, 0, sizeof( tPvFrame ) );
	frame.ImageBufferSize = mSensorFrameSize;
	frame.ImageBuffer = new uint8_t[ mSensorFrameSize ];

	tPvErr err = PvCaptureQueueFrame( mHandle, &frame, nullptr );
	CHECK_PVAPI_ERROR( err );
	err = PvAttrEnumSet( mHandle, "FrameStartTriggerMode", "Freerun" );
	CHECK_PVAPI_ERROR( err );
	err = PvAttrEnumSet( mHandle, "AcquisitionMode", "Continuous" );
	CHECK_PVAPI_ERROR( err );
	PvCommandRun( mHandle, "AcquisitionStart" );
	CHECK_PVAPI_ERROR( err );

	while ( ! mThreadShouldQuit )
	{
		err = PvCaptureWaitForFrameDone( mHandle, &frame, PVINFINITE );
		if ( err != ePvErrSuccess )
		{
			CHECK_PVAPI_ERROR( err );
			continue;
		}
		if ( frame.Status == ePvErrSuccess )
		{
			switch ( mPixelFormat )
			{
				case PixelFormat::MONO8:
				{
					std::lock_guard< std::mutex > lock( mMutex );
					Channel8uRef channel = mChannelCache8u->getNewChannel();
					memcpy( channel->getData(), frame.ImageBuffer, frame.ImageBufferSize );
					mCurrentChannel8u = channel;
					mHasNewFrame = true;
					break;
				}

				case PixelFormat::MONO16:
				{
					std::lock_guard< std::mutex > lock( mMutex );
					Channel16uRef channel = mChannelCache16u->getNewChannel();
					memcpy( channel->getData(), frame.ImageBuffer, frame.ImageBufferSize );
					mCurrentChannel16u = channel;
					mHasNewFrame = true;
					break;
				}

				case PixelFormat::MONO12PACKED:
				{
					Channel16uRef channel = mChannelCache16u->getNewChannel();
					uint16_t *dst = channel->getData();
					uint8_t *src = static_cast< uint8_t * >( frame.ImageBuffer );
					size_t n = channel->getWidth() * channel->getHeight();
					for ( size_t i = 0; i < n / 2; i++, dst += 2, src += 3 )
					{
						uint16_t p0 = src[ 0 ];
						uint16_t p01 = src[ 1 ];
						uint16_t p1 = src[ 2 ];
						dst[ 0 ] = ( p0 << 4 ) | (( p01 & 0xf0 ) >> 4 );
						dst[ 1 ] = ( ( p01 & 0xf ) << 8 ) | p1;
					}
					mCurrentChannel16u = channel;
					mHasNewFrame = true;
					break;
				}

				case PixelFormat::RGB24:
				{
					std::lock_guard< std::mutex > lock( mMutex );
					Surface8uRef surface = mSurfaceCache8u->getNewSurface();
					memcpy( surface->getData(), frame.ImageBuffer, frame.ImageBufferSize );
					mCurrentSurface8u = surface;
					mHasNewFrame = true;
					break;
				}

				default:
					break;
			}
		}
		else
		{
			CHECK_PVAPI_ERROR( frame.Status );
		}

		if ( ! mThreadShouldQuit )
		{
			err = PvCaptureQueueFrame( mHandle, &frame, nullptr );
			CHECK_PVAPI_ERROR( err );
		}
	}

	PvCommandRun( mHandle, "AcquisitionStop" );
	PvCaptureEnd( mHandle );

	delete [] static_cast< uint8_t * >( frame.ImageBuffer );
}

bool CapturePvApi::checkNewFrame() const
{
	std::lock_guard< std::mutex > lock( mMutex );
	return mHasNewFrame;
}

Channel8uRef CapturePvApi::getChannel() const
{
	std::lock_guard< std::mutex > lock( mMutex );
	mHasNewFrame = false;
	switch ( mPixelFormat )
	{
		case PixelFormat::MONO8:
			return mCurrentChannel8u;
			break;

		case PixelFormat::MONO16:
		case PixelFormat::MONO12PACKED:
		{
			return Channel8u::create( *mCurrentChannel16u );
			break;
		}

		case PixelFormat::RGB24:
		{
			return Channel8u::create( *mCurrentSurface8u );
			break;
		}

		default:
			return Channel8uRef();
			break;
	}
}

Channel8uRef CapturePvApi::getChannel8u() const
{
	return getChannel();
}

Channel16uRef CapturePvApi::getChannel16u() const
{
	std::lock_guard< std::mutex > lock( mMutex );
	mHasNewFrame = false;
	switch ( mPixelFormat )
	{
		case PixelFormat::MONO8:
			return Channel16u::create( *mCurrentChannel8u );
			break;

		case PixelFormat::MONO16:
		case PixelFormat::MONO12PACKED:
			return mCurrentChannel16u;
			break;

		case PixelFormat::RGB24:
			return Channel16u::create( *mCurrentSurface8u );
			break;

		default:
			return Channel16uRef();
			break;
	}
}

Surface8uRef CapturePvApi::getSurface() const
{
	std::lock_guard< std::mutex > lock( mMutex );
	mHasNewFrame = false;
	switch ( mPixelFormat )
	{
		case PixelFormat::MONO8:
			return Surface8u::create( *mCurrentChannel8u );
			break;

		case PixelFormat::MONO16:
		case PixelFormat::MONO12PACKED:
			return Surface8u::create( *mCurrentChannel16u );
			break;

		case PixelFormat::RGB24:
			return mCurrentSurface8u;
			break;

		default:
			return Surface8uRef();
			break;
	}
}

Surface8uRef CapturePvApi::getSurface8u() const
{
	return getSurface();
}

tPvUint32 CapturePvApi::getAttr( const std::string &name ) const
{
	tPvUint32 attr;
	tPvErr err = PvAttrUint32Get( mHandle, name.c_str(), &attr );
	THROW_ON_PVAPI_ERROR( err );
	return attr;
}

void CapturePvApi::setAttr( const std::string &name, tPvUint32 value ) const
{
	tPvErr err = PvAttrUint32Set( mHandle, name.c_str(), value );
	THROW_ON_PVAPI_ERROR( err );
}

// static
void CapturePvApi::cameraLinkCallback( void *context, tPvInterface interface,
									   tPvLinkEvent event, unsigned long id )
{
	CapturePvApi *capture = static_cast< CapturePvApi * >( context );

	switch( event )
	{
		case ePvLinkAdd:
			CI_LOG_I( "camera added: " << id );
			if ( capture->mDevice->getId() == id )
			{
				capture->openDevice();
			}
			break;

		case ePvLinkRemove:
			CI_LOG_I( "camera removed: " << id );
			if ( capture->mDevice->getId() == id )
			{
				capture->stop();
				// PvApi does not send add event for opened devices, we need to
				// close the device
				capture->closeDevice();
			}
			break;

		default:
			break;
	}
}

} } // mndl::pvapi
