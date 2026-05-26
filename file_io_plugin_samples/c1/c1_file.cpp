//
// C1File: encoder/decoder for the Apple IIgs raw 32768-byte SHR screen.
// See c1_file.h for the on-disk layout.
//

#include "c1_file.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

//------------------------------------------------------------------------------
// 640-mode pixel column -> palette offset within the row's 16-color bank.
// 640 mode packs 4 pixels per byte: bits 7-6, 5-4, 3-2, 1-0. Each 2-bit pixel
// is looked up in a 4-color sub-palette of the row's bank.
//   column %4 == 0  ->  bank + 8..11
//   column %4 == 1  ->  bank + 12..15
//   column %4 == 2  ->  bank + 0..3
//   column %4 == 3  ->  bank + 4..7
static const int k640ColumnBase[4] = { 8, 12, 0, 4 };

//------------------------------------------------------------------------------
// Construct from file.
//
C1File::C1File(const wchar_t* pFilePath)
	: m_valid(false)
	, m_widthPixels(0)
	, m_heightPixels(0)
{
	memset(m_palette, 0, sizeof(m_palette));
	memset(m_scbs, 0, sizeof(m_scbs));
	LoadFromFile(pFilePath);
}

//------------------------------------------------------------------------------
// Construct blank for save.
//
C1File::C1File(int widthPixels, int heightPixels)
	: m_valid(false)
	, m_widthPixels(widthPixels)
	, m_heightPixels(heightPixels)
{
	memset(m_palette, 0, sizeof(m_palette));
	memset(m_scbs, 0, sizeof(m_scbs));

	if ((widthPixels == 320 || widthPixels == 640) && heightPixels == 200)
		m_valid = true;
}

//------------------------------------------------------------------------------
C1File::~C1File()
{
	for (size_t i = 0; i < m_pPixelMaps.size(); ++i)
	{
		delete[] m_pPixelMaps[i];
	}
	m_pPixelMaps.clear();
}

//------------------------------------------------------------------------------
void C1File::LoadFromFile(const wchar_t* pFilePath)
{
	FILE* pFile = nullptr;
#ifdef _WIN32
	errno_t err = _wfopen_s(&pFile, pFilePath, L"rb");
	if (err != 0 || pFile == nullptr)
		return;
#else
	// Non-Windows builds (e.g. local syntax check): widechar fopen is not
	// portable; this branch is only here so the file compiles cleanly.
	(void)pFilePath;
	return;
#endif

	fseek(pFile, 0, SEEK_END);
	long fileLen = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	if (fileLen != (long)sizeof(C1_FileImage))
	{
		fclose(pFile);
		return;
	}

	C1_FileImage img;
	size_t got = fread(&img, 1, sizeof(img), pFile);
	fclose(pFile);

	if (got != sizeof(img))
		return;

	// Decide presented width: 640 if any SCB has bit 7 set.
	bool any640 = false;
	for (int y = 0; y < 200; ++y)
	{
		if (img.scbs[y] & 0x80)
		{
			any640 = true;
			break;
		}
	}
	m_widthPixels  = any640 ? 640 : 320;
	m_heightPixels = 200;

	// Copy palette and SCBs verbatim.
	memcpy(m_palette, img.palette, sizeof(m_palette));
	memcpy(m_scbs,    img.scbs,    sizeof(m_scbs));

	// Allocate pixel map.
	size_t frameSize = (size_t)m_widthPixels * (size_t)m_heightPixels;
	unsigned char* pFrame = new unsigned char[frameSize];
	memset(pFrame, 0, frameSize);
	m_pPixelMaps.push_back(pFrame);

	// Decode each row.
	for (int y = 0; y < 200; ++y)
	{
		unsigned char scb = img.scbs[y];
		int bank = (scb & 0x0F) << 4;          // pre-shifted palette base
		bool is640 = (scb & 0x80) != 0;
		const unsigned char* pRowSrc = img.pixels + (size_t)y * 160;
		unsigned char* pRowDst = pFrame + (size_t)y * m_widthPixels;

		if (!is640)
		{
			// 320 mode: 160 bytes -> 320 indices, nibble unpack, OR with bank.
			if (m_widthPixels == 320)
			{
				for (int x = 0; x < 160; ++x)
				{
					unsigned char b = pRowSrc[x];
					pRowDst[(x << 1)    ] = (unsigned char)(bank | ((b >> 4) & 0x0F));
					pRowDst[(x << 1) + 1] = (unsigned char)(bank | (b        & 0x0F));
				}
			}
			else
			{
				// Presented as 640: pixel-double each 320-mode pixel.
				for (int x = 0; x < 160; ++x)
				{
					unsigned char b = pRowSrc[x];
					unsigned char hi = (unsigned char)(bank | ((b >> 4) & 0x0F));
					unsigned char lo = (unsigned char)(bank | (b        & 0x0F));
					int dst = x << 2;
					pRowDst[dst    ] = hi;
					pRowDst[dst + 1] = hi;
					pRowDst[dst + 2] = lo;
					pRowDst[dst + 3] = lo;
				}
			}
		}
		else
		{
			// 640 mode: 4 pixels per byte, column-based palette offset.
			for (int x = 0; x < 160; ++x)
			{
				unsigned char b = pRowSrc[x];
				int baseCol = x << 2;
				pRowDst[baseCol    ] = (unsigned char)(bank | (k640ColumnBase[0] + ((b >> 6) & 0x03)));
				pRowDst[baseCol + 1] = (unsigned char)(bank | (k640ColumnBase[1] + ((b >> 4) & 0x03)));
				pRowDst[baseCol + 2] = (unsigned char)(bank | (k640ColumnBase[2] + ((b >> 2) & 0x03)));
				pRowDst[baseCol + 3] = (unsigned char)(bank | (k640ColumnBase[3] + ((b     ) & 0x03)));
			}
		}
	}

	m_valid = true;
}

