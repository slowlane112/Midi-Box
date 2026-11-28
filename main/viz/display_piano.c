#include <string.h> 
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "v1_midi.h"
#include "v1_compat.h"
#include "viz_common.h"
#include "gfx.h"

#include "esp_timer.h"


static uint8_t key_natural_height = 75; //60; //90;
static uint8_t key_sharp_height = 50; //40; //54;
static uint8_t side_padding = 0;
static uint8_t top_padding = 0;
static uint8_t notes_y_start = 0;
#define NOTE_FONT   font_large   /* note names: about the height of the original 8x16 text */
#define NOTE_BAR_Y  20           /* velocity bar, under the name */
#define NOTE_ROW_H  (NOTE_BAR_Y + 4)
#define background_color viz_bg(0)
//static uint16_t key_on_color = 0xbfb8; //0x9593; //0xf9bc; //0xdedd; //0xc54e;


static uint8_t update_notes_display_count = 0;

static uint8_t notes_start = 21;
static uint8_t notes_total = 88;



const size_t display_piano_key_on_colors_count = VIZ_COLOURS;

bool display_piano_show_notes = true;
uint8_t display_piano_key_on_color = 0;

static void draw_level_box(uint8_t type, int y_start, int x_start, uint8_t value) {
	
	// type 1 key, type 2 controller
	
	int lines = 0;
	
	if (type == 1) {
	
		if (value > 118) {
			value = 118;
		}
		
		if (value > 0) {
			lines = 1 + ((value - 1) * 15) / 118;
		}
		
	}
	else {
		if (value > 0) {
			lines = 1 + ((value - 1) * 15) / 127;
		}
		
	}
		
	uint16_t level_color = 0xBF04; // Cold: Deep Ice Blue (Levels 1-3)
	
	if (lines > 12) {
		level_color = 0x00F8; // Hot: Crimson Red (Levels 13-15)
	}
	else if (lines > 9) {
		level_color = 0x20FD; // Warm: Rich Amber / Orange (Levels 10-12)
	}
	else if (lines > 5) {
		level_color = 0xE007; // Mid: Vibrant Green (Levels 7-9)
	}
	
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 15; j++) {
			framebuffer_2[x_start + j + (y_start * DISPLAY_2_WIDTH)  + (i * DISPLAY_2_WIDTH)] = lines >= j+1 ? level_color : RGB(0x3A, 0x42, 0x50);   /* visible grey track: every bar the same size */
		}
	}
	
}

static void draw_controller_box(int y_start, int x_start, uint8_t controller, uint8_t value) {
	
	uint8_t height = (controller == CTRL_SUSTAIN_PEDAL) ? 8 : 14;
	uint16_t color = 0x00;
	
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < 15; j++) {
			
			if ((i == 0 && (j < 1 || j > 13)) || (i == height - 1 && (j < 1 || j > 13))) {
			
				color = viz_bg2(0);
			}
			else {
				color = value == 0 ? 0xffff : viz_col(display_piano_key_on_color);
			}
			
			framebuffer_2[x_start + j + (y_start * DISPLAY_2_WIDTH)  + (i * DISPLAY_2_WIDTH)] = color;
			
		}
	}
	
}

static void draw_drum_box(int y_start, int x_start, uint8_t drum, uint8_t value) {
	
	uint8_t height = 10;
	uint16_t color = value > 0 ? viz_col(display_piano_key_on_color) : 0xffff;
	
	for (int i = 0; i < height; i++) {
		
		for (int j = 0; j < 14; j++) {
			
			framebuffer_2[x_start + j + (y_start * DISPLAY_2_WIDTH)  + (i * DISPLAY_2_WIDTH)] = color;
			
		}
	}
	
}


