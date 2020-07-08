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


typedef struct Length_Buffer {
	umm length;
	u8 *data;
} Length_Buffer;


typedef struct Tile_Map {
	u32 tile_count;
	u32 pixels_per_row;
	u32 *pixels;
} Tile_Map;

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

typedef enum Action_Flags {
	ACTION_SELECTING = 0x1,
	ACTION_DRAGGING = 0x2,
} Action_Flags;

#define TILE_MASK_SOLID (1 << 31)
#define TILE_MASK_INDEX (~TILE_MASK_SOLID)
typedef u32 Tile;

#define LEVEL_WIDTH 32
#define LEVEL_HEIGHT 32

typedef struct Application_State {
	Application_Mode mode;

	View view_edit;
	View view_pick;

	SDL_Rect selection;
	
	Action_Flags interaction_flags;

	s32 drag_start_x;
	s32 drag_start_y;

	Tile tile_to_draw;
	Tile_Map tile_map;
	SDL_Texture *tile_map_texture;
	Tile level_map[LEVEL_HEIGHT][LEVEL_WIDTH];

	s32 window_width;
	s32 window_height;


	s32 mouse_previous_x;
	s32 mouse_previous_y;
	u32 mouse_previous_flags;
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

void flood_fill(u32 x, u32 y, u32 tile, u32 grid[LEVEL_HEIGHT][LEVEL_WIDTH]) {

	u32 tile_to_fill_over = grid[y][x];

	if (tile_to_fill_over != tile) {

		u32 stack_position = 0;
		u32 stack[LEVEL_WIDTH*LEVEL_HEIGHT];

		for (;;) {

			// Spool to beginning of line segment:
			while (x != 0) {
				if (tile_to_fill_over != grid[y][x-1]) {
					break;
				}
				--x;
			}

			b32 search_above = true;
			b32 search_below = true;

			do {

				// Fill
				grid[y][x] = tile;

				if (y > 0) {

					b32 above_should_fill = (tile_to_fill_over == grid[y-1][x]);

					if (search_above && above_should_fill) {
						stack[stack_position++] = x;
						stack[stack_position++] = y-1;
						search_above = false;
					}
					else if (!search_above && !above_should_fill) {
						search_above = true;
					}
				}

				if (y < LEVEL_HEIGHT-1) {

					b32 below_should_fill = (tile_to_fill_over == grid[y+1][x]);
					
					if (search_below && below_should_fill) {
						stack[stack_position++] = x;
						stack[stack_position++] = y+1;
						search_below = false;
					}
					else if (!search_below && !below_should_fill) {
						search_below = true;
					}
				}

				++x;

				if (tile_to_fill_over != grid[y][x]) {
					break;
				}
			} while (x < LEVEL_WIDTH);
			
			if (stack_position >= 2) {
				do {
					y = stack[--stack_position];
					x = stack[--stack_position];
				} while (stack_position >= 2 && tile_to_fill_over != grid[y][x]);
			}
			else {
				break;
			}
		}
	}
}

static void screen_to_world_space(View *view, float screen_x, float screen_y, float *world_x, float *world_y) {
	*world_x = screen_x / view->zoom + view->offset_x;
	*world_y = screen_y / view->zoom + view->offset_y;
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
	app_state.tile_to_draw = 0;
	app_state.view_edit.zoom = 1;
	app_state.view_pick.zoom = 1;
	app_state.selection = (SDL_Rect){10, 10, 5, 10};

	for (s32 y = 0; y < LEVEL_HEIGHT; ++y) {
		for (s32 x = 0; x < LEVEL_WIDTH; ++x) {
			app_state.level_map[y][x] = 0; //!(x&y&1);
			// app_state.collision_map[y][x] = 0;
		}
	}
	
	load_tile_palette(&app_state, renderer, argv[1]);

	b32 move_view_left = false;
	b32 move_view_right = false;
	b32 move_view_up = false;
	b32 move_view_down = false;

	u32 hot_tile_x = 0;
	u32 hot_tile_y = 0;
	u32 hot_tile_previous_x;
	u32 hot_tile_previous_y;

	SDL_Event e;
	b32 quit = false;
	while (!quit){

		enum {DRAG_NO_CHANGE, DRAG_START, DRAG_STOP} drag_update = DRAG_NO_CHANGE;

		app_state.mouse_previous_flags = app_state.mouse_flags;
		app_state.mouse_previous_x = app_state.mouse_x;
		app_state.mouse_previous_y = app_state.mouse_y;

		SDL_GetWindowSize(window, &app_state.window_width, &app_state.window_height);
		app_state.mouse_flags = SDL_GetMouseState(&app_state.mouse_x, &app_state.mouse_y);

		View *view = get_current_view(&app_state);

		float world_mouse_x;
		float world_mouse_y;
		screen_to_world_space(view, app_state.mouse_x, app_state.mouse_y, &world_mouse_x, &world_mouse_y);

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
							app_state.tile_to_draw ^= TILE_MASK_SOLID;
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
						if (!(app_state.interaction_flags & ACTION_DRAGGING)) drag_update = DRAG_START;
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
						if (app_state.interaction_flags & ACTION_DRAGGING) drag_update = DRAG_STOP;
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
				float world_view_width = (float)app_state.window_width / view->zoom;
				float world_view_height = (float)app_state.window_height / view->zoom;
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
			app_state.interaction_flags |= ACTION_DRAGGING;
			app_state.drag_start_x = world_mouse_x;
			app_state.drag_start_y = world_mouse_y;

		}
		else if (drag_update == DRAG_STOP) {
			app_state.interaction_flags &= ~ACTION_DRAGGING;
			view->offset_x += app_state.drag_start_x - world_mouse_x;
			view->offset_y += app_state.drag_start_y - world_mouse_y;
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

		const u32 tiles_per_row = app_state.tile_map.pixels_per_row / GAMEBOY_TILE_WIDTH; 

		const u32 level_width_pixels = LEVEL_WIDTH * scaled_tile_width;

		float effective_view_offset_x = view->offset_x;
		float effective_view_offset_y = view->offset_y;

		if (app_state.interaction_flags & ACTION_DRAGGING) {
			effective_view_offset_x += app_state.drag_start_x - world_mouse_x;
			effective_view_offset_y += app_state.drag_start_y - world_mouse_y;
		}

		s32 canvas_offset_x = (app_state.window_width/2 - level_width_pixels/2) - effective_view_offset_x;
		s32 canvas_offset_y = (app_state.window_height/2 - level_width_pixels/2) - effective_view_offset_y;

		hot_tile_previous_x = hot_tile_x;
		hot_tile_previous_y = hot_tile_y;
		hot_tile_x = (((float)app_state.mouse_x / view->zoom) - canvas_offset_x) / scaled_tile_width;
		hot_tile_y = (((float)app_state.mouse_y / view->zoom) - canvas_offset_y) / scaled_tile_width;

		SDL_Rect dest_rect;
		SDL_Rect source_rect;

		switch (app_state.mode) {
			case APP_MODE_VIEW:
			case APP_MODE_EDIT_LEVEL: {

				if (
					hot_tile_x < LEVEL_WIDTH &&
					hot_tile_y < LEVEL_HEIGHT
				) {
					if (mouse_left_clicked) {
						b32 mouse_previous_left_clicked = app_state.mouse_previous_flags & SDL_BUTTON(SDL_BUTTON_LEFT);

						if (mouse_previous_left_clicked) {

							s32 dx = hot_tile_x - hot_tile_previous_x;
							s32 dy = hot_tile_y - hot_tile_previous_y;

							s32 sx = dx ? (dx > 0 ? 1 : -1) : 0;

							float error = 0;

							if (abs(dx) > abs(dy)) {
								float derror = fabs((float)dy / (float)dx); // dx can not be 0 since condition is: '>'

								u32 xmin, xmax;

								if (dx > 0) {
									xmin = hot_tile_previous_x;
									xmax = hot_tile_x;
								}
								else {
									xmin = hot_tile_x;
									xmax = hot_tile_previous_x;
								}

								u32 y = hot_tile_previous_y;
								s32 sy = dy ? (dy > 0 ? 1 : -1) : 0;
	
								for (u32 x = xmin; x != xmax+1; ++x) {
									app_state.level_map[x][y] = app_state.tile_to_draw;
									error += derror;

									if (error > 0.5) {
										y += sy;
										error -= 1.0;
									}
								}
							}
							else {
								float derror = dy ? fabs((float)dx / (float)dy) : 1; // dx can not be 0 since condition is: '>'

								u32 ymin, ymax;

								if (dy > 0) {
									ymin = hot_tile_previous_y;
									ymax = hot_tile_y;
								}
								else {
									ymin = hot_tile_y;
									ymax = hot_tile_previous_y;
								}

								u32 x = hot_tile_previous_x;
								s32 sx = dx ? (dx > 0 ? 1 : -1) : 0;
	
								for (u32 y = ymin; y != ymax+1; ++y) {
									app_state.level_map[x][y] = app_state.tile_to_draw;
									error += derror;

									if (error > 0.5) {
										x += sx;
										error -= 1.0;
									}
								}
							}

						}
						else {
							app_state.level_map[hot_tile_y][hot_tile_x] = app_state.tile_to_draw;
						}
					}
					else if (mouse_right_clicked) {
						app_state.tile_to_draw = app_state.level_map[hot_tile_y][hot_tile_x];
					}

					if (do_fill) {
						flood_fill(hot_tile_x, hot_tile_y, app_state.tile_to_draw, app_state.level_map);
					}
				}



				{ // Drop shadow
					s32 border_radius = 6;
					dest_rect = (SDL_Rect){
						canvas_offset_x - border_radius,
						canvas_offset_y - border_radius,
						scaled_tile_width*LEVEL_WIDTH + (2*border_radius),
						scaled_tile_width*LEVEL_HEIGHT + (2*border_radius)
					};

					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60);
					SDL_RenderFillRect(renderer, &dest_rect);
				}

				for (u32 y = 0; y < LEVEL_HEIGHT; ++y) {
					for (u32 x = 0; x < LEVEL_WIDTH; ++x) {

						b32 is_hot_tile = (app_state.mode == APP_MODE_EDIT_LEVEL && x == hot_tile_x && y == hot_tile_y);

						u32 tile_index;
						
						if (!is_hot_tile) {
							tile_index = app_state.level_map[y][x];
						}
						else {
							tile_index = app_state.tile_to_draw;
						}
						
						u32 solid_flag = tile_index & TILE_MASK_SOLID;
						tile_index &= TILE_MASK_INDEX;

						dest_rect = (SDL_Rect){
							x * scaled_tile_width + canvas_offset_x,
							y * scaled_tile_width + canvas_offset_y,
							scaled_tile_width,
							scaled_tile_width,
						};
						source_rect = (SDL_Rect){
							tile_index % tiles_per_row * GAMEBOY_TILE_WIDTH,
							tile_index / tiles_per_row * GAMEBOY_TILE_WIDTH,
							GAMEBOY_TILE_WIDTH,
							GAMEBOY_TILE_WIDTH,
						};
						
						SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
						SDL_RenderCopy(renderer, app_state.tile_map_texture, &source_rect, &dest_rect);

						if (solid_flag) {
							SDL_SetRenderDrawColor(renderer, 0, 64, 128, 255);
							SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
							SDL_RenderFillRect(renderer, &dest_rect);
						}

						if (is_hot_tile) {
							SDL_SetRenderDrawColor(renderer, 240, 240, 0, 200);
							SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
							SDL_RenderDrawRect(renderer, &dest_rect);
						}

					}
				}

				if (app_state.interaction_flags & ACTION_SELECTING) {
					SDL_Rect selection = app_state.selection;

					selection.x *= scaled_tile_width;
					selection.y *= scaled_tile_width;
					selection.w *= scaled_tile_width;
					selection.h *= scaled_tile_width;

					selection.x += canvas_offset_x;
					selection.y += canvas_offset_y;

					SDL_SetRenderDrawColor(renderer, 0, 150, 200, 192);
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
					SDL_RenderFillRect(renderer, &selection);
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				}
			}
			break;

			case APP_MODE_PICK_TILE: {
				SDL_Rect dest_rect = {
					canvas_offset_x,
					canvas_offset_y,
					pixel_scale_factor * app_state.tile_map.pixels_per_row,
					pixel_scale_factor * app_state.tile_map.pixels_per_row,
				};

				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				SDL_RenderCopy(renderer, app_state.tile_map_texture, NULL, &dest_rect);


				if (mouse_left_clicked && (hot_tile_y < tiles_per_row) && (hot_tile_x < tiles_per_row)) {
					u32 solid_flag = app_state.tile_to_draw & TILE_MASK_SOLID;
					app_state.tile_to_draw = (hot_tile_y * tiles_per_row) + hot_tile_x;
					app_state.tile_to_draw |= solid_flag;
				}
			}
			break;

			default:
				assert(0);
		}


		SDL_SetRenderDrawColor(renderer, 255, 255, 0, 200);
		SDL_Rect hot_rect = {
			hot_tile_x * scaled_tile_width + canvas_offset_x,
			hot_tile_y * scaled_tile_width + canvas_offset_y,
			scaled_tile_width,
			scaled_tile_width,
		};
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_RenderDrawRect(renderer, &hot_rect);

		SDL_RenderSetScale(renderer, 1, 1);

		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	return 0;
}
