
#ifndef PluginInterface_h
#define PluginInterface_h 1

typedef void (__stdcall *ProgressCallback)( int progress );

// function definitions that must be published by the dll
extern "C"
{
	bool __stdcall initialize( char* language, unsigned short* version, bool* animation );

	void __stdcall setProgressCallback( ProgressCallback progressCallback );

	wchar_t* __stdcall getErrorMessage(); 

	char* __stdcall getFileTypeId(); 

	bool  __stdcall isReadSupported(); 

	bool  __stdcall isWriteSupported(); 

	bool  __stdcall isWriteTrueColorSupported();

	wchar_t* __stdcall getFileBoxDescription(); 

	wchar_t* __stdcall getFileExtension(); 

	void  __stdcall setFilename( wchar_t* filename ); 

	bool  __stdcall canHandle(); 

	bool  __stdcall loadBasicData(); 

	int   __stdcall getWidth(); 

	int   __stdcall getHeight(); 

	int   __stdcall getImageCount(); 

	bool  __stdcall canExtractPalette(); 

	unsigned char* __stdcall getRgbPalette(); 

	int   __stdcall getTransparentColor(); 

	bool  __stdcall isAlphaEnabled(); 

	bool  __stdcall loadNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned short* delayMs ); 

	bool  __stdcall beginWrite( int width, int height, int transparentColor, bool alphaEnabled, int numberOfFrames ); 

	bool  __stdcall writeNextImage( unsigned char* colorFrame, unsigned char* colorFramePalette, unsigned char* alphaFrame, unsigned char* alphaFramePalette, unsigned char* rgba, unsigned short delayMs ); 

	void  __stdcall finishProcessing(); 
}


#endif