//------------------------------------------------------------------------------
void C1File::SetPalette(const unsigned char* rgbTriplets)
{
	for (int i = 0; i < 256; ++i)
	{
		// 8-bit -> 4-bit: keep top nibble. Top nibble of the on-disk color
		// is reserved (canonical IIgs writes 0). Leave alpha = 0.
		m_palette[i].r = (uint16_t)(rgbTriplets[i * 3 + 0] >> 4);
		m_palette[i].g = (uint16_t)(rgbTriplets[i * 3 + 1] >> 4);
		m_palette[i].b = (uint16_t)(rgbTriplets[i * 3 + 2] >> 4);
		m_palette[i].a = 0;
	}
}

//------------------------------------------------------------------------------
void C1File::AddImage(const unsigned char* pixels)
{
	// Discard any prior frame.
	for (size_t i = 0; i < m_pPixelMaps.size(); ++i)
		delete[] m_pPixelMaps[i];
	m_pPixelMaps.clear();

	if (!m_valid || pixels == nullptr)
		return;

	size_t n = (size_t)m_widthPixels * (size_t)m_heightPixels;
	unsigned char* p = new unsigned char[n];
	memcpy(p, pixels, n);
	m_pPixelMaps.push_back(p);
}

//------------------------------------------------------------------------------
// Internal helpers for save.
//