static void draw_keyboard(const midi_values_t *midi_values, bool update_notes_display) {
	
	int note_pos = 0;
	
	// natural keys
	for (size_t i = notes_start; i < notes_start + notes_total; i++)
	{
		
		if (midi_notes[i].type == 1) {
			
			for (int j = 0; j < 7; j++) {
				
				for (int k = top_padding; k < key_natural_height + top_padding; k++) {
					
					framebuffer_2[(k * DISPLAY_2_WIDTH) + note_pos + j + side_padding] =  midi_values->note_values[i] > 0 ? viz_col(display_piano_key_on_color) : 0xffff;
				}
				
			}
		
			note_pos = note_pos + 8;
			
		}
		
	}
	
	// sharp
	
	note_pos = 0;
	static uint16_t key_sharp_color = 0x0000;
	
	for (size_t i = notes_start; i < notes_start + notes_total; i++)
	{
		
		if (midi_notes[i].type == 1) {
			
			note_pos = note_pos + 8;
			
		}
		else {
			
			for (int j = 0; j < 7; j++) {
				
				for (int k = top_padding; k < key_sharp_height + top_padding; k++) {
					
					if (j == 0 || j == 6 || k == key_sharp_height + top_padding - 1) {
						key_sharp_color = background_color;
					}
					else {
						key_sharp_color = midi_values->note_values[i] > 0 ? viz_col(display_piano_key_on_color) : 0x0000;
					}
						
					
					framebuffer_2[(k * DISPLAY_2_WIDTH) + note_pos + j + 4 - 8 + side_padding] = key_sharp_color;
				}
				
			}
		
		}
		
	}
	
	// controllers
	
	int pos_x = (DISPLAY_2_WIDTH / 2) - ((30 + 30 + 16) / 2);
	int pos_y = key_natural_height + top_padding + 8; 
	
	/*for (int i = 0; i < 3; i++)
	{
		
		draw_controller_box(pos_y, pos_x, i, controller_value[i]);
		
		if (i == 2) {
		
			draw_level_box(2, pos_y + 10, pos_x, controller_value[i]);
		
		}
		
		pos_x = pos_x + 30;
	}*/
	
	
	
	draw_controller_box(pos_y, pos_x, CTRL_SOFT_PEDAL, midi_values->controller_values[CTRL_SOFT_PEDAL]);
	
	pos_x = pos_x + 30;
	
	draw_controller_box(pos_y, pos_x, CTRL_SOSTENUTO_PEDAL, midi_values->controller_values[CTRL_SOSTENUTO_PEDAL]);
	
	pos_x = pos_x + 30;
	
	draw_controller_box(pos_y, pos_x, CTRL_SUSTAIN_PEDAL, midi_values->controller_values[CTRL_SUSTAIN_PEDAL]);
	
	draw_level_box(2, pos_y + 10, pos_x, midi_values->controller_values[CTRL_SUSTAIN_PEDAL]);
	

	
	
	
	// max is 53 chars
	
	if (display_piano_show_notes) {
	
	
		if (update_notes_display) {
			/* note names in the v2 font, each with its velocity bar centred underneath */
			gfx_t g = { framebuffer_2, VW, VH };
			gfx_fill(&g, 0, notes_y_start, VW, VH - notes_y_start, background_color);
			char label[24][8];
			int width[24], note[24], n = 0, total = 0;
			for (size_t i = notes_start; i < notes_start + notes_total && n < 24; i++) {
				if (midi_values->note_values[i] > 0) {
					snprintf(label[n], sizeof(label[n]), "%s%d", midi_notes[i].note, midi_notes[i].octave);
					width[n] = gfx_text_width(&NOTE_FONT, label[n]);
					if (width[n] < 15) {
						width[n] = 15;   /* at least as wide as its velocity bar */
					}
					note[n] = (int)i;
					total += width[n] + (n ? 8 : 0);
					n++;
				}
			}
			if (n > 0 && total <= VW - 8) {
				int x = (VW - total) / 2;
				for (int k = 0; k < n; k++) {
					gfx_text_box(&g, &NOTE_FONT, x, notes_y_start, width[k], ALIGN_CENTER, label[k], 0xFFFF);
					draw_level_box(1, notes_y_start + NOTE_BAR_Y, x + (width[k] - 15) / 2, midi_values->note_values[note[k]]);
					x += width[k] + 8;
				}
			} else if (n > 0) {
				char many[24];
				snprintf(many, sizeof(many), "%d notes", n);
				gfx_text_box(&g, &NOTE_FONT, 0, notes_y_start, VW, ALIGN_CENTER, many, viz_col(display_piano_key_on_color));
			}
		}
		
	}
	else {
		
		
		uint64_t current_time = esp_timer_get_time();
		
		int y_start = notes_y_start;
		int x_start = 0;
		// show drums
		for (size_t i = 35; i < 83; i++) {
			
			//uint8_t value = (current_time - midi_values->drum_timestamp_us[i] < 200000) ? midi_values->drum_values[i] : 0;
			uint8_t value = (current_time - midi_values->drum_timestamp_us[i] < 120000) ? midi_values->drum_values[i] : 0;
				
			draw_drum_box(y_start, x_start, i, value);
			
			
			if (i == 35 + 23) {
				x_start = 0;
				y_start = y_start + 14;
				
			}
			else {
			
				x_start = x_start + 18;
				
			}
		}
		
	}
	
}


void display_piano_draw(const midi_values_t *midi_values, bool *key_on_fired, char *message, bool display_active) {
	
	
	top_padding = (DISPLAY_2_HEIGHT - key_natural_height - (8 + 14) - NOTE_ROW_H) / 3;
	side_padding = (DISPLAY_2_WIDTH - (52 * 8)) / 2;
	notes_y_start = key_natural_height + (8 + 14) + top_padding + top_padding;
	
	bool update_notes_display = false;
 
	if (key_on_fired || update_notes_display_count == 8) {
		*key_on_fired = false;
		update_notes_display_count = 0;
		update_notes_display = true;
	}
	else {
		update_notes_display_count++;
	}
 
	int screen_end = update_notes_display ? DISPLAY_2_WIDTH * DISPLAY_2_HEIGHT : DISPLAY_2_WIDTH * notes_y_start;
 
	for (int i = 0; i < screen_end; i++) {
		framebuffer_2[i] = background_color;
	}

	draw_keyboard(midi_values, update_notes_display);
		
	if (message != NULL) {
		for (int y = 0; y < 24; y++) {
			for (int x = 0; x < DISPLAY_2_WIDTH; x++) {
				framebuffer_2[(y * DISPLAY_2_WIDTH) + x] = BG_COLOR;
			}
		}
		graphic_display_text(framebuffer_2, message, 4, 4, 0xffff, BG_COLOR);
	}

	display_2_draw();
	
}


