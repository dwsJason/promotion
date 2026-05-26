// DLL export shim for the c1 (Apple IIgs SHR) file I/O plugin.

#ifdef c1ImageIO_EXPORTS
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
