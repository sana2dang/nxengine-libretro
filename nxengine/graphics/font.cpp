#include "LRSDL_video.h"
#include "../nx.h"
#include "font.h"
#include "font.fdh"

#include "libretro_shared.h"

static int text_draw(int x, int y, const char *text, int spacing=0, NXFont *font=&whitefont);

#ifdef FRONTEND_SUPPORTS_RGB565
#define SCREEN_BPP 16
#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_MASK (0x1f << 11)
#define GREEN_MASK (0x3f << 5)
#define BLUE_MASK (0x1f << 0)
#else
#define SCREEN_BPP 15
#define RED_SHIFT 10
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_MASK (0x1f << 10)
#define GREEN_MASK (0x1f << 5)
#define BLUE_MASK (0x1f << 0)
#endif


static SDL_Surface *sdl_screen = NULL;
static SDL_Surface *shadesfc = NULL;

static bool initialized = false;
static bool rendering = true;
static bool shrink_spaces = true;
static int fontheight = 0;

NXFont whitefont;
//NXFont greenfont;
NXFont bluefont;		// used for "F3:Options" text on pause screen
//NXFont shadowfont;		// white letters w/ drop shadow

#include "../libretro/bitmap_font.h"

bool font_init(void)
{
	bool error = false;
    LRSDL_RWops *rw = LRSDL_RWFromMem(font_bmp, sizeof(font_bmp));
    LRSDL_RWops *krrw = LRSDL_RWFromMem(kr_font_bmp, sizeof(kr_font_bmp));

	// we'll be bypassing the NXSurface automatic scaling features
	// and drawing at the real resolution so we can get better-looking fonts.
	sdl_screen = screen->fSurface;

	SDL_Surface *font = LRSDL_LoadBMP_RW(rw, 1);
	SDL_Surface *krfont = LRSDL_LoadBMP_RW(krrw, 1);
	SetColorKey(font, SDL_SRCCOLORKEY, 0);
	SetColorKey(krfont, SDL_SRCCOLORKEY, 0);

	error |= whitefont.InitChars(font, 0xffffff);
	error |= whitefont.InitKrChars(krfont, 0xffffff);
//	error |= greenfont.InitChars(font, 0xffffff); // Workaround for avoiding diacritics to show in green color
	error |= bluefont.InitCharsShadowed(font, 0xffffff, 0x000000); // Workaround for avoiding diacritics not showing on map location names
	error |= bluefont.InitKrCharsShadowed(krfont, 0xffffff, 0x000000); // Workaround for avoiding diacritics not showing on map location names
//	error |= shadowfont.InitCharsShadowed(font, 0xffffff, 0x000000);
	
	error |= create_shade_sfc();

	FreeSurface(font);
	FreeSurface(krfont);

	if (error) return 1;

	fontheight = 0;
	for (char c = 'A'; c <= 'Z'; c++)
	{
		if (whitefont.letters[c]->h > fontheight)
			fontheight = whitefont.letters[c]->h;
	}

	for (char c = 'a'; c <= 'z'; c++)
	{
		if (whitefont.letters[c]->h > fontheight)
			fontheight = whitefont.letters[c]->h;
	}

	if (whitefont.krletters[0]->h > fontheight)
		fontheight = whitefont.krletters[0]->h;

	initialized = true;
	return 0;
}

void font_close(void)
{
}

bool font_reload()
{
	if (!initialized)
		return 0;
	
	whitefont.free();
//	greenfont.free();
	bluefont.free();
//	shadowfont.free();
	
	return font_init();
}

/*
void c------------------------------() {}
*/

NXFont::NXFont()
{
	memset(letters, 0, sizeof(letters));
	memset(krletters, 0, sizeof(krletters));
}

NXFont::~NXFont()
{
	free();
}

void NXFont::free()
{
	for(int i=0;i<NUM_LETTERS_RENDERED;i++)
	{
		if (letters[i])
			FreeSurface(letters[i]);
		letters[i] = NULL;
	}
	for(int i=0;i<NUM_KR_LETTERS_RENDERED;i++)	// added kr + symbol letters
	{
		if (krletters[i])
			FreeSurface(krletters[i]);
		krletters[i] = NULL;
	}
}

static void set_color(SDL_Surface *font, uint16_t color, uint16_t key)
{
	for (unsigned h = 0; h < font->h; h++)
	{
		uint16_t *pixels = (uint16_t*)font->pixels + h * (font->pitch / 2);
		for (unsigned w = 0; w < font->w; w++)
		{
			if (pixels[w] != key)
				pixels[w] = color;
		}
	}
}

