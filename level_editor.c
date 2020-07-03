#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>

#define MFD_IMPLEMENTATION
#include "miscellus_file_dialog.h"

typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef u32 b32;
enum {false = 0, true = 1};

typedef u64 umm;
typedef s64 smm;

typedef int check_size8[sizeof(u8)==1&&sizeof(s8)==1 ? 1 : -1];
typedef int check_size16[sizeof(u16)==2&&sizeof(s16)==2 ? 1 : -1];
typedef int check_size32[sizeof(u32)==4&&sizeof(s32)==4 ? 1 : -1];
typedef int check_size64[sizeof(u64)==8&&sizeof(s64)==8 ? 1 : -1];
typedef int check_sizeumm[sizeof(umm)==sizeof((void *)0) ? 1 : -1];
typedef int check_sizesmm[sizeof(smm)==sizeof((void *)0) ? 1 : -1];


#define GAMEBOY_TILE_WIDTH 8
#define GAMEBOY_BYTES_PER_TILE (8*8*2/8)

#define PIXEL_SCALE_FACTOR 3


typedef struct Length_Buffer {
	umm length;
	u8 *data;
} Length_Buffer;


typedef struct Tile_Map {
	u32 tile_count;
	u32 pixels_per_row;
	u32 *pixels;
} Tile_Map;

typedef struct Rectangular_Selection {
	u32 min_tile_x;
	u32 min_tile_y;
	u32 max_tile_x;
	u32 max_tile_y;
} Rectangular_Selection;


typedef enum Application_Mode {
	APP_MODE_VIEW = 0,
	APP_MODE_EDIT_LEVEL = 1,
	APP_MODE_EDIT_TILE = 2,
	APP_MODE_PICK_TILE = 3,
	COUNT_APP_MODE = 4
} Application_Mode;

typedef struct View {
	float zoom;
	float offset_x;
	float offset_y;
} View;

typedef enum Interaction_Flags {
	INACT_SELECTING = 0x1,
	INACT_DRAGGING = 0x2,
	INACT_EYEDROPPER = 0x4,
	INACT_DRAW_SOLID = 0x8,
} Interaction_Flags;

typedef struct Application_State {
	Application_Mode mode;
	View view_edit;
	View view_pick;

	SDL_Rect selection;
	
	Interaction_Flags interaction_flags;

	s32 drag_start_x;
	s32 drag_start_y;

	u32 draw_tile_index;
	Tile_Map tile_map;
	SDL_Texture *tile_map_texture;
	#define level_width 32
	#define level_height 32
	u32 level_map[level_height][level_width];
	u8 collision_map[level_height][level_width];

	s32 window_width;
	s32 window_height;

	s32 mouse_x;
	s32 mouse_y;
	u32 mouse_flags;
} Application_State;


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
	--v;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v |= v >> 32;
	++v;

	return v;
}

#define ceil_to_multiplum(value, multiplum) ((((value) + (multiplum - 1)) / multiplum) * multiplum)

static Tile_Map prepare_tile_map(Length_Buffer raw_data_buffer) {

	Tile_Map result = {0};

	umm rounded_size = raw_data_buffer.length;
	rounded_size = rounded_size & ~(GAMEBOY_BYTES_PER_TILE - 1); // Round down to nearest multiple of GAMEBOY_BYTES_PER_TILE

	result.tile_count = rounded_size / GAMEBOY_BYTES_PER_TILE;

	u32 num_pixels = result.tile_count * GAMEBOY_TILE_WIDTH*GAMEBOY_TILE_WIDTH;

	result.pixels_per_row = ceil_to_multiplum((u32)ceil(sqrt(num_pixels)), GAMEBOY_TILE_WIDTH);
	assert((result.pixels_per_row % GAMEBOY_TILE_WIDTH) == 0);

	return result;
}

