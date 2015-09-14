#include "cinder/Log.h"
#include "cinder/Utilities.h"
#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"

#include "CapturePvApiParams.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class PvApiTestApp : public App
{
 public:
	void setup() override;
	void update() override;
	void draw() override;

	void keyDown( KeyEvent event ) override;

	void cleanup() override;

 private:
	params::InterfaceGlRef mParams;

	mndl::pvapi::CapturePvApiParamsRef mCapturePvApi;

	gl::Texture2dRef mTexture;

	std::shared_ptr< std::thread > mThread;
	bool mThreadShouldQuit = false;
	void openCameraThreadFn();
	std::string mCameraProgress;
};

void PvApiTestApp::setup()
{
	mndl::pvapi::CapturePvApi::init();

	mThread = std::make_shared< std::thread >(
			std::bind( &PvApiTestApp::openCameraThreadFn, this ) );

	mParams = params::InterfaceGl::create( "Parameters", ivec2( 200, 300 ) );
	mParams->setPosition( ivec2( 10 ) );

	mParams->addParam( "Camera", &mCameraProgress, true );
	mParams->addButton( "Start", [ this ]() { if ( mCapturePvApi ) mCapturePvApi->start(); } );
	mParams->addButton( "Stop", [ this ]() { if ( mCapturePvApi ) mCapturePvApi->stop(); } );
}

void PvApiTestApp::openCameraThreadFn()
{
	mCameraProgress = "Connecting...";
	while ( ! mThreadShouldQuit )
	{
		if ( mndl::pvapi::CapturePvApi::getNumDevices() > 0 )
		{
			break;
		}
		ci::sleep( 100 );
	}

	if ( ! mThreadShouldQuit )
	{
		try
		{
			mCapturePvApi = mndl::pvapi::CapturePvApiParams::create();
		}
		catch ( const mndl::pvapi::CapturePvApiExc &exc )
		{
			mCameraProgress = exc.what();
			CI_LOG_EXCEPTION( "CapturePvApi:", exc );
		}

		if ( mCapturePvApi )
		{
			mCapturePvApi->getParams()->setPosition( ivec2( 220, 10 ) );
			// set Rgb24 or Mono8 pixel format
			if ( PvAttrEnumSet( mCapturePvApi->getPvHandle(), "PixelFormat", "Rgb24" ) != ePvErrSuccess )
			{
				CHECK_PVAPI_ERROR( PvAttrEnumSet( mCapturePvApi->getPvHandle(), "PixelFormat", "Mono8" ) );
			}

			mCapturePvApi->start();
			mCameraProgress = "Connected.";
		}
	}
}

void PvApiTestApp::update()
{
	if ( mCapturePvApi && mCapturePvApi->checkNewFrame() )
	{
		auto surface = mCapturePvApi->getSurface();
		if ( surface ) // can get null surface for not supported modes
		{
			mTexture = gl::Texture2d::create( *surface );
		}
	}
}

void PvApiTestApp::draw()
{
	gl::viewport( getWindowSize() );
	gl::setMatricesWindow( getWindowSize() );

	gl::clear();

	if ( mTexture )
	{
		gl::draw( mTexture, getWindowBounds() );
	}

	mParams->draw();
}

void PvApiTestApp::keyDown( KeyEvent event )
{
	switch ( event.getCode() )
	{
		case KeyEvent::KEY_f:
			setFullScreen( ! isFullScreen() );
			break;

		case KeyEvent::KEY_ESCAPE:
			quit();
			break;

		default:
			break;
	}
}

void PvApiTestApp::cleanup()
{
	if ( mThread )
	{
		mThreadShouldQuit = true;
		mThread->join();
		mThread.reset();
	}

	mCapturePvApi.reset();

	mndl::pvapi::CapturePvApi::cleanup();
}

CINDER_APP( PvApiTestApp, RendererGl )
