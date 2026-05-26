//
// C++ Encoder/Decoder for the Apple IIgs Super Hi-Res raw screen format.
//
// File extension: .c1     ProDOS file type: $C1     Aux type: $0000
// Total file size on disk: exactly 32768 bytes ($8000).
//
// Layout:
//   $0000..$7CFF  32000 bytes  Pixel data, 200 rows x 160 bytes, 2 nibble-
//                              packed pixels per byte (high nibble = even
//                              column, low nibble = odd column).
//   $7D00..$7DC7    200 bytes  Scanline Control Bytes (one per row).
//                              bit 7  : 0 = 320 mode, 1 = 640 mode
//                              bit 6  : interrupt enable (ignored by files)
//                              bit 5  : color fill (320 mode only)
//                              bits3-0: palette index 0..15
//   $7DC8..$7DFF     56 bytes  Padding (zeros).
//   $7E00..$7FFF    512 bytes  Palette: 16 palettes x 16 colors x 2 bytes.
//                              Each color is little-endian "0000 RRRR GGGG BBBB":
//                              byte0 = GGGGBBBB, byte1 = 0000RRRR.
//
#ifndef C1_FILE_H
#define C1_FILE_H

#include <stdint.h>
#include <vector>

#pragma pack(push, 1)

// 4-bits-per-channel packed BGRA color (same wire format as C16_Color).
typedef struct C1_Color
{
	uint16_t b : 4;
	uint16_t g : 4;
	uint16_t r : 4;
	uint16_t a : 4;
} C1_Color;

// The whole file as one struct. The plugin reads/writes this directly.
typedef struct C1_FileImage
{
	unsigned char pixels[32000];   // 200 rows x 160 bytes
	unsigned char scbs[200];       // one Scanline Control Byte per row
	unsigned char pad[56];         // zeros
	C1_Color      palette[256];    // 16 palettes x 16 colors
} C1_FileImage;

#pragma pack(pop)

static_assert(sizeof(C1_Color) == 2,         "C1_Color must be 2 bytes");
static_assert(sizeof(C1_FileImage) == 32768, "C1_FileImage must be exactly $8000 bytes");


class C1File
{
public:
	// Construct from an existing file on disk. After construction, call
	// IsValid() to check whether the file was a well-formed 32768-byte SHR.
	C1File(const wchar_t* pFilePath);

	// Construct a blank file for saving. widthPixels must be 320 or 640;
	// heightPixels must be 200.
	C1File(int widthPixels, int heightPixels);

	~C1File();

	// True iff the file (load) or arguments (save) were valid.
	bool IsValid() const { return m_valid; }

	// Width Promotion should see (320 or 640). 640 is selected on load
	// whenever any SCB has bit 7 set; on save, this matches the constructor
	// argument.
	int GetWidthPixels() const { return m_widthPixels; }
	int GetHeight()      const { return m_heightPixels; }

	// One image; layout is [m_widthPixels x m_heightPixels] of 8-bit indices,
	// where each index is (scb_palette_bank << 4) | per-pixel-nibble. So the
	// 256-entry RGB palette returned by GetPalette() directly resolves them.
	const std::vector<unsigned char*>& GetPixelMaps() const { return m_pPixelMaps; }

	// 256 colors, in on-disk order (palette 0 entries 0..15, palette 1
	// entries 16..31, ...).
	const C1_Color* GetPalette()    const { return m_palette; }
	int             GetPaletteSize() const { return 256; }

	// 200 SCBs. May be all zeros for newly-constructed save objects.
	const unsigned char* GetSCBs()     const { return m_scbs; }
	int                  GetNumSCBs() const { return 200; }

	// --- Save-side setup ---

	// Replace the entire 256-color palette. rgbTriplets points to 768 bytes
	// (R,G,B,R,G,B,...). Channels are truncated to 4 bits; alpha set to 0xF.
	void SetPalette(const unsigned char* rgbTriplets);

	// Copy the source frame into our internal buffer. The pointer must be
	// (m_widthPixels * m_heightPixels) bytes of 8-bit indices.
	void AddImage(const unsigned char* pixels);

	// Analyse rows, generate SCBs, pack pixels, write 32768 bytes to disk.
	// If any row violates strict SHR rules, the offending pixels are remapped
	// to the nearest legal color (and a sidecar <path>.remap.log is written
	// plus an OutputDebugStringW message issued).
	// Returns true on a successful disk write.
	bool SaveToFile(const wchar_t* pFilenamePath);

private:
	void LoadFromFile(const wchar_t* pFilePath);

	bool m_valid;
	int  m_widthPixels;
	int  m_heightPixels;

	C1_Color      m_palette[256];
	unsigned char m_scbs[200];

	// One frame, malloced to m_widthPixels * m_heightPixels.
	std::vector<unsigned char*> m_pPixelMaps;
};

#endif // C1_FILE_H
