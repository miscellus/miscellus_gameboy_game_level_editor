#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
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


typedef struct Tile_Map {
	SDL_Surface *surface;
	s32 pitch;
	s32 tile_count;
} Tile_Map;

typedef struct Global_State {
	//SDL_Texture *tilemap;
	Loaded_Font font;
	SDL_Renderer *renderer;
	SDL_Window *window;
	s32 mouse_x;
	s32 mouse_y;
	float offset_x;
	float offset_y;
	Tile_Map tile_map;
	bool left_down;
	bool right_down;
	bool up_down;
	bool down_down;
} Global_State;

static Global_State global;

// 8-bit sdl surfaces are indexed (palletised)	
static SDL_Color game_boy_palette[4] = {
	{0xc4, 0xcf, 0xa1, 0xff},
	{0x8b,0x95,0x6d,0xff},
	{0x4d,0x53,0x3c,0xff},
	{0x1f,0x1f,0x1f,0xff},
};

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

static void read_entire_file(s8 *path, s64 *out_length, u8 **out_contents) {

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
		if (*text - global.font.first_codepoint < global.font.num_code_points) {
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

	bool hover = (mouse_x >= rect->x && mouse_x < (rect->x + rect->w)) && (mouse_y >= rect->y && mouse_y < (rect->y + rect->h));
	
	if (hover) {
		SDL_SetTextureColorMod(global.font.atlas_texture, 50, 50, 50);
		SDL_SetRenderDrawColor(global.renderer, 255, 255, 0, 255);
	}
	else {
		SDL_SetTextureColorMod(global.font.atlas_texture, 255, 255, 255);
		SDL_SetRenderDrawColor(global.renderer, 57, 178, 190, 255);
	}
	SDL_RenderFillRect(global.renderer, rect);


	stbtt_print(rect->x, rect->y, label);

	SDL_SetTextureColorMod(global.font.atlas_texture, 255, 255, 255);

	if (hover && button_flags & SDL_BUTTON(SDL_BUTTON_LEFT)) {
		return true;
	}
	else {
		return false;
	}
}



bool load_font(char *path_to_ttf, float glyph_height, s32 atlas_dimension, Loaded_Font *out_font) {

	s64 ttf_contents_length;
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

enum {
	map_width = 32,
	map_height = 32,
};
static int level_map[map_height][map_width] = {124, 24, 5, 2};


static SDL_Surface *get_surface_for_tile_index(int index) {

	switch(index) {
		default: return NULL;
	}
}


static inline s64 next_higher_pow2(s64 v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v |= v >> 32;
	v++;

	return v;
}

Tile_Map load_tile_map(char *tile_map_filename) {

	s64 file_length = 0;
	u8 *file_contents = NULL;
	read_entire_file(tile_map_filename, &file_length, &file_contents);
	if (!file_contents) {
		panic("Could not read tile map file, %s\n", tile_map_filename);
	}

	const int tile_side_length = 8;
	const int bytes_per_tile = 8*8*2/8;

	file_length = file_length & -bytes_per_tile; // Round down to nearest multiple of bytes_per_tile

	s32 tile_count = file_length / bytes_per_tile;

	s32 num_pixels = tile_count * tile_side_length*tile_side_length;

	s32 surface_side_length = next_higher_pow2(ceil(sqrt(num_pixels))); 

	SDL_Surface *tile_map_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, surface_side_length, surface_side_length, 8, 0, 0, 0, 0);
	if (!tile_map_surface) {
		panic("Could not create tile_map_surface\n");
	}
	
	SDL_SetPaletteColors(tile_map_surface->format->palette, game_boy_palette, 0, 4);


	u8 *at = file_contents;
	u8 *end = at + tile_count * bytes_per_tile;
	u8 *pixels = tile_map_surface->pixels;

	for (int y = 0; y < surface_side_length; y += tile_side_length) {
		for (int x = 0; x < surface_side_length; x += tile_side_length) {
			if (at >= end) goto end;

			u8 *line = &pixels[y * surface_side_length + x];

			for (int tile_y = 0; tile_y < 8; ++tile_y) {
				u8  low_byte = *at++;
				u8 high_byte = *at++;

				for (int tile_x = 0; tile_x < 8; ++tile_x) {
					s8 color = 0;
					color |= (( low_byte >> (7 - tile_x)) & 1);
					color |= ((high_byte >> (7 - tile_x)) & 1) << 1;
					color &= 3;

					line[tile_x] = color;
				}

				line += surface_side_length;
			}	
		}
	}

	Tile_Map result = {0};
end:
	result.surface = tile_map_surface;
	result.pitch = surface_side_length / tile_side_length;
	result.tile_count = tile_count;
	return result;
}

int main() {


	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		panic("SDL_Init Error: %s\n", SDL_GetError());
	}

	SDL_Window *window = SDL_CreateWindow(
		"Miscellus GameBoy Level Editor",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1024, 768,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

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
		panic("Could not get screen surface\n");
	}

	global.tile_map = load_tile_map("/home/jakob/own/dev/GB/ROMs/Donkey Kong (World) (Rev A) (SGB Enhanced).gb");

	SDL_Texture *tile_map_texture = SDL_CreateTextureFromSurface(global.renderer, global.tile_map.surface);
	if (!tile_map_texture) {
		panic("Could not make texture out of tile map surface\n");
	}

	load_font("Fonts/Rubik/Rubik-Medium.ttf", 25, 1024, &global.font);



	SDL_Rect rect;
	rect.x = 250;
	rect.y = 150;
	rect.w = 180;
	rect.h = 48;

	int dx = 2;
	int dy = 2;

	float zoom_level = 1;

	int window_width;
	int window_height; 

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
				else if (e.key.keysym.sym == SDLK_LEFT) {
					global.left_down = true;
				}
				else if (e.key.keysym.sym == SDLK_RIGHT) {
					global.right_down = true;
				}
				else if (e.key.keysym.sym == SDLK_UP) {
					global.up_down = true;
				}
				else if (e.key.keysym.sym == SDLK_DOWN) {
					global.down_down = true;
				}
			}
			else if (e.type == SDL_KEYUP){
				if (e.key.keysym.sym == SDLK_LEFT) {
					global.left_down = false;
				}
				else if (e.key.keysym.sym == SDLK_RIGHT) {
					global.right_down = false;
				}
				else if (e.key.keysym.sym == SDLK_UP) {
					global.up_down = false;
				}
				else if (e.key.keysym.sym == SDLK_DOWN) {
					global.down_down = false;
				}
			}
			else if (e.type == SDL_MOUSEMOTION) {
				global.mouse_x = e.motion.x;
				global.mouse_y = e.motion.y;
			}
			else if(e.type == SDL_MOUSEWHEEL && e.wheel.y)
			{

				if(e.wheel.y > 0) // scroll up
				{
					zoom_level += 1;
					if (zoom_level > 20) zoom_level = 20;
				}
				else if(e.wheel.y < 0) // scroll down
				{
					zoom_level -= 1;
					if (zoom_level < 3) zoom_level = 2;					
				}

				SDL_RenderSetScale(global.renderer, zoom_level, zoom_level);
			}
			else if (e.type == SDL_MOUSEBUTTONDOWN){
			}

		}


		SDL_GetWindowSize(window, &window_width, &window_height);

		if (global.mouse_x > (window_width - 32) || global.right_down) {
			global.offset_x -= 8;
		}
		else if (global.mouse_x < 32 || global.left_down) {
			global.offset_x += 8;
		}

		if (global.mouse_y > (window_height - 32) || global.down_down) {
			global.offset_y -= 8;
		}
		else if (global.mouse_y < 32 || global.up_down) {
			global.offset_y += 8;
		}

		rect.x += dx;
		rect.y += dy;

		if (rect.x > (window_width + rect.w)) {
			dx = -2;
		}
		else if (rect.x <= 0) {
			dx = 2;
		}

		if (rect.y > (window_height + rect.h)) {
			dy = -2;
		}
		else if (rect.y <= 0) {
			dy = 2;
		}



		// SDL_SetRenderDrawColor(renderer, 187, 185, 173, 255);
		SDL_SetRenderDrawColor(renderer, 229, 226, 210, 255);
		SDL_RenderClear(renderer);

		if (button("Hej med dig!", &rect)) {
			quit = true;
		}

		for (int y = 0; y < map_height; ++y) {
			for (int x = 0; x < map_width; ++x) {
				int tile_index = 5000+level_map[y][x];

				int px = tile_index % global.tile_map.pitch;
				int py = tile_index / global.tile_map.pitch;

				SDL_Rect source_rect = {
					px,
					py,
					8,
					8
				};

				SDL_Rect dest_rect = {
					x * 8 + global.offset_x,
					y * 8 + global.offset_y,
					8,
					8,
				};
				
				SDL_RenderCopy(global.renderer, tile_map_texture, &source_rect, &dest_rect);
			}
		}

		{
			float mx = global.mouse_x / zoom_level;
			float my = global.mouse_y / zoom_level;

			SDL_Rect tile_rect = {
				(int)mx & -8,
				(int)my & -8,
				8,
				8,
			};
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
			SDL_SetRenderDrawColor(global.renderer, 255, 255, 0, 200);
			SDL_RenderDrawRect(global.renderer, &tile_rect);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		}
#if 0	
		SDL_Rect drect = {0,0,512,512};
		SDL_Rect srect = {global.mouse_x, global.mouse_y, 128, 128};
		SDL_RenderCopy(global.renderer, tile_map_texture, &srect, NULL);
#endif

		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	return 0;
}
