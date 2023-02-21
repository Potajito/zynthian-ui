#!/usr/bin/python3
# -*- coding: utf-8 -*-
#******************************************************************************
# ZYNTHIAN PROJECT: Zynthian GUI
#
# Zynthian WSLeds Class for Z2
#
# Copyright (C) 2015-2023 Fernando Moyano <jofemodo@zynthian.org>
#
#******************************************************************************
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# For a full copy of the GNU General Public License see the LICENSE.txt file.
#
#******************************************************************************

# Zynthian specific modules
from zyngui.zynthian_wsleds_base import zynthian_wsleds_base
from zyngui import zynthian_gui_config
from zynlibs.zynseq import zynseq

# ---------------------------------------------------------------------------
# Zynthian WSLeds class for Z2
# ---------------------------------------------------------------------------
class zynthian_wsleds_z2(zynthian_wsleds_base):
	
	def __init__(self, zyngui):
		super().__init__(zyngui)
		if zynthian_gui_config.wiring_layout == "Z2_V1":
			# LEDS with PWM1 (pin 13, channel 1)
			self.dma = 10
			self.pin = 13
			self.chan = 1
			self.num_leds = 25
		elif zynthian_gui_config.wiring_layout in ("Z2_V2", "Z2_V3"):
			# LEDS with SPI0 (pin 10, channel 0)
			self.dma = 10
			self.pin = 10
			self.chan = 0
			self.num_leds = 25


	def update_wsleds(self):
		curscreen = self.zyngui.current_screen
		# Menu
		if curscreen == "main":
			self.wsleds.setPixelColor(0, self.wscolor_active)
		elif curscreen == "stepseq" and self.zyngui.screens['stepseq'].is_shown_menu():
			self.wsleds.setPixelColor(0, self.wscolor_active)
		elif curscreen == "admin":
			self.wsleds.setPixelColor(0, self.wscolor_admin)
		else:
			self.wsleds.setPixelColor(0, self.wscolor_light)

		# Active Layer
		# => Light non-empty layers
		n = self.zyngui.screens['layer'].get_num_root_layers()
		main_fxchain = self.zyngui.screens['layer'].get_main_fxchain_root_layer()
		if main_fxchain:
			n -= 1
		for i in range(6):
			if i < n:
				self.wsleds.setPixelColor(1 + i, self.wscolor_light)
			else:
				self.wsleds.setPixelColor(1 + i, self.wscolor_off)
		# => Light FX layer if not empty
		if main_fxchain:
			self.wsleds.setPixelColor(7, self.wscolor_light)
		else:
			self.wsleds.setPixelColor(7, self.wscolor_off)
		# => Light active layer
		i = self.zyngui.screens['layer'].get_root_layer_index()
		if i is not None:
			if main_fxchain and i == n:
				if curscreen == "control":
					self.wsleds.setPixelColor(7, self.wscolor_active)
				else:
					self.blink(7, self.wscolor_active)
			elif i < 6:
				if curscreen == "control":
					self.wsleds.setPixelColor(1 + i, self.wscolor_active)
				else:
					self.blink(1 + i, self.wscolor_active)

		# Stepseq screen:
		if curscreen == "zynpad":
			self.wsleds.setPixelColor(8, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(8, self.wscolor_light)

		# Pattern Editor screen:
		if curscreen == "pattern_editor":
			self.wsleds.setPixelColor(9, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(9, self.wscolor_light)

		# MIDI Recorder screen:
		if curscreen == "midi_recorder":
			self.wsleds.setPixelColor(10, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(10, self.wscolor_light)

		# Snapshot screen:
		if curscreen == "snapshot":
			self.wsleds.setPixelColor(11, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(11, self.wscolor_light)

		# Presets screen:
		if curscreen in ("preset", "bank"):
			self.wsleds.setPixelColor(12, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(12, self.wscolor_light)

		# Light ALT button => MIDI LEARN!
		if self.zyngui.midi_learn_zctrl or curscreen == "zs3_learn":
			self.wsleds.setPixelColor(13, self.wscolor_yellow)
		elif self.zyngui.midi_learn_mode:
			self.wsleds.setPixelColor(13, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(13, self.wscolor_light)

		# REC/PLAY Audio buttons:
		if 'audio_recorder' in self.zyngui.status_info:
			self.wsleds.setPixelColor(14, self.wscolor_red)
		else:
			self.wsleds.setPixelColor(14, self.wscolor_light)

		if curscreen == "pattern_editor":
			pb_status = self.zyngui.screens['pattern_editor'].get_playback_status()
			if pb_status == zynseq.SEQ_PLAYING:
				self.wsleds.setPixelColor(15, self.wscolor_green)
			elif pb_status in (zynseq.SEQ_STARTING, zynseq.SEQ_RESTARTING):
				self.wsleds.setPixelColor(15, self.wscolor_yellow)
			elif pb_status in (zynseq.SEQ_STOPPING, zynseq.SEQ_STOPPINGSYNC):
				self.wsleds.setPixelColor(15, self.wscolor_red)
			elif pb_status == zynseq.SEQ_STOPPED:
				self.wsleds.setPixelColor(15, self.wscolor_light)
		elif 'audio_player' in self.zyngui.status_info:
			self.wsleds.setPixelColor(15, self.wscolor_active)
		else:
			self.wsleds.setPixelColor(15, self.wscolor_light)

		# REC/PLAY MIDI buttons:
		if self.zyngui.status_info['midi_recorder']:
			if "REC" in self.zyngui.status_info['midi_recorder']:
				self.wsleds.setPixelColor(16, self.wscolor_red)
			else:
				self.wsleds.setPixelColor(16, self.wscolor_light)

			if "PLAY" in self.zyngui.status_info['midi_recorder']:
				self.wsleds.setPixelColor(17, self.wscolor_active)
			else:
				self.wsleds.setPixelColor(17, self.wscolor_light)
		else:
			self.wsleds.setPixelColor(16, self.wscolor_light)
			self.wsleds.setPixelColor(17, self.wscolor_light)

		# Back/No button
		self.wsleds.setPixelColor(18, self.wscolor_red)

		# Up button
		self.wsleds.setPixelColor(19, self.wscolor_yellow)

		# Select/Yes button
		self.wsleds.setPixelColor(20, self.wscolor_green)

		# Left, Bottom, Right button
		for i in range(3):
			self.wsleds.setPixelColor(21 + i, self.wscolor_yellow)

		# Audio Mixer/Levels screen
		if curscreen == "audio_mixer":
			self.wsleds.setPixelColor(24, self.wscolor_active)
		elif curscreen == "alsa_mixer":
			self.wsleds.setPixelColor(24, self.wscolor_admin)
		else:
			self.wsleds.setPixelColor(24, self.wscolor_light)

		try:
			self.screens[curscreen].update_wsleds()
		except:
			pass

#------------------------------------------------------------------------------
