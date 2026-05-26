// i16 file I/O plugin shim. Wraps C16File for the Promotion plugin contract.

#pragma pack(1)

#include "i16ImageIo.h"
#include "16_file.h"

#include <stdio.h>
#include <malloc.h>

#define GDEBUG 0

// some useful defines
#define FILE_TYPE_ID "de.cosmigo.fileio.16"
#define FILE_BOX_DESCRIPTION L"16 - I16 Image"
#define FILE_EXTENSION L"16"

// at the moment there is only version "1" of the file plugin interface
#define PLUGIN_INTERFACE_VERSION_USED 1

// a simple identifier useful for file type detection (matches header magic)
#define FILE_HEADER_TYPE_ID "I16I"

// error messages (just English!)
#define ERROR_NO_FILE_NAME L"No file given to load!"
#define ERROR_FILE_OPEN_FAILED L"Could not open file!"
#define ERROR_FILE_READ_FAILED L"Could not read file!"
#define ERROR_FILE_WRITE_FAILED L"Could not write file!"

// latest error message
wchar_t lastErrorMessage[2048];

// progress callback to forward progress information
ProgressCallback progressCallback = NULL;

// file name that is currently defined
wchar_t currentFileName[2048];

// flag needed for auto loading basic file information data (i.e. file header)
bool basicDataLoaded = false;
C16File* CurrentFile = nullptr;

// Promotion expects 256 entries of RGB / alpha; only first 16 are meaningful for i16.
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

	unsigned int width;     // pixels
	unsigned int height;    // pixels
	int transparentColor;
	bool alphaEnabled;

} fileHeader;

// helper method to reset the previous error message
void resetError()
{
	lastErrorMessage[0] = 0;
}

// check if there is an error
bool isError()
{
	return lastErrorMessage[0] != 0;
}

// reset internal data to be ready for the next file io
void resetBasicData()
{
	basicDataLoaded = false;
	fileHeader.width = -1;
	fileHeader.height = -1;
	fileHeader.transparentColor = -1;
	fileHeader.alphaEnabled = false;
	memset(rgbTable, 0, 768);
	memset(alphaTable, 0, 256);

	resetError();
}

// 4-bit channel -> 8-bit channel: 0x0..0xF -> 0x00..0xFF
static inline unsigned char expand4to8(unsigned char n)
{
	n &= 0x0F;
	return (unsigned char)((n << 4) | n);
}

// auto load basic file information from the given file
bool ensureBasicData()
{
	resetError();

	// already done?
	if (basicDataLoaded)
		return true;

	if (currentFileName[0] == 0)
	{
		wcscpy(lastErrorMessage, ERROR_NO_FILE_NAME);
		return false;
	}

	CurrentFile = new C16File(currentFileName);

	if (CurrentFile->GetFrameCount() < 1)
	{
		basicDataLoaded = false;
		delete CurrentFile;
		CurrentFile = nullptr;
		// Don't report an error here, or we get an error on boot when Promotion
		// asks every plugin if it can handle some unrelated file.
		return false;
	}

	fileHeader.width = CurrentFile->GetWidthPixels();
	fileHeader.height = CurrentFile->GetHeight();

	const C16_Palette& pal = CurrentFile->GetPalette();

	for (int idx = 0; idx < pal.iNumColors; ++idx)
	{
		int rgbIndex = idx * 3;
		alphaTable[idx]      = expand4to8((unsigned char)pal.pColors[idx].a);
		rgbTable[rgbIndex+0] = expand4to8((unsigned char)pal.pColors[idx].r);
		rgbTable[rgbIndex+1] = expand4to8((unsigned char)pal.pColors[idx].g);
		rgbTable[rgbIndex+2] = expand4to8((unsigned char)pal.pColors[idx].b);
	}

	basicDataLoaded = true;
	return true;
}

// helper to forward progress if a callback exists
void updateProgress(int progress)
{
	if (progressCallback != NULL)
	{
		(*progressCallback)(progress);
	}
}