namespace {

// Squared RGB distance between two 4-bit palette entries.
int colorDist(const C1_Color& a, const C1_Color& b)
{
	int dr = (int)a.r - (int)b.r;
	int dg = (int)a.g - (int)b.g;
	int db = (int)a.b - (int)b.b;
	return dr * dr + dg * dg + db * db;
}

// Find the palette index in [lo, lo+count) whose color is closest to
// palette[wantIdx]. Used when remapping an out-of-range pixel to the
// nearest legal index in its column's legal range.
unsigned char nearestInRange(const C1_Color* palette, unsigned char wantIdx,
                             int lo, int count)
{
	int bestIdx = lo;
	int bestDist = colorDist(palette[wantIdx], palette[lo]);
	for (int i = 1; i < count; ++i)
	{
		int d = colorDist(palette[wantIdx], palette[lo + i]);
		if (d < bestDist)
		{
			bestDist = d;
			bestIdx = lo + i;
		}
	}
	return (unsigned char)bestIdx;
}

// For each candidate palette bank P, count how many row pixels would already
// be in-range. Best bank wins (lower bank breaks ties).
//
// Validity per pixel:
//   320-mode: pixel index >> 4 == P
//   640-mode: pixel index in [P*16 + k640ColumnBase[col%4],
//                              P*16 + k640ColumnBase[col%4] + 4)
//
// rowPixels has rowWidth entries.
int pickBest320Bank(const unsigned char* rowPixels, int rowWidth)
{
	int bestBank = 0;
	int bestCount = -1;
	for (int P = 0; P < 16; ++P)
	{
		int count = 0;
		for (int x = 0; x < rowWidth; ++x)
		{
			if ((rowPixels[x] >> 4) == P)
				++count;
		}
		if (count > bestCount)
		{
			bestCount = count;
			bestBank = P;
		}
	}
	return bestBank;
}

int pickBest640Bank(const unsigned char* rowPixels)
{
	int bestBank = 0;
	int bestCount = -1;
	for (int P = 0; P < 16; ++P)
	{
		int count = 0;
		for (int x = 0; x < 640; ++x)
		{
			int lo = (P << 4) + k640ColumnBase[x & 3];
			unsigned char v = rowPixels[x];
			if ((int)v >= lo && (int)v < lo + 4)
				++count;
		}
		if (count > bestCount)
		{
			bestCount = count;
			bestBank = P;
		}
	}
	return bestBank;
}

// True if every pair (2x, 2x+1) is identical AND every pixel uses bank P.
bool rowCanCollapseTo320(const unsigned char* row640, int P)
{
	int pBase = P << 4;
	for (int x = 0; x < 640; x += 2)
	{
		if (row640[x] != row640[x + 1]) return false;
		unsigned char v = row640[x];
		if (v < pBase || v >= pBase + 16) return false;
	}
	return true;
}

void appendLog(std::vector<wchar_t>& log, const wchar_t* line)
{
	while (*line) log.push_back(*line++);
	log.push_back(L'\n');
}

} // anonymous namespace

