#include <algorithm>

#include "cinder/Log.h"
#include "cinder/Utilities.h"
#include "cinder/app/App.h"
#include "cinder/app/Window.h"

#include "CapturePvApiParams.h"

using namespace ci;

namespace mndl { namespace pvapi {

CapturePvApiParams::CapturePvApiParams( const DeviceRef &device ) :
	CapturePvApiParams( app::App::get()->getWindow(), device )
{
}

CapturePvApiParams::CapturePvApiParams( const app::WindowRef &window, const DeviceRef &device ) :
	CapturePvApi( device )
{
	setupParams( window );
}

void CapturePvApiParams::setupParams( const app::WindowRef &window )
{
	mParams = params::InterfaceGl::create( window, mDevice->getCameraName() + " " + mDevice->getSerialNumber(),
			ivec2( 200, 300 ) );

	std::vector< std::string > dataTypeStr = { "ePvDatatypeUnknown",
		"ePvDatatypeCommand", "ePvDatatypeRaw", "ePvDatatypeString",
		"ePvDatatypeEnum", "ePvDatatypeUint32", "ePvDatatypeFloat32",
		"ePvDatatypeInt64", "ePvDatatypeBoolean" };

	tPvAttrListPtr listPtr;
	unsigned long listLength;
	if ( PvAttrList( mHandle, &listPtr, &listLength ) == ePvErrSuccess )
	{
		tPvAttributeInfo attrInfo;
		for ( int i = 0; i < listLength; i++ )
		{
			const char *attrName = listPtr[ i ];

			PvAttrInfo( mHandle, attrName, &attrInfo );
			auto categories = split( attrInfo.Category, '/' );

			// remove empty tokens causing opening, trailing separators
			categories.erase( std::remove_if( categories.begin(), categories.end(),
								[]( const std::string &s ) { return s.empty(); } ),
							  categories.end() );

			switch ( attrInfo.Datatype )
			{
				case ePvDatatypeCommand:
				{
					mParams->addButton( attrName,
							[ this, attrName ]()
							{
								CHECK_PVAPI_ERROR( PvCommandRun( mHandle, attrName ) );
							} );
					break;
				}

				case ePvDatatypeUint32:
				{
					// NOTE: tPvUint32 is defined as unsigned long, which is
					// not always 32-bit, although the limits from the camera
					// suggest that these are indeed 32-bit values
					std::function< void( uint32_t )> setter =
						[ this, attrName ]( uint32_t v )
						{
							tPvUint32 pv = v;
							CHECK_PVAPI_ERROR( PvAttrUint32Set( mHandle, attrName, pv ) );
						};
					std::function< uint32_t () > getter =
						[ this, attrName ]() -> uint32_t
						{
							tPvUint32 pv;
							CHECK_PVAPI_ERROR( PvAttrUint32Get( mHandle, attrName, &pv ) );
							return uint32_t( pv );
						};

					tPvUint32 minLimit, maxLimit;
					CHECK_PVAPI_ERROR( PvAttrRangeUint32( mHandle, attrName, &minLimit, &maxLimit ) );

					mParams->addParam( attrName, setter, getter ).min( minLimit ).max( maxLimit );
					break;
				}

				case ePvDatatypeFloat32:
				{
					std::function< void( float )> setter =
						[ this, attrName ]( float v )
						{
							CHECK_PVAPI_ERROR( PvAttrFloat32Set( mHandle, attrName, v ) );
						};
					std::function< float () > getter =
						[ this, attrName ]() -> float
						{
							tPvFloat32 pv;
							CHECK_PVAPI_ERROR( PvAttrFloat32Get( mHandle, attrName, &pv ) );
							return pv;
						};
					tPvFloat32 minLimit, maxLimit;
					CHECK_PVAPI_ERROR( PvAttrRangeFloat32( mHandle, attrName, &minLimit, &maxLimit ) );

					mParams->addParam( attrName, setter, getter ).min( minLimit ).max( maxLimit ).step( 0.1f );
					break;
				}

				case ePvDatatypeInt64:
				{
					// NOTE: tPvInt64( long long ) is not supported by ATB
					// using double instead
					std::function< void( double )> setter =
						[ this, attrName ]( double v )
						{
							tPvInt64 pv( v );
							CHECK_PVAPI_ERROR( PvAttrInt64Set( mHandle, attrName, pv ) );
						};
					std::function< double () > getter =
						[ this, attrName ]() -> double
						{
							tPvInt64 pv;
							CHECK_PVAPI_ERROR( PvAttrInt64Get( mHandle, attrName, &pv ) );
							return double( pv );
						};

					tPvInt64 minLimit, maxLimit;
					CHECK_PVAPI_ERROR( PvAttrRangeInt64( mHandle, attrName, &minLimit, &maxLimit ) );

					mParams->addParam( attrName, setter, getter ).min( minLimit ).max( maxLimit );
					break;
				}

				case ePvDatatypeEnum:
				{
					char enumRangeStr[ 4096 ];
					CHECK_PVAPI_ERROR( PvAttrRangeEnum( mHandle, attrName, enumRangeStr, 4096, nullptr ) );

					std::vector< std::string > enumNames = split( enumRangeStr, ',' );

					std::function< void( int i )> setter =
						[ this, attrName, enumNames ]( int i )
						{
							CHECK_PVAPI_ERROR( PvAttrEnumSet( mHandle, attrName, enumNames[ i ].c_str() ) );
						};
					std::function< int () > getter =
						[ this, attrName, enumNames ]() -> int
						{
							char str[ 128 ];
							CHECK_PVAPI_ERROR( PvAttrStringGet( mHandle, attrName, str, 128, nullptr ) );
							auto it = std::find( enumNames.begin(), enumNames.end(), str );
							return it - enumNames.begin();
						};

					mParams->addParam( attrName, enumNames, setter, getter );
					break;
				}

				case ePvDatatypeString:
				{
					std::function< void( std::string )> setter =
						[ this, attrName ]( std::string str )
						{
							CHECK_PVAPI_ERROR( PvAttrStringSet( mHandle, attrName, str.c_str() ) );
						};
					std::function< std::string () > getter =
						[ this, attrName ]() -> std::string
						{
							char str[ 128 ];
							CHECK_PVAPI_ERROR( PvAttrStringGet( mHandle, attrName, str, 128, nullptr ) );
							return std::string( str );
						};
					mParams->addParam( attrName, setter, getter );
					break;
				}

				case ePvDatatypeBoolean:
				{
					// NOTE: tPvBoolean is defined as unsigned char
					std::function< void( bool v )> setter =
						[ this, attrName ]( bool v )
						{
							tPvBoolean pv = v;
							CHECK_PVAPI_ERROR( PvAttrBooleanSet( mHandle, attrName, pv ) );
						};
					std::function< bool () > getter =
						[ this, attrName ]() -> bool
						{
							tPvBoolean pv;
							CHECK_PVAPI_ERROR( PvAttrBooleanGet( mHandle, attrName, &pv ) );
							return bool( pv );
						};
					mParams->addParam( attrName, setter, getter );
					break;
				}

				default:
					CI_LOG_W( "not supported datatype " + dataTypeStr[ attrInfo.Datatype ] + " " + attrName );
					continue;
					break;
			}

			bool readOnly = ( attrInfo.Flags & ePvFlagConst ) || ( ! attrInfo.Flags & ePvFlagWrite );
			if ( readOnly )
			{
				mParams->setOptions( attrName, "readonly=true" );
			}

			if ( ! categories.empty() )
			{
				mParams->setOptions( attrName, "group=`" + categories.back() + "`" );

				for ( size_t i = categories.size() - 1; i > 0; i-- )
				{
					mParams->setOptions( categories[ i ], "group=`" + categories[ i - 1 ] + "`" );
				}

				for ( const auto &cat : categories )
				{
					mParams->setOptions( cat, "opened=false" );
				}
			}
		}
	}
}

} } // mndl::pvapi
