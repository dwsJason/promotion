// c1 file I/O plugin shim. Wraps C1File for the Promotion plugin contract.
// Handles a single raw 32768-byte Apple IIgs SHR screen (.c1).
//
// NOTE: do NOT add a translation-unit-wide `#pragma pack(1)` here. The on-disk
// structs (C1_Color, C1_FileImage) manage their own packing with push/pop in
// c1_file.h. A global pack(1) would leak into the C1File *class* definition
// (it sits after the pop in the header), giving this TU a packed layout while
// c1_file.cpp compiles the constructor with default alignment -- the two then
// disagree on member offsets and the object is corrupt on construction.

#include "c1ImageIo.h"
#include "c1_file.h"

#include <stdio.h>
#include <malloc.h>

#define GDEBUG 0

#define FILE_TYPE_ID "de.cosmigo.fileio.c1"
#define FILE_BOX_DESCRIPTION L"C1 - Apple IIgs SHR (raw 32K)"
#define FILE_EXTENSION L"c1"

// Plugin interface version
#define PLUGIN_INTERFACE_VERSION_USED 1

// error messages
#define ERROR_NO_FILE_NAME    L"No file given to load!"
#define ERROR_FILE_OPEN_FAILED L"Could not open file!"
#define ERROR_BAD_DIMENSIONS  L"C1 plugin only supports 320x200 or 640x200 images."

wchar_t lastErrorMessage[2048];
ProgressCallback progressCallback = NULL;

// Filename Promotion last gave us, normalized (CiderPress aux-type suffix stripped).
wchar_t currentFileName[2048];

bool basicDataLoaded = false;
C1File* CurrentFile = nullptr;

// Promotion-side 256-entry RGB palette (8 bits/channel).
unsigned char rgbTable[768];

// Header info cached for getWidth/getHeight.
struct
{
	int width;
	int height;
} fileHeader;

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

//------------------------------------------------------------------------------
static void resetError()
{
	lastErrorMessage[0] = 0;
}

static bool isError()
{
	return lastErrorMessage[0] != 0;
}

static void resetBasicData()
{
	basicDataLoaded = false;
	fileHeader.width = -1;
	fileHeader.height = -1;
	memset(rgbTable, 0, sizeof(rgbTable));
	if (CurrentFile)
	{
		delete CurrentFile;
		CurrentFile = nullptr;
	}
	resetError();
}

// 4-bit channel -> 8-bit channel: 0x0..0xF -> 0x00..0xFF
static inline unsigned char expand4to8(unsigned char n)
{
	n &= 0x0F;
	return (unsigned char)((n << 4) | n);
}

static inline bool isHex(wchar_t c)
{
	return (c >= L'0' && c <= L'9')
	    || (c >= L'a' && c <= L'f')
	    || (c >= L'A' && c <= L'F');
}

// CiderPress-style filenames carry the ProDOS type/aux as an "#XXXXXXXX"
// suffix (8 hex digits). The plugin accepts both:
//   foo.c1            (plain)
//   foo.c1#c10000     (extension + suffix)
//   foo#c10000        (suffix only, no extension)
// This function strips any trailing "#XXXXXXXX" so file-open uses the
// actual on-disk filename — we don't ever rewrite filenames on save.
static void normalizeCiderPressSuffix(wchar_t* path)
{
	size_t len = wcslen(path);
	if (len < 9) return;

	// Look for "#" followed by exactly 8 hex digits, ending the basename.
	// The suffix is always at the end of the string.
	if (path[len - 9] != L'#') return;
	for (size_t i = len - 8; i < len; ++i)
	{
		if (!isHex(path[i])) return;
	}
	path[len - 9] = 0;
}

// auto load basic file information from the given file
static bool ensureBasicData()
{
	resetError();

	if (basicDataLoaded)
		return true;

	if (currentFileName[0] == 0)
	{
		wcscpy_s(lastErrorMessage, 2048, ERROR_NO_FILE_NAME);
		return false;
	}

	CurrentFile = new C1File(currentFileName);

	if (!CurrentFile->IsValid())
	{
		delete CurrentFile;
		CurrentFile = nullptr;
		// Silent on probe — Promotion asks every plugin about every file.
		return false;
	}

	fileHeader.width  = CurrentFile->GetWidthPixels();
	fileHeader.height = CurrentFile->GetHeight();

	const C1_Color* pal = CurrentFile->GetPalette();
	for (int i = 0; i < 256; ++i)
	{
		rgbTable[i * 3 + 0] = expand4to8((unsigned char)pal[i].r);
		rgbTable[i * 3 + 1] = expand4to8((unsigned char)pal[i].g);
		rgbTable[i * 3 + 2] = expand4to8((unsigned char)pal[i].b);
	}

	basicDataLoaded = true;
	return true;
}

static void updateProgress(int progress)
{
	if (progressCallback != NULL)
	{
		(*progressCallback)(progress);
	}
}

