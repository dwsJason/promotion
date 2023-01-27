// todo file format description

#pragma pack(1)

#include "simImageIo.h"

#include <stdio.h>
#include <malloc.h>



// some useful defines
#define FILE_TYPE_ID "de.cosmigo.fileio.sim"
#define FILE_BOX_DESCRIPTION L"SIM - Sample Image"
#define FILE_EXTENSION L"sim"

// at the moment there is only version "1" of the file plugin interface
#define PLUGIN_INTERFACE_VERSION_USED 1

// a simple identifier useful for file type detection
#define FILE_HEADER_TYPE_ID "SIMG"

// error messages (just English!)
#define ERROR_NO_FILE_NAME L"No file given to load!"
#define ERROR_FILE_OPEN_FAILED L"Could not open file!"
#define ERROR_FILE_READ_FAILED L"Could not read file!"
#define ERROR_FILE_WRITE_FAILED L"Could not write file!"

// latest error message
wchar_t lastErrorMessage[2048];

// progress callback to forward progress information
ProgressCallback progressCallback= NULL;

// file name that is currently defined
wchar_t currentFileName[2048];

// flag needed for auto loading basic file information data (i.e. file header)
bool basicDataLoaded;

unsigned char rgbTable[ 768 ];
unsigned char alphaTable[ 256 ];

// Header data of the file
struct 
{
	char typeId[4];
	unsigned char version;
	
	unsigned int width;
	unsigned int height;
	int transparentColor;
	bool alphaEnabled;

} fileHeader;

// helper method to reset the previous error message
void resetError()
{
	lastErrorMessage[0]= 0;
}

// check if there is an error
bool isError()
{
	return lastErrorMessage[0]!=0;
}

// reset internal data to be ready for the next file io
void resetBasicData()
{
	basicDataLoaded= false;
	fileHeader.width= -1;
	fileHeader.height= -1;
	fileHeader.transparentColor= -1;
	fileHeader.alphaEnabled= false;
	memset( rgbTable, 0, 768 );
	memset( alphaTable, 0, 256 );
	
	resetError();
}

// auto load basic file information from the given file
bool ensureBasicData()
{
	resetError();

	// already done?
	if ( basicDataLoaded )
		return true;

	if ( currentFileName[0]== 0 )
	{
		wcscpy( lastErrorMessage, ERROR_NO_FILE_NAME );
		return  false;
	}

	// open file and read header data plus color palette
	FILE* file= _wfopen( currentFileName, L"rb" ); 
	if ( file==NULL )
	{
		wcscpy( lastErrorMessage, ERROR_FILE_OPEN_FAILED );
		return false;
	}

	// read file type and version. we assume there may be no file smaller than 5 bytes!
	if ( fread( &fileHeader, 1, 5, file ) != 5 )
	{
		wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
		fclose( file );
		return false;
	}

	// if type and version does not match then stop here
	if ( fileHeader.version != 1 || strncmp( fileHeader.typeId, FILE_HEADER_TYPE_ID, 4 ) != 0 )
	{
		fclose( file );
		return false;
	}

    // read rest of header
	if ( fread( &fileHeader.width, 1, sizeof( fileHeader ) - 5, file ) != (sizeof( fileHeader ) - 5) )
	{
		wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
		fclose( file );
		return false;
	}

	// read colors
	if ( fread( rgbTable, 1, sizeof( rgbTable ), file ) != sizeof( rgbTable ) )
	{
		wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
		fclose( file );
		return false;
	}

	// read alpha values if enabled
	if ( fileHeader.alphaEnabled )
	{
		if ( fread( alphaTable, 1, sizeof( alphaTable ), file ) != sizeof( alphaTable ) )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
			fclose( file );
			return false;
		}
	}
	
	fclose( file );

	basicDataLoaded= true;
	return true;
}

// helper to forward progress if a callback exists
void updateProgress( int progress )
{
	if ( progressCallback!=NULL )
	{
		(*progressCallback)( progress );
	}
}



