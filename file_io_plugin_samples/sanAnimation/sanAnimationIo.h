
// Der folgende ifdef-Block zeigt die Standardlösung zur Erstellung von Makros, die das Exportieren 
// aus einer DLL vereinfachen. Alle Dateien in dieser DLL wurden mit dem in der Befehlszeile definierten
// Symbol sanAnimationIO_EXPORTS kompiliert. Dieses Symbol sollte für kein Projekt definiert werden, das
// diese DLL verwendet. Auf diese Weise betrachtet jedes andere Projekt, dessen Quellcodedateien diese Datei 
// einbeziehen, DLL_EXPORT-Funktionen als aus einer DLL importiert, während diese DLL mit diesem 
// Makro definierte Symbole als exportiert betrachtet.
#ifdef sanAnimationIO_EXPORTS
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


// Fügen Sie hier Ihre Header-Dateien ein
#define WIN32_LEAN_AND_MEAN		// Selten benutzte Teile der Windows-Header nicht einbinden

#include <windows.h>
#include "..\pluginInterface.h"





