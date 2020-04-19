#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <SDL2/SDL.h>


typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;


typedef char check_size8[sizeof(u8)==1&&sizeof(s8)==1 ? 1 : -1];
typedef char check_size16[sizeof(u16)==2&&sizeof(s16)==2 ? 1 : -1];
typedef char check_size32[sizeof(u32)==4&&sizeof(s32)==4 ? 1 : -1];
typedef char check_size64[sizeof(u64)==8&&sizeof(s64)==8 ? 1 : -1];

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct Loaded_Font {
	s32 atlas_dimension;
	SDL_Surface *atlas_surface;
	SDL_Texture *atlas_texture;
	u32 first_codepoint;
	u32 num_code_points;
	stbtt_bakedchar *baked_chars;
} Loaded_Font;

typedef struct Global_State {
	//SDL_Texture *tilemap;
	Loaded_Font font;
	SDL_Renderer *renderer;
	SDL_Window *window;
	s32 mouse_x;
	s32 mouse_y;
} Global_State;

static Global_State global;

static void __attribute__((noreturn)) panic(char *format, ...) {
	fprintf(stderr, "[ERROR] ");
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(-1);
}

#define STRUCTURE(name) typedef struct name name; struct name
#define FF(format, ...)

STRUCTURE(this_is_a_test) {
	int x; FF(%d)
};

static void read_entire_file(s8 *path, u64 *out_length, u8 **out_contents) {

	FILE *file = fopen(path, "rb");
	if (!file) return;

	u64 length;
	u8 *contents;

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	contents = malloc(length+1);

	u64 amount_read = fread(contents, 1, length, file);
	fclose(file);

	if (amount_read != length) {
		free(contents);
		return;
	}

	contents[length] = '\0';

	*out_length = length;
	*out_contents = contents;
	return;
}

void stbtt_print(float x, float y, char *text) {
	// assume orthographic projection with units = screen pixels, origin at top left
	s32 dim = global.font.atlas_dimension;

	while (*text) {
		if (*text >= ' ' && *text <= 128) {
			stbtt_aligned_quad q;
			stbtt_GetBakedQuad(global.font.baked_chars, dim, dim, *text - 32, &x, &y, &q, 1);

			SDL_Rect src_rect = {.x = dim * q.s0 - 1,
								 .y = dim * (q.t0) - 1,
								 .w = dim * (q.s1 - q.s0) + 1,
								 .h = dim * (q.t1 - q.t0) + 1};
			SDL_Rect dst_rect = {.x = q.x0, .y = q.y0, .w = q.x1 - q.x0, .h = q.y1 - q.y0};

			SDL_RenderCopy(global.renderer, global.font.atlas_texture, &src_rect, &dst_rect);
		}
		++text;
	}
}

static bool button(char *label, SDL_Rect *rect) {
	int mouse_x, mouse_y;
	int button_flags = SDL_GetMouseState(&mouse_x, &mouse_y);

	bool hover = (mouse_x >= rect->x && mouse_x < (rect->x + rect->w)) && (mouse_y >= rect->y && mouse_y < (rect->y + rect->w));
	
	if (hover) {
		SDL_SetTextureColorMod(global.font.atlas_texture, 50, 50, 50);
		SDL_SetRenderDrawColor(global.renderer, 255, 255, 0, 255);
	}
	else {
		SDL_SetTextureColorMod(global.font.atlas_texture, 255, 255, 255);
		SDL_SetRenderDrawColor(global.renderer, 57, 178, 190, 255);
	}
	SDL_RenderFillRect(global.renderer, rect);




	stbtt_print(rect->x+20, rect->y+64, label);

	SDL_SetTextureColorMod(global.font.atlas_texture, 255, 255, 255);

	if (hover && button_flags & SDL_BUTTON(SDL_BUTTON_LEFT)) {
		return true;
	}
	else {
		return false;
	}
}