//------------------------------------------------------------------------------
bool C1File::SaveToFile(const wchar_t* pFilenamePath)
{
	if (!m_valid || m_pPixelMaps.empty())
		return false;

	const unsigned char* src = m_pPixelMaps[0];

	C1_FileImage out;
	memset(&out, 0, sizeof(out));
	memcpy(out.palette, m_palette, sizeof(m_palette));

	// Remap log (lines separated by '\n').
	std::vector<wchar_t> log;
	int totalRemaps = 0;
	wchar_t lineBuf[160];

	if (m_widthPixels == 320)
	{
		// Every row is 320-mode. Pick the best bank per row; remap pixels that
		// fall in other banks to the nearest in-bank color.
		for (int y = 0; y < 200; ++y)
		{
			const unsigned char* row = src + (size_t)y * 320;
			int P = pickBest320Bank(row, 320);
			int pBase = P << 4;
			int rowRemaps = 0;

			for (int x = 0; x < 320; x += 2)
			{
				unsigned char a = row[x    ];
				unsigned char b = row[x + 1];
				if ((a >> 4) != P)
				{
					a = nearestInRange(m_palette, a, pBase, 16);
					++rowRemaps;
				}
				if ((b >> 4) != P)
				{
					b = nearestInRange(m_palette, b, pBase, 16);
					++rowRemaps;
				}
				out.pixels[(size_t)y * 160 + (x >> 1)] =
					(unsigned char)(((a & 0x0F) << 4) | (b & 0x0F));
			}

			out.scbs[y] = (unsigned char)(P & 0x0F); // 320 mode, no fill, no IRQ

			if (rowRemaps > 0)
			{
				totalRemaps += rowRemaps;
				swprintf(lineBuf, 160,
				    L"row %3d (320-mode, bank %2d): remapped %d pixel(s) to nearest in-bank color",
				    y, P, rowRemaps);
				appendLog(log, lineBuf);
			}
		}
	}
	else // 640
	{
		for (int y = 0; y < 200; ++y)
		{
			const unsigned char* row = src + (size_t)y * 640;
			int P320 = pickBest320Bank(row, 640);

			if (rowCanCollapseTo320(row, P320))
			{
				// Save as 320-mode (no detail loss).
				int pBase = P320 << 4;
				for (int x = 0; x < 320; ++x)
				{
					unsigned char v = row[x << 1]; // pair is identical
					out.pixels[(size_t)y * 160 + (x >> 1)] =
						(x & 1)
						? (out.pixels[(size_t)y * 160 + (x >> 1)] | (unsigned char)(v & 0x0F))
						: (unsigned char)((v & 0x0F) << 4);
				}
				out.scbs[y] = (unsigned char)(P320 & 0x0F); // 320 mode
				continue;
			}

			// 640-mode SCB. Pick the best bank.
			int P = pickBest640Bank(row);
			int rowRemaps = 0;

			for (int x = 0; x < 160; ++x)
			{
				int baseCol = x << 2;
				unsigned char packed = 0;
				for (int sub = 0; sub < 4; ++sub)
				{
					int col = baseCol + sub;
					unsigned char want = row[col];
					int lo = (P << 4) + k640ColumnBase[sub];
					int hi = lo + 4;
					unsigned char use;
					if ((int)want >= lo && (int)want < hi)
					{
						use = want;
					}
					else
					{
						use = nearestInRange(m_palette, want, lo, 4);
						++rowRemaps;
					}
					unsigned int twoBit = (unsigned int)((int)use - lo) & 0x03;
					packed = (unsigned char)(packed | (twoBit << (6 - 2 * sub)));
				}
				out.pixels[(size_t)y * 160 + x] = packed;
			}

			out.scbs[y] = (unsigned char)(0x80 | (P & 0x0F)); // 640 mode

			if (rowRemaps > 0)
			{
				totalRemaps += rowRemaps;
				swprintf(lineBuf, 160,
				    L"row %3d (640-mode, bank %2d): remapped %d pixel(s) to nearest legal column-color",
				    y, P, rowRemaps);
				appendLog(log, lineBuf);
			}
		}
	}

	// Write the file.
	FILE* pFile = nullptr;
#ifdef _WIN32
	errno_t err = _wfopen_s(&pFile, pFilenamePath, L"wb");
	if (err != 0 || pFile == nullptr)
		return false;
#else
	(void)pFilenamePath;
	return false;
#endif

	size_t written = fwrite(&out, 1, sizeof(out), pFile);
	fclose(pFile);

	if (written != sizeof(out))
		return false;

	// Sidecar log + debug-output for any remaps.
	if (totalRemaps > 0)
	{
#ifdef _WIN32
		wchar_t header[160];
		swprintf(header, 160,
		    L"c1 save: remapped %d pixel(s) across the image to fit SHR rules\n",
		    totalRemaps);
		OutputDebugStringW(header);

		// Emit each row's line to the debugger, one at a time.
		size_t i = 0;
		while (i < log.size())
		{
			wchar_t buf[256];
			size_t j = 0;
			while (i < log.size() && log[i] != L'\n' && j < 254)
				buf[j++] = log[i++];
			buf[j++] = L'\n';
			buf[j] = 0;
			OutputDebugStringW(buf);
			if (i < log.size() && log[i] == L'\n') ++i;
		}

		// Sidecar log: <path>.remap.log
		size_t pathLen = wcslen(pFilenamePath);
		std::vector<wchar_t> logPath(pathLen + 16);
		wcscpy_s(&logPath[0], pathLen + 16, pFilenamePath);
		wcscat_s(&logPath[0], pathLen + 16, L".remap.log");

		FILE* pLog = nullptr;
		if (_wfopen_s(&pLog, &logPath[0], L"w, ccs=UTF-8") == 0 && pLog != nullptr)
		{
			fputws(header, pLog);
			std::vector<wchar_t> nulTerm(log);
			nulTerm.push_back(0);
			fputws(&nulTerm[0], pLog);
			fclose(pLog);
		}
#endif
	}

	return true;
}
