/*
 * SPDX-FileCopyrightText: 2026 slowlane112
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#ifndef DISPLAY_VISUALISER_THREE_H
#define DISPLAY_VISUALISER_THREE_H

extern bool display_visualiser_three_background_oval;
extern uint8_t display_visualiser_three_background_color;
extern const size_t display_visualiser_three_background_colors_count;
void display_visualiser_three_draw(const midi_values_t *midi_values, char *message, bool display_active);
void display_visualiser_three_deinit(void);

extern uint8_t display_visualiser_three_shape;
#endif
