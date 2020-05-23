#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>

typedef unsigned char u8;
typedef char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef long long s64;

typedef u32 b32;

typedef size_t uptr;
typedef ssize_t sptr;

typedef char check_size8[sizeof(u8)==1&&sizeof(s8)==1 ? 1 : -1];
typedef char check_size16[sizeof(u16)==2&&sizeof(s16)==2 ? 1 : -1];
typedef char check_size32[sizeof(u32)==4&&sizeof(s32)==4 ? 1 : -1];
typedef char check_size64[sizeof(u64)==8&&sizeof(s64)==8 ? 1 : -1];
typedef char check_sizeptr[sizeof(uptr)==sizeof((void *)0) ? 1 : -1];


#define GAMEBOY_TILE_WIDTH 8
#define GAMEBOY_BYTES_PER_TILE (8*8*2/8)

#define PIXEL_SCALE_FACTOR 3

typedef struct Length_Buffer {
	sptr length;
	u8 *data;
} Length_Buffer;

typedef struct Tile_Map {
	s32 tile_count;
	s32 pixels_per_row;
	u32 *pixels;
} Tile_Map;



static Length_Buffer read_entire_file(s8 *path) {

	Length_Buffer result = {0};

	FILE *file = fopen(path, "rb");

	if (file) {

		u64 length;
		u8 *contents;

		fseek(file, 0, SEEK_END);
		length = ftell(file);
		fseek(file, 0, SEEK_SET);
		
		contents = malloc(length);

		u64 amount_read = fread(contents, 1, length, file);
		fclose(file);

		if (amount_read == length) {
			result.length = length;
			result.data = contents;
		} else {		
			free(contents);
		}
	}

	return result;
}