static void compute_pixels_from_gameboy_tile_format(
	Tile_Map tile_map,
	Length_Buffer raw_game_boy_tile_data)
{

	static const u32 game_boy_palette[4] = {
		0xc4cfa1ff,
		0x8b956dff,
		0x4d533cff,
		0x1f1f1fff,
	};

	s32 pixels_per_row = tile_map.pixels_per_row;

	u8 *source_at = raw_game_boy_tile_data.data;
	u8 *source_end = source_at + tile_map.tile_count * GAMEBOY_BYTES_PER_TILE;

	for (int y = 0; y < pixels_per_row; y += GAMEBOY_TILE_WIDTH) {
		for (int x = 0; x < pixels_per_row; x += GAMEBOY_TILE_WIDTH) {

			if (source_at >= source_end) return;

			u32 *line = &tile_map.pixels[y * pixels_per_row + x];

			for (int tile_y = 0; tile_y < GAMEBOY_TILE_WIDTH; ++tile_y) {
				u8  low_byte = *source_at++;
				u8 high_byte = *source_at++;

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


static inline View *get_current_view(Application_State *app_state) {
	if (app_state->mode == APP_MODE_EDIT_LEVEL) {
		return &app_state->view_edit;
	}
	else if (app_state->mode == APP_MODE_PICK_TILE) {
		return &app_state->view_pick;
	}

	return NULL;
}


static b32 load_tile_palette(Application_State *app_state, SDL_Renderer *renderer, char *palette_file_path) {
	Length_Buffer tile_file_buffer = read_entire_file(palette_file_path);

	if (tile_file_buffer.data == NULL) {
		return false;
	}

	// Cleanup after potential previous texture
	if (app_state->tile_map_texture) {
		SDL_DestroyTexture(app_state->tile_map_texture);
	}

	app_state->tile_map = prepare_tile_map(tile_file_buffer);


	app_state->tile_map_texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STREAMING,
		app_state->tile_map.pixels_per_row,
		app_state->tile_map.pixels_per_row);

	void *texture_pixels;
	s32 pitch;

	s32 error = SDL_LockTexture(app_state->tile_map_texture, NULL, &texture_pixels, &pitch);
	if(error) {
		panic("Could not lock tile map texture: %s\n", SDL_GetError());
	}

	app_state->tile_map.pixels = texture_pixels;
	compute_pixels_from_gameboy_tile_format(app_state->tile_map, tile_file_buffer);
	SDL_UnlockTexture(app_state->tile_map_texture);

	return true;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		panic("%s expects the path to a tile palette file as the first argument.\n", argv[0]);
	}

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

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		SDL_DestroyWindow(window);
		panic("SDL_CreateRenderer Error: %s\n", SDL_GetError());
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	Application_State app_state = {0};
	app_state.mode = APP_MODE_EDIT_LEVEL;
	app_state.draw_tile_index = 0;
	app_state.view_edit.zoom = 1;
	app_state.view_pick.zoom = 1;
	app_state.selection = (SDL_Rect){10, 10, 5, 10};

	for (s32 y = 0; y < level_height; ++y) {
		for (s32 x = 0; x < level_width; ++x) {
			app_state.level_map[y][x] = 0;
			app_state.collision_map[y][x] = 0;
		}
	}
	
	load_tile_palette(&app_state, renderer, argv[1]);

	b32 move_view_left;
	b32 move_view_right;
	b32 move_view_up;
	b32 move_view_down;

	SDL_Event e;
	b32 quit = false;
	while (!quit){

		enum {DRAG_NO_CHANGE, DRAG_START, DRAG_STOP} drag_update = DRAG_NO_CHANGE;

		SDL_GetWindowSize(window, &app_state.window_width, &app_state.window_height);
		app_state.mouse_flags = SDL_GetMouseState(&app_state.mouse_x, &app_state.mouse_y);

		View *view = get_current_view(&app_state);
		float world_mouse_x = (float)app_state.mouse_x / (float)view->zoom + view->offset_x;
		float world_mouse_y = (float)app_state.mouse_y / (float)view->zoom + view->offset_y;

		b32 do_fill = false;

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

					case SDLK_o: {
						if (e.key.keysym.mod & KMOD_CTRL) {
							char file_path[1024];

							if (miscellus_file_dialog(file_path, 1024)) {
								load_tile_palette(&app_state, renderer, file_path);
							}
						}
					}
					break;

					case SDLK_s: {
						if (e.key.keysym.mod & KMOD_CTRL) {
							// TODO(jakob): save
						}
						else if (app_state.mode == APP_MODE_EDIT_LEVEL) {
							// Toggle draw solid
							app_state.interaction_flags ^= INACT_DRAW_SOLID;
						}
					}
					break;

					case SDLK_f: {
						view->offset_x = 0;
						view->offset_y = 0;
						view->zoom = 1;
					}
					break;

					case SDLK_b: {
						do_fill = true;
					}
					break;

					case SDLK_TAB: {
						if (app_state.mode == APP_MODE_EDIT_LEVEL) app_state.mode = APP_MODE_PICK_TILE;
						else app_state.mode = APP_MODE_EDIT_LEVEL;
					}
					break;

					case SDLK_LEFT: move_view_left = true; break;
					case SDLK_RIGHT: move_view_right = true; break;
					case SDLK_UP: move_view_up = true; break;
					case SDLK_DOWN: move_view_down = true; break;

					case SDLK_SPACE: {
						if (!(app_state.interaction_flags & INACT_DRAGGING)) drag_update = DRAG_START;
					}
					break;
				}
			}
			else if (e.type == SDL_KEYUP){
				switch(e.key.keysym.sym) {
					case SDLK_LEFT: move_view_left = false; break;
					case SDLK_RIGHT: move_view_right = false; break;
					case SDLK_UP: move_view_up = false; break;
					case SDLK_DOWN: move_view_down = false; break;

					case SDLK_SPACE: {
						if (app_state.interaction_flags & INACT_DRAGGING) drag_update = DRAG_STOP;
					}
				}
			}
			else if(e.type == SDL_MOUSEWHEEL) {
				
				if(e.wheel.y > 0) {
					view->zoom *= 1.2;
					if (view->zoom > 10) {view->zoom = 10;}
				}
				else if(e.wheel.y < 0) {
					view->zoom *= 0.8;
					if (view->zoom < 0.1) view->zoom = 0.1;
				}

				// Adjust view position (Zoom like in Gimp)
				float world_view_width = (float)app_state.window_width / (float)view->zoom;
				float world_view_height = (float)app_state.window_height / (float)view->zoom;
				float x01 = (float)app_state.mouse_x / (float)app_state.window_width;
				float y01 = (float)app_state.mouse_y / (float)app_state.window_height;
				view->offset_x = world_mouse_x - x01*world_view_width; 
				view->offset_y = world_mouse_y - y01*world_view_height;
			}
			else if (e.type == SDL_DROPFILE) {
				char *dropped_file_path = e.drop.file;
				load_tile_palette(&app_state, renderer, dropped_file_path);
				SDL_free(dropped_file_path);
			}
			else if (e.type == SDL_MOUSEBUTTONDOWN) { 
				if (e.button.button == SDL_BUTTON_MIDDLE) {
					drag_update = DRAG_START;
				}
			}
			else if (e.type == SDL_MOUSEBUTTONUP) { 
				if (e.button.button == SDL_BUTTON_MIDDLE) {
					drag_update = DRAG_STOP;
				}
			}
		}


		if (drag_update == DRAG_START) {
			app_state.interaction_flags |= INACT_DRAGGING;
			app_state.drag_start_x = world_mouse_x;
			app_state.drag_start_y = world_mouse_y;

		}
		else if (drag_update == DRAG_STOP) {
			app_state.interaction_flags &= ~INACT_DRAGGING;
			view->offset_x -= world_mouse_x - app_state.drag_start_x;
			view->offset_y -= world_mouse_y - app_state.drag_start_y;
		}

		const float view_speed = 16.0f / view->zoom;

		if (move_view_left)   view->offset_x -= view_speed;
		if (move_view_right)  view->offset_x += view_speed;
		if (move_view_up)     view->offset_y -= view_speed;
		if (move_view_down)   view->offset_y += view_speed;

		SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
		SDL_RenderClear(renderer);

		SDL_RenderSetScale(renderer, view->zoom, view->zoom);

		b32 mouse_left_clicked = app_state.mouse_flags & SDL_BUTTON(SDL_BUTTON_LEFT);
		b32 mouse_right_clicked = app_state.mouse_flags & SDL_BUTTON(SDL_BUTTON_RIGHT);

		s32 pixel_scale_factor = app_state.window_height/256;
		if (pixel_scale_factor <= 0) pixel_scale_factor = 1;
		s32 scaled_tile_width = pixel_scale_factor * GAMEBOY_TILE_WIDTH;	

		Tile_Map tile_map = app_state.tile_map;

		const u32 tiles_per_row = tile_map.pixels_per_row / GAMEBOY_TILE_WIDTH; 

		const u32 level_width_pixels = level_width * scaled_tile_width;

		s32 x_offset = (app_state.window_width/2 - level_width_pixels/2) - view->offset_x;
		s32 y_offset = (app_state.window_height/2 - level_width_pixels/2) - view->offset_y;

		float zoomed_mouse_x = ((float)app_state.mouse_x / (float)view->zoom);
		float zoomed_mouse_y = ((float)app_state.mouse_y / (float)view->zoom);

		if (app_state.interaction_flags & INACT_DRAGGING) {
			x_offset += zoomed_mouse_x + view->offset_x - app_state.drag_start_x;
			y_offset += zoomed_mouse_y + view->offset_y - app_state.drag_start_y;
		}

		u32 hot_tile_x = (zoomed_mouse_x - x_offset) / scaled_tile_width;
		u32 hot_tile_y = (zoomed_mouse_y - y_offset) / scaled_tile_width;

		if (
			hot_tile_x < level_width &&
			hot_tile_y < level_height
		) {
			if (mouse_left_clicked) {
				app_state.level_map[hot_tile_y][hot_tile_x] = app_state.draw_tile_index;
				app_state.collision_map[hot_tile_y][hot_tile_x] = (app_state.interaction_flags & INACT_DRAW_SOLID) ? 1 : 0;
			}
			else if (mouse_right_clicked) {
				app_state.draw_tile_index = app_state.level_map[hot_tile_y][hot_tile_x];
				if (app_state.collision_map[hot_tile_y][hot_tile_x] == 1) {
					app_state.interaction_flags |= INACT_DRAW_SOLID;
				}
				else {
					app_state.interaction_flags &= ~INACT_DRAW_SOLID;
				}
			}

			if (do_fill) {

				u32 tile_index_to_fill_over = app_state.level_map[hot_tile_y][hot_tile_x];

				u32 stack_position = 0;
				u32 stack[2*level_width*level_height];

				app_state.level_map[hot_tile_y][hot_tile_x] = app_state.draw_tile_index;

				u32 selected_tile_x = hot_tile_x;
				u32 selected_tile_y = hot_tile_y;

				// consider neighbors, color and add them to the stack if applicable
				for (;;) {
					// Order of neighbors, 0 denotes the current tile
					//
					//      [1]
					//   [3][0][4]
					//      [2]

					u32 neighbor_ys[4] = {selected_tile_y - 1, selected_tile_y + 1, selected_tile_y, selected_tile_y};
					u32 neighbor_xs[4] = {selected_tile_x, selected_tile_x, selected_tile_x - 1, selected_tile_x + 1};

					for (u32 i = 0; i < 4; ++i) {

						u32 neighbor_x = neighbor_xs[i];
						u32 neighbor_y = neighbor_ys[i];

						if (
							neighbor_y < level_height &&
							neighbor_x < level_width
						) {

							u32 neighbor_tile_index = app_state.level_map[neighbor_y][neighbor_x];

							if (neighbor_tile_index == tile_index_to_fill_over) {
								app_state.level_map[neighbor_y][neighbor_x] = app_state.draw_tile_index;

								stack[stack_position++] = neighbor_x;
								stack[stack_position++] = neighbor_y;
							}
						}

					}

					if (stack_position >= 2) {
						selected_tile_y = stack[--stack_position];
						selected_tile_x = stack[--stack_position];
					}
					else {
						break;
					}
				}
			}
		}

		SDL_Rect dest_rect;
		SDL_Rect source_rect;


		{ // Drop shadow
			s32 border_radius = 6;
			dest_rect = (SDL_Rect){
				x_offset - border_radius,
				y_offset - border_radius,
				scaled_tile_width*level_width + (2*border_radius),
				scaled_tile_width*level_height + (2*border_radius)
			};

			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60);
			SDL_RenderFillRect(renderer, &dest_rect);
		}

		switch (app_state.mode) {
			case APP_MODE_VIEW:
			case APP_MODE_EDIT_LEVEL: {
				for (u32 y = 0; y < level_height; ++y) {
					for (u32 x = 0; x < level_width; ++x) {

						u32 tile_index = app_state.level_map[y][x];

						dest_rect = (SDL_Rect){
							x * scaled_tile_width + x_offset,
							y * scaled_tile_width + y_offset,
							scaled_tile_width,
							scaled_tile_width,
						};

						b32 draw_solid = false;
						b32 is_hot_tile = (app_state.mode == APP_MODE_EDIT_LEVEL && x == hot_tile_x && y == hot_tile_y);
						
						if (!is_hot_tile) {
							
							source_rect = (SDL_Rect){
								tile_index % tiles_per_row * GAMEBOY_TILE_WIDTH,
								tile_index / tiles_per_row * GAMEBOY_TILE_WIDTH,
								GAMEBOY_TILE_WIDTH,
								GAMEBOY_TILE_WIDTH,
							};
							
							draw_solid = app_state.collision_map[y][x] == 1;
						}
						else {

							SDL_SetRenderDrawColor(renderer, 240, 240, 0, 200);
							
							source_rect = (SDL_Rect){
								app_state.draw_tile_index % tiles_per_row * GAMEBOY_TILE_WIDTH,
								app_state.draw_tile_index / tiles_per_row * GAMEBOY_TILE_WIDTH,
								GAMEBOY_TILE_WIDTH,
								GAMEBOY_TILE_WIDTH,
							};
							//SDL_RenderCopy(renderer, app_state.tile_map_texture, &hot_rect, &dest_rect);

							draw_solid = app_state.interaction_flags & INACT_DRAW_SOLID;
						}

						if (draw_solid) {
							SDL_SetTextureColorMod( app_state.tile_map_texture, 255, 128, 0 );
						}
						else {
							SDL_SetTextureColorMod( app_state.tile_map_texture, 255, 255, 255 );
						}
						
						SDL_RenderCopy(renderer, app_state.tile_map_texture, &source_rect, &dest_rect);

						if (is_hot_tile) {
							SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
							SDL_RenderDrawRect(renderer, &dest_rect);
							SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
						}

					}
				}

				if (app_state.interaction_flags & INACT_SELECTING) {
					SDL_Rect selection = app_state.selection;

					selection.x *= scaled_tile_width;
					selection.y *= scaled_tile_width;
					selection.w *= scaled_tile_width;
					selection.h *= scaled_tile_width;

					selection.x += x_offset;
					selection.y += y_offset;

					SDL_SetRenderDrawColor(renderer, 0, 150, 200, 192);
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
					SDL_RenderFillRect(renderer, &selection);
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				}
			}
			break;

			case APP_MODE_PICK_TILE: {
				SDL_Rect dest_rect = {
					x_offset,
					y_offset,
					pixel_scale_factor * tile_map.pixels_per_row,
					pixel_scale_factor * tile_map.pixels_per_row,
				};
				SDL_RenderCopy(renderer, app_state.tile_map_texture, NULL, &dest_rect);

				SDL_SetRenderDrawColor(renderer, 240, 240, 0, 200);			
				SDL_Rect hot_rect = {
					hot_tile_x * scaled_tile_width + x_offset,
					hot_tile_y * scaled_tile_width + y_offset,
					scaled_tile_width,
					scaled_tile_width,
				};
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
				SDL_RenderDrawRect(renderer, &hot_rect);
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

				if (mouse_left_clicked) {
					if (hot_tile_y > tiles_per_row) hot_tile_y = tiles_per_row - 1;
					if (hot_tile_x > tiles_per_row) hot_tile_x = tiles_per_row - 1;
					app_state.draw_tile_index = (hot_tile_y * tiles_per_row) + hot_tile_x;
				}
			}
			break;

			default:
				assert(0);
		}

		SDL_RenderSetScale(renderer, 1, 1);












		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	return 0;
}