bool NXFont::InitChars(SDL_Surface *font, uint32_t color)
{
	SDL_Color fgcolor;
	SDL_Surface *letter;

	fgcolor.r = (uint8_t)(color >> 16);
	fgcolor.g = (uint8_t)(color >> 8);
	fgcolor.b = (uint8_t)(color);

	//char str[2];
	//str[1] = 0;
	uint16_t blue = 0x1f;

	for(int i=1;i<NUM_LETTERS_RENDERED;i++)
	{
		//str[0] = i;

      letter = (SDL_Surface*)AllocNewSurface(0, 6, 10);

		SDL_Rect src = {0};

		src.w = 5;
		src.h = 10;
		src.x = (i % 16) * 16;
		src.y = (i / 16) * 16;

		SDL_Rect dst = {0};
		dst.w = letter->w;
		dst.h = letter->h;

		SetColorKey(letter, SDL_SRCCOLORKEY, 0x1f);
		FillRectangle(letter, NULL, 0x1f);

		DrawBlit(font, &src, letter, &dst);

		uint16 color = fgcolor.r << RED_SHIFT
		| fgcolor.g << GREEN_SHIFT
		| fgcolor.b << BLUE_SHIFT;

		set_color(letter, color, blue);

		letters[i] = letter;
	}

	return 0;
}

// create a font with a drop-shadow (used for "MNA" stage-name displays)
bool NXFont::InitCharsShadowed(SDL_Surface *font, uint32_t color, uint32_t shadowcolor)
{
	SDL_Color fgcolor, bgcolor;
	SDL_Surface *top, *bottom;
	SDL_Rect dstrect;
	const int offset = 2;

	fgcolor.r = (uint8_t)(color >> 16);
	fgcolor.g = (uint8_t)(color >> 8);
	fgcolor.b = (uint8_t)(color);

	bgcolor.r = (uint8_t)(shadowcolor >> 16);
	bgcolor.g = (uint8_t)(shadowcolor >> 8);
	bgcolor.b = (uint8_t)(shadowcolor);

	char str[2];
	str[1] = 0;


	for(int i=1;i<NUM_LETTERS_RENDERED;i++)
	{
		str[0] = i;

		uint16_t blue = 0x1f;

		top = (SDL_Surface*)AllocNewSurface(0, 6, 10);
		bottom = (SDL_Surface*)AllocNewSurface(0, 6, 10);

		FillRectangle(top, NULL, blue);
		FillRectangle(bottom, NULL, blue);
		SetColorKey(top, SDL_SRCCOLORKEY, blue);
		SetColorKey(bottom, SDL_SRCCOLORKEY, blue);

		SDL_Rect src = {0};
		src.w = 5;
		src.h = 10;
		src.x = (i % 16) * 16;
		src.y = (i / 16) * 16;

		SDL_Rect dst = {0};
		dst.w = top->w;
		dst.h = top->h;

		DrawBlit(font, &src, top, &dst);
		DrawBlit(font, &src, bottom, &dst);

		uint16_t color_fg = fgcolor.r << RED_SHIFT
		| fgcolor.g << GREEN_SHIFT
		| fgcolor.b << BLUE_SHIFT;
		uint16_t color_bg = bgcolor.r << RED_SHIFT
		| bgcolor.g << GREEN_SHIFT
		| bgcolor.b << BLUE_SHIFT;

		set_color(top, color_fg, blue);
		set_color(bottom, color_bg, blue);

		letters[i] = (SDL_Surface*)AllocNewSurface(0, top->w, top->h+offset);

		SetColorKey(letters[i], SDL_SRCCOLORKEY, blue);
		FillRectangle(letters[i], NULL, blue);

		dstrect.x = 0;
		dstrect.y = offset;
		DrawBlit(bottom, NULL, letters[i], &dstrect);

		dstrect.x = 0;
		dstrect.y = 0;
		DrawBlit(top, NULL, letters[i], &dstrect);
	}

	return 0;
}

