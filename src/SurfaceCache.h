#pragma once

#include <vector>

#include "cinder/Cinder.h"
#include "cinder/Surface.h"

template< typename T >
class SurfaceCacheT
{
  public:
	SurfaceCacheT( int32_t width, int32_t height, ci::SurfaceChannelOrder sco, int numSurfaces ) :
		mWidth( width ), mHeight( height ), mSCO( sco )
	{
		for ( int i = 0; i < numSurfaces; ++i )
		{
			mSurfaceData.push_back( std::shared_ptr< T >( new T[ width * height * sco.getPixelInc() ], std::default_delete< T [] >() ) );
			mSurfaceUsed.push_back( false );
		}
	}

	void resize( int32_t width, int32_t height )
	{
		mWidth = width;
		mHeight = height;
	}

	std::shared_ptr< ci::SurfaceT< T > > getNewSurface()
	{
		// try to find an available block of pixel data to wrap a surface around
		for ( size_t i = 0; i < mSurfaceData.size(); ++i )
		{
			if ( ! mSurfaceUsed[ i ] )
			{
				mSurfaceUsed[ i ] = true;
				auto newSurface = new ci::SurfaceT< T >( mSurfaceData[ i ].get(), mWidth, mHeight, mWidth * mSCO.getPixelInc(), mSCO );
				std::shared_ptr< ci::SurfaceT< T > > result = std::shared_ptr< ci::SurfaceT< T > >( newSurface, [=] ( ci::SurfaceT< T > *s ) { mSurfaceUsed[ i ] = false; } );
				return result;
			}
		}

		// we couldn't find an available surface, so we'll need to allocate one
		return ci::SurfaceT< T >::create( mWidth, mHeight, mSCO.hasAlpha(), mSCO );
	}

  private:
	std::vector< std::shared_ptr< T > > mSurfaceData;
	std::vector< bool > mSurfaceUsed;
	int32_t mWidth, mHeight;
	ci::SurfaceChannelOrder mSCO;
};

typedef SurfaceCacheT< uint8_t > SurfaceCache;
typedef SurfaceCacheT< uint8_t > SurfaceCache8u;
typedef std::shared_ptr< SurfaceCache8u > SurfaceCacheRef;
typedef std::shared_ptr< SurfaceCache8u > SurfaceCache8uRef;

typedef SurfaceCacheT< uint16_t > SurfaceCache16u;
typedef std::shared_ptr< SurfaceCache16u > SurfaceCache16uRef;

typedef SurfaceCacheT< float > SurfaceCache32f;
typedef std::shared_ptr< SurfaceCache32f > SurfaceCache32fRef;
