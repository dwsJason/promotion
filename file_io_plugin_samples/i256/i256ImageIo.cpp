// todo file format description

#pragma pack(1)

#include "i256ImageIo.h"
#include "256_file.h"

#include <stdio.h>
#include <malloc.h>

#define GDEBUG 0

// some useful defines
#define FILE_TYPE_ID "de.cosmigo.fileio.256"
#define FILE_BOX_DESCRIPTION L"256 - I256 Image"
#define FILE_EXTENSION L"256"

// at the moment there is only version "1" of the file plugin interface
#define PLUGIN_INTERFACE_VERSION_USED 1

// a simple identifier useful for file type detection
#define FILE_HEADER_TYPE_ID "I256"

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
bool basicDataLoaded = false;
I256File* CurrentFile = nullptr;

unsigned char rgbTable[ 768 ];
unsigned char alphaTable[ 256 ];

#if GDEBUG
volatile bool GWaitAttach = true;

void WaitDebugger()
{
	while (GWaitAttach)
	{
		// spin
	}
}

#endif

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

	CurrentFile = new I256File(currentFileName);

	if (CurrentFile->GetFrameCount() < 1)
	{
		basicDataLoaded = false;
		delete CurrentFile;
		CurrentFile = nullptr;
		// Don't report an error, otherwise we get an error on boot
		// Basically, if the filetype doesn't match, do not report an error
		//wcscpy( lastErrorMessage, ERROR_FILE_OPEN_FAILED );
		return false;
	}

	fileHeader.width = CurrentFile->GetWidth();
	fileHeader.height = CurrentFile->GetHeight();

	const I256_Palette& pal = CurrentFile->GetPalette();

	for (int idx = 0; idx < pal.iNumColors;++idx)
	{
		int rgbIndex = idx * 3;
		alphaTable[idx] = pal.pColors[idx].a;
		rgbTable[rgbIndex+0] = pal.pColors[idx].r;
		rgbTable[rgbIndex+1] = pal.pColors[idx].g;
		rgbTable[rgbIndex+2] = pal.pColors[idx].b;
	}

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
		//if ( basicDataLoaded )
		//	return 0;

		return -1;
	}

	bool  __stdcall isAlphaEnabled()
	{
		if ( basicDataLoaded )
			return false;

		return false;
	}

	bool  __stdcall loadNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned short* delayMs )
	{
		if ( !basicDataLoaded )
			return false;

		updateProgress( 0 );


		const std::vector<unsigned char*>& PixelMaps = CurrentFile->GetPixelMaps();
		int Width = CurrentFile->GetWidth();
		int Height = CurrentFile->GetHeight();

		if (PixelMaps.size() > 0)
		{
			memcpy(colorFrame, PixelMaps[0], Width*Height);
		}

		updateProgress( 50 );

		//const I256_Palette& Palette = CurrentFile->GetPalette();

		//for (int idx = 0; idx < Palette.iNumColors; ++idx)
		//{
		//	int rgbindex = 3*idx;
		//
		//	colorFramePalette[rgbindex+0] = Palette.pColors[idx].r;
		//	colorFramePalette[rgbindex+1] = Palette.pColors[idx].g;
		//	colorFramePalette[rgbindex+3] = Palette.pColors[idx].b;
		//	//alphaFramePalette[idx]        = Palette.pColors[idx].a;
		//}
		memcpy(colorFramePalette, rgbTable, 768);

		updateProgress( 100 );

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

		updateProgress( 0 );

		return true;
	}

	bool __stdcall writeNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned char* rgba, unsigned short delayMs )
	{

		if (CurrentFile)
		{
			delete CurrentFile;
			CurrentFile = new I256File(fileHeader.width, fileHeader.height, 256);
		}

		const I256_Palette& Palette = CurrentFile->GetPalette();

		for (int idx = 0; idx < Palette.iNumColors; ++idx)
		{
			int rgbindex = 3*idx;

			Palette.pColors[idx].r = colorFramePalette[rgbindex+0];
			Palette.pColors[idx].g = colorFramePalette[rgbindex+1];
			Palette.pColors[idx].b = colorFramePalette[rgbindex+3];
			Palette.pColors[idx].a = 255; //alphaFramePalette[idx];
		}

		std::vector<unsigned char*> pixels;
		pixels.push_back(colorFrame);
		CurrentFile->AddImages(pixels);

		updateProgress( 60 );

		CurrentFile->SaveToFile(currentFileName);

		updateProgress( 100 );

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
#if GDEBUG
	WaitDebugger();
#endif

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