__attribute__((noreturn)) static void panic(char *format, ...) {
	fprintf(stderr, "[ERROR] ");
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(-1);
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


static Tile_Map prepare_tile_map(Length_Buffer raw_data_buffer) {

	Tile_Map result = {0};

	sptr rounded_size = raw_data_buffer.length;
	rounded_size = rounded_size & ~(GAMEBOY_BYTES_PER_TILE - 1); // Round down to nearest multiple of GAMEBOY_BYTES_PER_TILE

	result.tile_count = rounded_size / GAMEBOY_BYTES_PER_TILE;

	s32 num_pixels = result.tile_count * GAMEBOY_TILE_WIDTH*GAMEBOY_TILE_WIDTH;

	result.pixels_per_row = next_higher_pow2(ceil(sqrt(num_pixels)));
	assert((result.pixels_per_row % GAMEBOY_TILE_WIDTH) == 0);

	return result;
}

static void compute_pixels_from_gameboy_tile_format(
	Tile_Map tile_map,
	Length_Buffer raw_game_boy_tile_data)
{
	// 8-bit sdl surfaces are indexed (palletised)	
	static const u32 game_boy_palette[4] = {
		0xc4cfa1ff,
		0x8b956dff,
		0x4d533cff,
		0x1f1f1fff,
	};

	s32 pixels_per_row = tile_map.pixels_per_row;

	u8 *at = raw_game_boy_tile_data.data;
	u8 *end = at + tile_map.tile_count * GAMEBOY_BYTES_PER_TILE;
	u32 *pixels = tile_map.pixels;

	for (int y = 0; y < pixels_per_row; y += GAMEBOY_TILE_WIDTH) {
		for (int x = 0; x < pixels_per_row; x += GAMEBOY_TILE_WIDTH) {

			if (at >= end) return;

			u32 *line = &pixels[y * pixels_per_row + x];

			for (int tile_y = 0; tile_y < GAMEBOY_TILE_WIDTH; ++tile_y) {
				u8  low_byte = *at++;
				u8 high_byte = *at++;

				for (int tile_x = 0; tile_x < GAMEBOY_TILE_WIDTH; ++tile_x) {
					u8 color = 0;
					color |= (( low_byte >> (7 - tile_x)) & 1);
					color |= ((high_byte >> (7 - tile_x)) & 1) << 1;
					assert(color <= 3);

					line[tile_x] = game_boy_palette[color];
				}

				line += pixels_per_row;
			}	
		}
	}
}


typedef enum Application_Mode {
	APP_MODE_VIEW = 0,
	APP_MODE_DRAW_TILES = 1,
} Application_Mode;

int main() {

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		panic("SDL_Init Error: %s\n", SDL_GetError());
	}

	SDL_Window *window = SDL_CreateWindow(
		"Miscellus GameBoy Level Editor",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		800, 600,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	if (!window) {
		panic("SDL_CreateWindow Error: %s\n", SDL_GetError());
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		SDL_DestroyWindow(window);
		panic("SDL_CreateRenderer Error: %s\n", SDL_GetError());
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	Tile_Map tile_map;
	SDL_Texture *tile_map_texture;
	{
		Length_Buffer tile_file_buffer = read_entire_file("/home/jakob/own/dev/GB/ROMs/Donkey Kong (World) (Rev A) (SGB Enhanced).gb");
		tile_map = prepare_tile_map(tile_file_buffer);

		tile_map_texture = SDL_CreateTexture(
			renderer,
			SDL_PIXELFORMAT_RGBA8888,
			SDL_TEXTUREACCESS_STREAMING,
			tile_map.pixels_per_row,
			tile_map.pixels_per_row);

		void *texture_pixels;
		s32 pitch;

		s32 error = SDL_LockTexture(tile_map_texture, NULL, &texture_pixels, &pitch);
		if(error) {
			panic("Could not lock tile map texture: %s\n", SDL_GetError());
		}

		tile_map.pixels = texture_pixels;
		compute_pixels_from_gameboy_tile_format(tile_map, tile_file_buffer);
		SDL_UnlockTexture(tile_map_texture);
	}


	#define level_width 32
	#define level_height 32
	s32 level_map[level_height][level_width];

	for (s32 y = 0; y < level_height; ++y)
		for (s32 x = 0; x < level_width; ++x)
			level_map[y][x] = 1000;

	s32 draw_tile_index = 5000;

	Application_Mode mode = APP_MODE_VIEW;

	SDL_Event e;
	bool quit = false;
	while (!quit){
		while (SDL_PollEvent(&e)) {

			if (e.type == SDL_QUIT){
				quit = true;
			}
			else if (e.type == SDL_KEYDOWN){
				
				switch(e.key.keysym.sym) {

					case SDLK_ESCAPE:
					case SDLK_q: {
						quit = true;
					}
					break;

					case SDLK_TAB: mode = APP_MODE_DRAW_TILES - mode; break;
				}
			}
			else if(e.type == SDL_MOUSEWHEEL) {
				if(e.wheel.y > 0) {
					draw_tile_index = (draw_tile_index + 1) % tile_map.tile_count;
				}
				else if(e.wheel.y < 0) {
					draw_tile_index = (draw_tile_index - 1) % tile_map.tile_count;
				}
			}
		}

		s32 window_width;
		s32 window_height;
		s32 mouse_x;
		s32 mouse_y;
		u32 mouse_flags;

		SDL_GetWindowSize(window, &window_width, &window_height);
		mouse_flags = SDL_GetMouseState(&mouse_x, &mouse_y);

		b32 mouse_left_clicked = mouse_flags & SDL_BUTTON(SDL_BUTTON_LEFT);

		s32 pixel_scale_factor = window_height/256;
		if (pixel_scale_factor <= 0) pixel_scale_factor = 1;
		s32 scaled_tile_width = pixel_scale_factor * GAMEBOY_TILE_WIDTH;


		SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
		SDL_RenderClear(renderer);

		const s32 tiles_per_row = tile_map.pixels_per_row / GAMEBOY_TILE_WIDTH; 

		const s32 level_width_pixels = level_width * scaled_tile_width;

		const s32 x_offset = window_width/2 - level_width_pixels/2;
		const s32 y_offset = window_height/2 - level_width_pixels/2;

		s32 hot_tile_x = (mouse_x - x_offset) / scaled_tile_width;
		s32 hot_tile_y = (mouse_y - y_offset) / scaled_tile_width;

		for (s32 y = 0; y < level_height; ++y) {
			for (s32 x = 0; x < level_width; ++x) {

				s32 tile_index = level_map[y][x];

				SDL_Rect source_rect = {
					tile_index % tiles_per_row * GAMEBOY_TILE_WIDTH,
					tile_index / tiles_per_row * GAMEBOY_TILE_WIDTH,
					GAMEBOY_TILE_WIDTH,
					GAMEBOY_TILE_WIDTH,
				};

				SDL_Rect dest_rect = {
					x * scaled_tile_width + x_offset,
					y * scaled_tile_width + y_offset,
					scaled_tile_width,
					scaled_tile_width,
				};
				
				SDL_RenderCopy(renderer, tile_map_texture, &source_rect, &dest_rect);

				if (mode == APP_MODE_DRAW_TILES && x == hot_tile_x && y == hot_tile_y) {
					if (mouse_left_clicked) {
						level_map[y][x] = draw_tile_index;
					}

					SDL_SetRenderDrawColor(renderer, 240, 240, 0, 200);
					
					SDL_Rect hot_rect = {
						draw_tile_index % tiles_per_row * GAMEBOY_TILE_WIDTH,
						draw_tile_index / tiles_per_row * GAMEBOY_TILE_WIDTH,
						GAMEBOY_TILE_WIDTH,
						GAMEBOY_TILE_WIDTH,
					};
					SDL_RenderCopy(renderer, tile_map_texture, &hot_rect, &dest_rect);

					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
					SDL_RenderDrawRect(renderer, &dest_rect);
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				}
			}
		}

		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	return 0;
}
