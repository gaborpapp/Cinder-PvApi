#pragma once

#include "cinder/params/Params.h"

#include "CapturePvApi.h"

namespace cinder { namespace app {
	class Window;
	typedef std::shared_ptr< Window > WindowRef;
} }

namespace mndl { namespace pvapi {

typedef std::shared_ptr< class CapturePvApiParams > CapturePvApiParamsRef;

class CapturePvApiParams : public CapturePvApi
{
 public:
	static CapturePvApiParamsRef create( const DeviceRef &device = DeviceRef() )
	{ return CapturePvApiParamsRef( new CapturePvApiParams( device ) ); }

	static CapturePvApiParamsRef create( const cinder::app::WindowRef &window, const DeviceRef &device = DeviceRef() )
	{ return CapturePvApiParamsRef( new CapturePvApiParams( window, device ) ); }

	ci::params::InterfaceGlRef getParams() { return mParams; }

 protected:
	CapturePvApiParams( const DeviceRef &device );
	CapturePvApiParams( const cinder::app::WindowRef &window, const DeviceRef &device );

	ci::params::InterfaceGlRef mParams;

	void setupParams( const ci::app::WindowRef &window );
};

} } // mndl::pvapi