extern "C"
{
	bool __stdcall initialize(char* language, unsigned short* version, bool* animation)
	{
		resetError();

		// we don't care about language in this sample!

		*version = PLUGIN_INTERFACE_VERSION_USED;
		*animation = false; // it's an image processing plugin

		currentFileName[0] = 0; // no initial file name

		return true;
	}

	void __stdcall setProgressCallback(ProgressCallback progressCallback)
	{
		::progressCallback = progressCallback;
	}

	wchar_t* __stdcall getErrorMessage()
	{
		if (!isError())
			return NULL;

		return lastErrorMessage;
	}

	char* __stdcall getFileTypeId()
	{
		resetError();
		return FILE_TYPE_ID;
	}

	bool __stdcall isReadSupported()
	{
		resetError();
		return true;
	}

	bool __stdcall isWriteSupported()
	{
		resetError();
		return true;
	}

	bool __stdcall isWriteTrueColorSupported()
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

	void __stdcall setFilename(wchar_t* filename)
	{
		resetError();

		// only reset data if a new file is selected!
		if (wcscmp(currentFileName, filename) != 0)
		{
			resetBasicData();
			wcscpy(currentFileName, filename);
		}
	}

	bool __stdcall canHandle()
	{
		// Quick-check optimization possible (read just the 16-byte header) but
		// we mirror i256's approach: defer to a full load attempt.
		return ensureBasicData();
	}

	bool __stdcall loadBasicData()
	{
		return ensureBasicData();
	}

	int __stdcall getWidth()
	{
		return fileHeader.width;
	}

	int __stdcall getHeight()
	{
		return fileHeader.height;
	}

	int __stdcall getImageCount()
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
		if (basicDataLoaded)
			return rgbTable;

		return NULL;
	}

	int __stdcall getTransparentColor()
	{
		return -1;
	}

	bool __stdcall isAlphaEnabled()
	{
		return false;
	}

	bool __stdcall loadNextImage(unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned short* delayMs)
	{
		if (!basicDataLoaded)
			return false;

		updateProgress(0);

		const std::vector<unsigned char*>& PixelMaps = CurrentFile->GetPixelMaps();
		int Width = CurrentFile->GetWidthPixels();
		int Height = CurrentFile->GetHeight();

		if (PixelMaps.size() > 0)
		{
			memcpy(colorFrame, PixelMaps[0], Width * Height);
		}

		updateProgress(50);

		// rgbTable was populated by ensureBasicData with 8-bit expanded values.
		// Only the first 16 entries are meaningful; the rest stay zero.
		memcpy(colorFramePalette, rgbTable, 768);

		updateProgress(100);

		return true;
	}

	bool __stdcall beginWrite(int width, int height, int transparentColor, bool alphaEnabled, int numberOfFrames)
	{
		resetBasicData();

		// set up file header. We do not actually write yet!
		strncpy(fileHeader.typeId, FILE_HEADER_TYPE_ID, 4);
		fileHeader.version = 1;
		fileHeader.width = width;
		fileHeader.height = height;
		fileHeader.transparentColor = transparentColor;
		fileHeader.alphaEnabled = alphaEnabled;

		updateProgress(0);

		return true;
	}

	bool __stdcall writeNextImage(unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned char* rgba, unsigned short delayMs)
	{
		if (CurrentFile)
		{
			delete CurrentFile;
			CurrentFile = nullptr;
		}

		int srcWidth  = (int)fileHeader.width;
		int srcHeight = (int)fileHeader.height;

		// On disk the .16 format stores width in bytes (2 pixels per byte).
		int widthBytes = (srcWidth + 1) / 2;
		int padWidth   = widthBytes * 2;          // always even

		// AddImages reads padWidth*srcHeight bytes from the source pointer, but
		// Promotion sized colorFrame as srcWidth*srcHeight. Pad on odd widths.
		unsigned char* paddedFrame = nullptr;
		unsigned char* framePtr    = colorFrame;
		if (padWidth != srcWidth)
		{
			paddedFrame = new unsigned char[ (size_t)padWidth * (size_t)srcHeight ];
			memset(paddedFrame, 0, (size_t)padWidth * (size_t)srcHeight);
			for (int y = 0; y < srcHeight; ++y)
			{
				memcpy(paddedFrame + (size_t)y * padWidth,
				       colorFrame  + (size_t)y * srcWidth,
				       srcWidth);
			}
			framePtr = paddedFrame;
		}

		CurrentFile = new C16File(widthBytes, srcHeight, 16);

		const C16_Palette& Palette = CurrentFile->GetPalette();

		// Truncate Promotion's 8-bit palette down to 4 bits per channel.
		for (int idx = 0; idx < Palette.iNumColors; ++idx)
		{
			int rgbindex = 3 * idx;

			Palette.pColors[idx].r = (unsigned char)(colorFramePalette[rgbindex+0] >> 4);
			Palette.pColors[idx].g = (unsigned char)(colorFramePalette[rgbindex+1] >> 4);
			Palette.pColors[idx].b = (unsigned char)(colorFramePalette[rgbindex+2] >> 4);
			Palette.pColors[idx].a = 0xF; // opaque
		}

		std::vector<unsigned char*> pixels;
		pixels.push_back(framePtr);
		CurrentFile->AddImages(pixels);

		if (paddedFrame)
		{
			delete[] paddedFrame;
		}

		updateProgress(60);

		CurrentFile->SaveToFile(currentFileName);

		updateProgress(100);

		return true;
	}

	void __stdcall finishProcessing()
	{
		resetBasicData();

		// set progress back to 0 to hide the progress bar
		updateProgress(0);
	}
}



//--------------------------------------------------------------------------------

BOOL APIENTRY DllMain(HANDLE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved)
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