//------------------------------------------------------------------------------

extern "C"
{
	bool __stdcall initialize(char* /*language*/, unsigned short* version, bool* animation)
	{
		resetError();
		*version = PLUGIN_INTERFACE_VERSION_USED;
		*animation = false;
		currentFileName[0] = 0;
		return true;
	}

	void __stdcall setProgressCallback(ProgressCallback callback)
	{
		::progressCallback = callback;
	}

	wchar_t* __stdcall getErrorMessage()
	{
		if (!isError()) return NULL;
		return lastErrorMessage;
	}

	char* __stdcall getFileTypeId()
	{
		resetError();
		return (char*)FILE_TYPE_ID;
	}

	bool __stdcall isReadSupported()      { resetError(); return true;  }
	bool __stdcall isWriteSupported()     { resetError(); return true;  }
	bool __stdcall isWriteTrueColorSupported() { resetError(); return false; }

	wchar_t* __stdcall getFileBoxDescription() { resetError(); return (wchar_t*)FILE_BOX_DESCRIPTION; }
	wchar_t* __stdcall getFileExtension()      { resetError(); return (wchar_t*)FILE_EXTENSION; }

	void __stdcall setFilename(wchar_t* filename)
	{
		resetError();

		if (wcscmp(currentFileName, filename) != 0)
		{
			resetBasicData();
			wcscpy_s(currentFileName, 2048, filename);
			normalizeCiderPressSuffix(currentFileName);
		}
	}

	bool __stdcall canHandle()
	{
		return ensureBasicData();
	}

	bool __stdcall loadBasicData()
	{
		return ensureBasicData();
	}

	int __stdcall getWidth()  { return fileHeader.width;  }
	int __stdcall getHeight() { return fileHeader.height; }

	int __stdcall getImageCount() { resetError(); return 1; }

	bool __stdcall canExtractPalette() { resetError(); return true; }

	unsigned char* __stdcall getRgbPalette()
	{
		if (basicDataLoaded) return rgbTable;
		return NULL;
	}

	int __stdcall getTransparentColor() { return -1; }
	bool __stdcall isAlphaEnabled()      { return false; }

	bool __stdcall loadNextImage(unsigned char* colorFrame,
	                             unsigned char* colorFramePalette,
	                             unsigned char* /*alphaFrame*/,
	                             unsigned char* /*alphaFramePalette*/,
	                             unsigned short* /*delayMs*/)
	{
		if (!basicDataLoaded) return false;

		updateProgress(0);

		const std::vector<unsigned char*>& maps = CurrentFile->GetPixelMaps();
		int w = CurrentFile->GetWidthPixels();
		int h = CurrentFile->GetHeight();

		if (!maps.empty())
		{
			memcpy(colorFrame, maps[0], (size_t)w * (size_t)h);
		}

		updateProgress(50);

		memcpy(colorFramePalette, rgbTable, 768);

		updateProgress(100);
		return true;
	}

	bool __stdcall beginWrite(int width, int height,
	                          int /*transparentColor*/, bool /*alphaEnabled*/,
	                          int /*numberOfFrames*/)
	{
		resetBasicData();

		if ((width != 320 && width != 640) || height != 200)
		{
			wcscpy_s(lastErrorMessage, 2048, ERROR_BAD_DIMENSIONS);
			return false;
		}

		fileHeader.width  = width;
		fileHeader.height = height;

		updateProgress(0);
		return true;
	}

	bool __stdcall writeNextImage(unsigned char* colorFrame,
	                              unsigned char* colorFramePalette,
	                              unsigned char* /*alphaFrame*/,
	                              unsigned char* /*alphaFramePalette*/,
	                              unsigned char* /*rgba*/,
	                              unsigned short /*delayMs*/)
	{
		if (CurrentFile)
		{
			delete CurrentFile;
			CurrentFile = nullptr;
		}

		CurrentFile = new C1File(fileHeader.width, fileHeader.height);
		if (!CurrentFile->IsValid())
		{
			wcscpy_s(lastErrorMessage, 2048, ERROR_BAD_DIMENSIONS);
			return false;
		}

		CurrentFile->SetPalette(colorFramePalette);
		CurrentFile->AddImage(colorFrame);

		updateProgress(60);

		bool ok = CurrentFile->SaveToFile(currentFileName);

		updateProgress(100);

		if (!ok && !isError())
		{
			wcscpy_s(lastErrorMessage, 2048, ERROR_FILE_OPEN_FAILED);
		}
		return ok;
	}

	void __stdcall finishProcessing()
	{
		resetBasicData();
		updateProgress(0);
	}
}

//------------------------------------------------------------------------------
BOOL APIENTRY DllMain(HANDLE /*hModule*/,
                      DWORD ul_reason_for_call,
                      LPVOID /*lpReserved*/)
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