bool NXFont::InitKrChars(SDL_Surface *font, uint32_t color)
{
	SDL_Color fgcolor;
	SDL_Surface *krletter;

	fgcolor.r = (uint8_t)(color >> 16);
	fgcolor.g = (uint8_t)(color >> 8);
	fgcolor.b = (uint8_t)(color);

	uint16_t blue = 0x1f;

	for(int i=0;i<NUM_KR_LETTERS_RENDERED;i++)
	{

		krletter = (SDL_Surface*)AllocNewSurface(0, 12, 12);	// han font size : 12 x 12

		SDL_Rect src = {0};

		src.w = 11;
		src.h = 11;
		src.x = (i % 47) * 12;
		src.y = (i / 47) * 12;

		SDL_Rect dst = {0};
		dst.w = krletter->w;
		dst.h = krletter->h;

		SetColorKey(krletter, SDL_SRCCOLORKEY, blue);
		FillRectangle(krletter, NULL, blue);

		DrawBlit(font, &src, krletter, &dst);

		uint16 color = fgcolor.r << RED_SHIFT
		| fgcolor.g << GREEN_SHIFT
		| fgcolor.b << BLUE_SHIFT;

		set_color(krletter, color, blue);

		krletters[i] = krletter;
	}

	return 0;
}

// create a font with a drop-shadow (used for "MNA" stage-name displays)
bool NXFont::InitKrCharsShadowed(SDL_Surface *font, uint32_t color, uint32_t shadowcolor)
{
	SDL_Color fgcolor, bgcolor;
	SDL_Surface *top, *bottom;
	SDL_Rect dstrect;
	const int offset = 2;

	fgcolor.r = (uint8_t)(color >> 16);
	fgcolor.g = (uint8_t)(color >> 8);
	fgcolor.b = (uint8_t)(color);

	bgcolor.r = (uint8_t)(shadowcolor >> 16);
	bgcolor.g = (uint8_t)(shadowcolor >> 8);
	bgcolor.b = (uint8_t)(shadowcolor);

	for(int i=0;i<NUM_KR_LETTERS_RENDERED;i++)
	{
		uint16_t blue = 0x1f;

		top = (SDL_Surface*)AllocNewSurface(0, 12, 12);
		bottom = (SDL_Surface*)AllocNewSurface(0, 12, 12);

		FillRectangle(top, NULL, blue);
		FillRectangle(bottom, NULL, blue);
		SetColorKey(top, SDL_SRCCOLORKEY, blue);
		SetColorKey(bottom, SDL_SRCCOLORKEY, blue);

		SDL_Rect src = {0};
		src.w = 11;
		src.h = 11;
		src.x = (i % 47) * 12;
		src.y = (i / 47) * 12;

		SDL_Rect dst = {0};
		dst.w = top->w;
		dst.h = top->h;

		DrawBlit(font, &src, top, &dst);
		DrawBlit(font, &src, bottom, &dst);

		uint16_t color_fg = fgcolor.r << RED_SHIFT
		| fgcolor.g << GREEN_SHIFT
		| fgcolor.b << BLUE_SHIFT;
		uint16_t color_bg = bgcolor.r << RED_SHIFT
		| bgcolor.g << GREEN_SHIFT
		| bgcolor.b << BLUE_SHIFT;

		set_color(top, color_fg, blue);
		set_color(bottom, color_bg, blue);

		krletters[i] = (SDL_Surface*)AllocNewSurface(0, top->w, top->h+offset);

		SetColorKey(krletters[i], SDL_SRCCOLORKEY, blue);
		FillRectangle(krletters[i], NULL, blue);

		dstrect.x = 0;
		dstrect.y = offset;
		DrawBlit(bottom, NULL, krletters[i], &dstrect);

		dstrect.x = 0;
		dstrect.y = 0;
		DrawBlit(top, NULL, krletters[i], &dstrect);
	}

	return 0;
}

/*
void c------------------------------() {}
*/

