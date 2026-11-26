/*
 * SPDX-FileCopyrightText: 2026 slowlane112
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#ifndef DISPLAY_PIANO_H
#define DISPLAY_PIANO_H

void display_piano_draw(const midi_values_t *midi_values, bool *key_on_fired, char *message, bool display_active);
extern uint8_t display_piano_key_on_color;
extern const size_t display_piano_key_on_colors_count;
extern bool display_piano_show_notes;
#endif