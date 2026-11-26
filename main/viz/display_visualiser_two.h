/*
 * SPDX-FileCopyrightText: 2026 slowlane112
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#ifndef DISPLAY_VISUALISER_TWO_H
#define DISPLAY_VISUALISER_TWO_H

extern bool display_visualiser_two_background_oval;
extern uint8_t display_visualiser_two_background_color;
extern const size_t display_visualiser_two_background_colors_count;
void display_visualiser_two_draw(const midi_values_t *midi_values, char *message, bool display_active);
void display_visualiser_two_deinit(void);
#endif