bool load_font(char *path_to_ttf, float glyph_height, s32 atlas_dimension, Loaded_Font *out_font) {

	u64 ttf_contents_length;
	u8 *ttf_contents = NULL;
	read_entire_file(path_to_ttf, &ttf_contents_length, &ttf_contents);

	if (!ttf_contents) {
		return false;
	}

	SDL_Surface *atlas_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, atlas_dimension, atlas_dimension, 8, 0, 0, 0, 0);
	if (!atlas_surface) {
		panic("Could not create font surface\n");
	}

	/* 8-bit sdl surfaces are indexed (palletised), so setup a pallete with
	 * 256 shades of grey. this is needed so the sdl blitter has something to
	 * convert from when blitting to a direct colour surface */
	SDL_Color shades_of_gray[256];
	for(int i = 0; i < 256; i++){
		shades_of_gray[i].r = 255;
		shades_of_gray[i].g = 255;
		shades_of_gray[i].b = 255;
		shades_of_gray[i].a = i;
	}
	SDL_SetPaletteColors(atlas_surface->format->palette, shades_of_gray, 0, 256);

	out_font->first_codepoint = ' ';
	out_font->num_code_points = 96;
	out_font->baked_chars = malloc(out_font->num_code_points * sizeof(*out_font->baked_chars));
	assert(out_font->baked_chars);

	s32 font_bake_result = stbtt_BakeFontBitmap(
		ttf_contents,
		0,
		glyph_height,
		atlas_surface->pixels,
		atlas_dimension, atlas_dimension,
		out_font->first_codepoint, out_font->num_code_points,
		out_font->baked_chars); // no guarantee this fits!

	free(ttf_contents);

	if (!font_bake_result) {
		panic("Could not bake truetype font!\n");
	}

	SDL_Texture *atlas_texture = SDL_CreateTextureFromSurface(global.renderer, atlas_surface);
	if (!atlas_texture) {
		panic("Could not make texture out of font surface\n");
	}

	SDL_SetTextureBlendMode(atlas_texture, SDL_BLENDMODE_BLEND);


	out_font->atlas_dimension = atlas_dimension;
	out_font->atlas_surface = atlas_surface;
	out_font->atlas_texture = atlas_texture;
	return true;
}

void destroy_font(Loaded_Font *font) {
	SDL_FreeSurface(font->atlas_surface);
	SDL_DestroyTexture(font->atlas_texture);
	free(font->baked_chars);
}

int main() {

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		panic("SDL_Init Error: %s\n", SDL_GetError());
	}

	SDL_Window *window = SDL_CreateWindow("Miscellus GameBoy Level Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!window) {
		panic("SDL_CreateWindow Error: %s\n", SDL_GetError());
	}

	global.window = window;		

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		SDL_DestroyWindow(window);
		panic("SDL_CreateRenderer Error: %s\n", SDL_GetError());
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	global.renderer = renderer;

	SDL_Surface *screen_surface = SDL_GetWindowSurface(window);
	if (!screen_surface) {
		panic("Could not ge1024t screen surface\n");
	}


	load_font("Yrsa/Yrsa-Regular.ttf", 96, 512, &global.font);

	SDL_Rect rect;
	rect.x = 250;
	rect.y = 150;
	rect.w = 200;
	rect.h = 200;

	int window_width;
	int window_height; 

	SDL_GetWindowSize(window, &window_width, &window_height);

	//Our event structure
	SDL_Event e;
	bool quit = false;
	while (!quit){
		while (SDL_PollEvent(&e)){
			if (e.type == SDL_QUIT){
				quit = true;
			}
			else if (e.type == SDL_KEYDOWN){
				if (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_ESCAPE) {
					quit = true;
				}
			}
			else if (e.type == SDL_MOUSEMOTION) {
				global.mouse_x = e.motion.x;
				global.mouse_y = e.motion.y;
			}
			else if (e.type == SDL_MOUSEBUTTONDOWN){
			}

		}


		if (++rect.x > window_width + rect.w) {
			rect.x = -rect.w;
		}


		// SDL_SetRenderDrawColor(renderer, 187, 185, 173, 255);
		SDL_SetRenderDrawColor(renderer, 229, 226, 210, 255);
		SDL_RenderClear(renderer);

		if (button("Quit", &rect)) {
			quit = true;
		}




		SDL_Rect drect = {0,0,512,512};
		SDL_RenderCopy(renderer, global.font.atlas_texture, NULL, &drect);

		//SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

		//SDL_BlitSurface(yrsa_glyphs, NULL, screen_surface, NULL);
		//SDL_UpdateWindowSurface(window);
		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	return 0;
}