extern "C"
{
	bool __stdcall initialize( char* language, unsigned short* version, bool* animation )
	{
		resetError();

		// we don't care about language in this sample!
		
		*version= PLUGIN_INTERFACE_VERSION_USED;
		*animation= false; // it's an image processing plugin

		currentFileName[0]= 0; // no initial file name

		return true;
	}

	void __stdcall setProgressCallback( ProgressCallback progressCallback )
	{
		::progressCallback= progressCallback;
	}

	wchar_t* __stdcall getErrorMessage()
	{
		if ( !isError() )
			return NULL;

		return lastErrorMessage;
	}

	char* __stdcall getFileTypeId()
	{
		resetError();
		return FILE_TYPE_ID;
	}

	bool  __stdcall isReadSupported()
	{
		resetError();
		return true;
	}

	bool  __stdcall isWriteSupported()
	{
		resetError();
		return true;
	}

	bool  __stdcall isWriteTrueColorSupported()
	{
		resetError();
		return false;
	}

	wchar_t* __stdcall getFileBoxDescription()
	{
		resetError();
		return FILE_BOX_DESCRIPTION;
	}

	wchar_t* __stdcall getFileExtension()
	{
		resetError();
		return FILE_EXTENSION;
	}

	void  __stdcall setFilename( wchar_t* filename )
	{
		resetError();

		// only reset data if a new file is selected!
		if ( wcscmp( currentFileName, filename )!=0 ) 
		{
			resetBasicData();
			wcscpy( currentFileName, filename );
		}
	}

	bool  __stdcall canHandle()
	{
		// To speed things up, we should only check if the file is supported by reading only the first bytes
		// of the file. We make it simpler here.
		return ensureBasicData();
	}

	bool  __stdcall loadBasicData()
	{
		return ensureBasicData();
	}

	int   __stdcall getWidth()
	{
		return fileHeader.width;
	}

	int   __stdcall getHeight()
	{
		return fileHeader.height;
	}

	int  __stdcall getImageCount()
	{
		resetError();
		return 1;
	}

	bool __stdcall canExtractPalette()
	{
		resetError();
		return true;
	}

	unsigned char* __stdcall getRgbPalette()
	{
		if ( basicDataLoaded )
			return rgbTable;

		return NULL;
	}

	int   __stdcall getTransparentColor()
	{
		if ( basicDataLoaded )
			return fileHeader.transparentColor;

		return -1;
	}

	bool  __stdcall isAlphaEnabled()
	{
		if ( basicDataLoaded )
			return fileHeader.alphaEnabled;

		return false;
	}

	bool  __stdcall loadNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned short* delayMs )
	{
		if ( !basicDataLoaded )
			return false;

		// reset progress
		updateProgress( 0 );

		// open file and read 
		FILE* file= _wfopen( currentFileName, L"rb" ); 
		if ( file==NULL )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_OPEN_FAILED );
			return false;
		}

		// we already loaded header data including palette, so we copy the palette here
		memcpy( colorFramePalette, rgbTable, sizeof( rgbTable ) );
		if ( fileHeader.alphaEnabled && alphaFramePalette!=NULL )
			memcpy( alphaFramePalette, alphaTable, sizeof( alphaTable ) );


		// continue with image data
		int bitmapPos= sizeof( fileHeader ) + 768;
		if ( fileHeader.alphaEnabled )
			bitmapPos+= 256;
		if ( fseek( file, bitmapPos, SEEK_SET ) != 0 )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
			fclose( file );
			return false;
		}

		// read color bitmap data
		if ( fread( colorFrame, 1, fileHeader.width * fileHeader.height, file ) != fileHeader.width * fileHeader.height )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
			fclose( file );
			return false;
		}

		// set progress
		updateProgress( 50 );

		// if enabled read alpha bitmap data
		if ( fileHeader.alphaEnabled && alphaFrame!=NULL &&
			 fread( alphaFrame, 1, fileHeader.width * fileHeader.height, file ) != fileHeader.width * fileHeader.height )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_READ_FAILED );
			fclose( file );
			return false;
		}

		// set progress
		updateProgress( 100 );
		
		fclose( file );
		return true;
	}

	bool  __stdcall beginWrite( int width, int height, int transparentColor, bool alphaEnabled, int numberOfFrames )
	{
		resetBasicData();

		// set up file header. We do not actually write yet!
		strncpy( fileHeader.typeId, FILE_HEADER_TYPE_ID, 4 );
		fileHeader.version= 1;
		fileHeader.width= width;
		fileHeader.height= height;
		fileHeader.transparentColor= transparentColor;
		fileHeader.alphaEnabled= alphaEnabled;

		// reset progress
		updateProgress( 0 );

		return true;
	}

	bool __stdcall writeNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned char* rgba, unsigned short delayMs )
	{

		// open file and write
		FILE* file= _wfopen( currentFileName, L"w+b" ); 
		if ( file==NULL )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_OPEN_FAILED );
			return false;
		}

		// write header
		if ( fwrite( &fileHeader, 1, sizeof( fileHeader ), file ) != sizeof( fileHeader ) )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_WRITE_FAILED );
			fclose( file );
			return false;
		}

		// write color data
		if ( fwrite( colorFramePalette, 1, 768, file ) != 768 )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_WRITE_FAILED );
			fclose( file );
			return false;
		}

		// set progress
		updateProgress( 30 );

		// if enabled write alpha data
		if ( fileHeader.alphaEnabled && fwrite( alphaFramePalette, 1, 256, file ) != 256 )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_WRITE_FAILED );
			fclose( file );
			return false;
		}

		// write color bitmap data
		if ( fwrite( colorFrame, 1, fileHeader.width * fileHeader.height, file ) != fileHeader.width * fileHeader.height )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_WRITE_FAILED );
			fclose( file );
			return false;
		}

		// set progress
		updateProgress( 60 );

		// if enabled write alpha bitmap data
		if ( fileHeader.alphaEnabled && fwrite( alphaFrame, 1, fileHeader.width * fileHeader.height, file ) != fileHeader.width * fileHeader.height )
		{
			wcscpy( lastErrorMessage, ERROR_FILE_WRITE_FAILED );
			fclose( file );
			return false;
		}

		// set progress
		updateProgress( 100 );
		
		fclose( file );
		return true;
	}

	void  __stdcall finishProcessing()
	{
		resetBasicData();


		// set progress back to 0 to hide the progress bar
		updateProgress( 0 );
	}
}



//--------------------------------------------------------------------------------

BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}