// draw a text string
static int text_draw(int x, int y, const char *text, int spacing, NXFont *font)
{
	int orgx = x;
	int i;
	SDL_Rect dstrect;

	for(i=0;text[i];i++)
	{
		char ch = text[i];
		char ch2 = text[i+1];
		if (ch >= 0xA1 && ch <= 0xA2 && ch2 >= 0xA1 && ch2 <= 0xFE) // Symbol(166)  : 0xA1A1 ~ 0xA2E8 (except xxA0, xxFF)
		{
			unsigned short krch = ((unsigned short)ch - 0xA1) * 94 + (ch2 - 0xA1) + 2350;
			SDL_Surface *krletter = font->krletters[krch];

			if (rendering && krletter)
			{
				// must set this every time, because SDL_BlitSurface overwrites
				// dstrect with final clipping rectangle.
				dstrect.x = x;
				dstrect.y = y;
				DrawBlit(krletter, NULL, sdl_screen, &dstrect);
			}

			if (krletter) 
			{
				if (krletter->w < spacing * 2)
				{	// fixed spacing
					x += spacing * 2;
				}
				else
				{
					x += krletter->w;
				}
				i++;
			} 
			else if (spacing != 0)
			{
				x += spacing;
			}
		}
		else if (ch >= 0xB0 && ch <= 0xC8 && ch2 >= 0xA1 && ch2 <= 0xFE) // Hangul(2350) : 0xB0A1 ~ 0xC8FE (except xxA0, xxFF)
		{
			unsigned short krch = ((unsigned short)ch - 0xB0) * 94 + (ch2 - 0xA1);
			SDL_Surface *krletter = font->krletters[krch];

			if (rendering && krletter)
			{
				// must set this every time, because SDL_BlitSurface overwrites
				// dstrect with final clipping rectangle.
				dstrect.x = x;
				dstrect.y = y;
				DrawBlit(krletter, NULL, sdl_screen, &dstrect);
			}

			if (krletter) 
			{
				if (krletter->w < spacing * 2)
				{	// fixed spacing
					x += spacing * 2;
				}
				else
				{
					x += krletter->w;
				}
				i++;
			} 
			else if (spacing != 0)
			{
				x += spacing;
			}
		}
		else 
		{
			SDL_Surface *letter = font->letters[ch];

			if (ch == '=' && game.mode != GM_CREDITS)
			{
				if (rendering)
					draw_sprite((x), (y)+2, SPR_TEXTBULLET);
			}
			else if (rendering && ch != ' ' && letter)
			{
				// must set this every time, because SDL_BlitSurface overwrites
				// dstrect with final clipping rectangle.
				dstrect.x = x;
				dstrect.y = y;
				DrawBlit(letter, NULL, sdl_screen, &dstrect);
			}

			if (spacing != 0)
			{	// fixed spacing
				x += spacing;
			}
			else
			{	// variable spacing
				if (ch == ' ' && shrink_spaces)
				{	// 10.5 px for spaces - make smaller than they really are - the default
					x += 6;
					if (i & 1) x++;
				}
				else
				{
					if (letter)
						x += letter->w;
				}
			}
		}
	}

	// return the final width of the text drawn
	return (x - orgx);
}


int GetFontWidth(const char *text, int spacing, bool is_shaded)
{
	int wd;

	if (spacing)
		return (strlen(text) * spacing);

	rendering = false;
	shrink_spaces = is_shaded;

	wd = text_draw(0, 0, text, spacing);

	rendering = true;
	shrink_spaces = true;

	return (wd);
}

int GetFontHeight()
{
	return fontheight;
}

/*
void c------------------------------() {}
*/

// create the shadesfc, used by font_draw_shaded. It's just a big long black surface
// with 50% per-surface alpha applied, that we can use to darken the background.
static bool create_shade_sfc(void)
{
	if (shadesfc)
		FreeSurface(shadesfc);
	
	int ht = whitefont.letters['M']->h;
	
	shadesfc = (SDL_Surface*)AllocNewSurface(SDL_SRCALPHA | SDL_SWSURFACE, SCREEN_WIDTH, ht);
	
	if (!shadesfc)
		return 1;
	
	FillRectangle(shadesfc, NULL, 0);
	LRSDL_SetAlpha(shadesfc, SDL_SRCALPHA, 128);
	
	return 0;
}


int font_draw(int x, int y, const char *text, int spacing, NXFont *font)
{
	return (text_draw(x, y, text, spacing, font));
}

// draw a text string with a 50% dark border around it
int font_draw_shaded(int x, int y, const char *text, int spacing, NXFont *font)
{
	SDL_Rect srcrect, dstrect;
	int wd;

	// get full-res width of final text
	rendering = false;
	shrink_spaces = false;

	srcrect.x = 0;
	srcrect.y = 0;
	srcrect.h = shadesfc->h;
	srcrect.w = text_draw(0, 0, text, spacing, font);

	rendering = true;

	// shade
	dstrect.x = x;
	dstrect.y = y;
	DrawBlit(shadesfc, &srcrect, sdl_screen, &dstrect);

	// draw the text on top as normal
	wd = text_draw(x, y, text, spacing, font);

	shrink_spaces = true;
	return (wd);
}





