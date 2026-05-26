
// DLL export/import macro. Files in this DLL are compiled with the
// i16ImageIO_EXPORTS preprocessor define so that symbols are exported.
// Consumers leave the define unset and see them as imports.
#ifdef i16ImageIO_EXPORTS
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "..\pluginInterface.h"
