#!/usr/bin/python3
# -*- coding: utf-8 -*-
#******************************************************************************
# ZYNTHIAN PROJECT: Zynthian GUI
#
# Zynthian GUI Main Menu Class
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

import logging

# Zynthian specific modules
from zyngui import zynthian_gui_config
from zyngui.zynthian_gui_selector import zynthian_gui_selector

#------------------------------------------------------------------------------
# Zynthian App Selection GUI Class
#------------------------------------------------------------------------------

class zynthian_gui_main_menu(zynthian_gui_selector):

	def __init__(self):
		super().__init__('Main', True)


	def fill_list(self):
		self.list_data=[]

		# Main Views
		self.list_data.append((self.new_synth_layer, 0, "New Synth Chain"))
		self.list_data.append((self.new_audiofx_layer, 0, "New Audio Chain"))
		self.list_data.append((self.new_midifx_layer, 0, "New MIDI Chain"))
		self.list_data.append((self.new_generator_layer, 0, "New Generator Chain"))
		self.list_data.append((self.new_special_layer, 0, "New Special Chain"))
		self.list_data.append((self.snapshots, 0, "Snapshots"))
		self.list_data.append((self.clean_all, 0, "Clean All"))

		# Add list of Apps
		self.list_data.append((None, 0, "> APPS"))
		self.list_data.append((self.step_sequencer, 0, "Sequencer"))
		self.list_data.append((self.midi_recorder, 0, "MIDI Recorder"))

		# Add list of System / configuration views
		self.list_data.append((None, 0, "> SYSTEM"))
		self.list_data.append((self.tempo_settings, 0, "Tempo Settings"))
		self.list_data.append((self.alsa_mixer, 0, "Audio Levels"))
		self.list_data.append((self.admin, 0, "Admin"))
		if zynthian_gui_config.control_test_enabled:
			self.list_data.append((self.control_test, 0, "Control Test"))
		self.list_data.append((self.all_notes_off, 0, "PANIC! All Notes Off"))

		super().fill_list()


	def select_action(self, i, t='S'):
		if self.list_data[i][0]:
			self.last_action=self.list_data[i][0]
			self.last_action(t)


	def new_synth_processor(self, t='S'):
		self.zyngui.modify_chain({"type": "MIDI Synth", "midi_thru": False, "audio_thru": False})


	def new_audiofx_processor(self, t='S'):
		try:
			chain_id = self.zyngui.chain_manager.add_chain(None, enable_audio_thru = True)
			self.zyngui.modify_chain({"type": "Audio Effect", "audio_thru": True, "chain_id": chain_id})
		except Exception as e:
			logging.error(e)


	def new_midifx_processor(self, t='S'):
		self.zyngui.modify_chain({"type": "MIDI Tool", "midi_thru": True, "audio_thru": False})


	def new_generator_processor(self, t='S'):
		#chain_id = self.zyngui.chain_manager.add_chain(None, enable_audio_thru = False)
		self.zyngui.modify_chain({"type": "Audio Generator", "midi_thru": False, "audio_thru": False})


	def new_special_processor(self, t='S'):
		self.zyngui.modify_chain({"type": "Special", "midi_thru": True, "audio_thru": True})


	def snapshots(self, t='S'):
		logging.info("Snapshots")
		self.zyngui.show_screen_reset("snapshot")


	def clean_all(self, t='S'):
		self.zyngui.show_confirm("Do you really want to remove all chains?", self.clean_all_confirmed)


	def clean_all_confirmed(self, params=None):
		self.index = 0
		self.zyngui.clean_all()


	def audio_mixer(self, t='S'):
		logging.info("Audio Mixer")
		self.zyngui.show_screen('audio_mixer')


	def step_sequencer(self, t='S'):
		logging.info("Step Sequencer")
		self.zyngui.show_screen('zynpad')


	def midi_recorder(self, t='S'):
		logging.info("MIDI Recorder")
		self.zyngui.show_screen("midi_recorder")


	def alsa_mixer(self, t='S'):
		logging.info("ALSA Mixer")
		self.zyngui.show_screen("alsa_mixer")


	def tempo_settings(self, t='S'):
		logging.info("Tempo Settings")
		self.zyngui.show_screen("tempo")

#
	def admin(self, t='S'):
		logging.info("Admin")
		self.zyngui.show_screen("admin")


	def control_test(self, t='S'):
		logging.info("Control Test")
		self.zyngui.show_screen_reset("control_test")


	def all_notes_off(self, t='S'):
		logging.info("All Notes Off")
		self.zyngui.callable_ui_action("ALL_NOTES_OFF")


	def set_select_path(self):
		self.select_path.set("Main")


#------------------------------------------------------------------------------