#pragma once

#include <memory>
#include <vector>

#include "cinder/Channel.h"

template< typename T >
class ChannelCacheT
{
  public:
	ChannelCacheT( int32_t width, int32_t height, size_t numChannels ) :
		mWidth( width ), mHeight( height )
	{
		for ( size_t i = 0; i < numChannels; ++i )
		{
			mChannelData.push_back( std::shared_ptr< T >( new T[ width * height ],
						std::default_delete< T[] >() ) );
			mChannelUsed.push_back( false );
		}
	}

	void resize( int32_t width, int32_t height )
	{
		mWidth = width;
		mHeight = height;
	}

	std::shared_ptr< ci::ChannelT< T > > getNewChannel()
	{
		for ( size_t i = 0; i < mChannelData.size(); ++i )
		{
			if ( ! mChannelUsed[ i ] )
			{
				mChannelUsed[ i ] = true;
				auto newChannel = new ci::ChannelT< T >( mWidth, mHeight, mWidth * sizeof( T ), 1, mChannelData[ i ].get() );
				auto result = std::shared_ptr< ci::ChannelT< T > >
					( newChannel, [ = ] ( ci::ChannelT< T > *c  )
								  { mChannelUsed[ i ] = false; } );
				return result;
			}
		}

		return ci::ChannelT< T >::create( mWidth, mHeight );
	}

  private:
	std::vector< std::shared_ptr< T > > mChannelData;
	std::vector< bool > mChannelUsed;
	int32_t mWidth, mHeight;
};

typedef ChannelCacheT< uint8_t > ChannelCache;
typedef ChannelCacheT< uint8_t > ChannelCache8u;
typedef std::shared_ptr< ChannelCache8u > ChannelCacheRef;
typedef std::shared_ptr< ChannelCache8u > ChannelCache8uRef;

typedef ChannelCacheT< uint16_t > ChannelCache16u;
typedef std::shared_ptr< ChannelCache16u > ChannelCache16uRef;

typedef ChannelCacheT< float > ChannelCache32f;
typedef std::shared_ptr< ChannelCache32f > ChannelCache32fRef;
