#!/usr/bin/python3
# -*- coding: utf-8 -*-
# ******************************************************************************
# ZYNTHIAN PROJECT: Zynthian GUI
#
# Zynthian GUI Audio Mixer
#
# Copyright (C) 2015-2024 Fernando Moyano <jofemodo@zynthian.org>
#                         Brian Walton <brian@riban.co.uk>
#
# ******************************************************************************
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
# ******************************************************************************

import os
import tkinter
import logging
from time import monotonic
from math import log10

# Zynthian specific modules
from zyncoder.zyncore import lib_zyncore
from zynlibs.zynaudioplayer import *
from zyngine.zynthian_signal_manager import zynsigman

from . import zynthian_gui_base
from . import zynthian_gui_config
from zyngui.zynthian_gui_dpm import zynthian_gui_dpm
from zyngine.zynthian_audio_recorder import zynthian_audio_recorder
from zyngine.zynthian_engine_audioplayer import zynthian_engine_audioplayer
from zyngine import zynthian_controller

# ------------------------------------------------------------------------------
# Zynthian Mixer Strip Class
# This provides a UI element that represents a mixer strip, one used per chain
# ------------------------------------------------------------------------------


class zynthian_gui_mixer_strip():

	# Initialise mixer strip object
	#	parent: Parent object
	#	x: Horizontal coordinate of left of fader
	#	y: Vertical coordinate of top of fader
	#	width: Width of fader
	#	height: Height of fader
	def __init__(self, parent, x, y, width, height):
		self.parent = parent
		self.zynmixer = parent.zynmixer
		self.zctrls = None
		self.x = x
		self.y = y
		self.width = width
		self.height = height
		self.hidden = False
		self.chain_id = None
		self.chain = None

		self.hidden = True

		self.button_height = int(self.height * 0.07)
		self.legend_height = int(self.height * 0.08)
		self.balance_height = int(self.height * 0.03)
		self.fader_height = self.height - self.balance_height - self.legend_height - 2 * self.button_height
		self.fader_bottom = self.height - self.legend_height - self.balance_height
		self.fader_top = self.fader_bottom - self.fader_height
		self.fader_centre_x = int(x + width * 0.5)
		self.fader_centre_y = int(y + height * 0.5)
		self.balance_top = self.fader_bottom
		self.balance_control_centre = int(self.width / 2)
		self.balance_control_width = int(self.width / 4) # Width of each half of balance control

		#Digital Peak Meter (DPM) parameters
		self.dpm_width = int(self.width / 10) # Width of each DPM
		self.dpm_length = self.fader_height
		self.dpm_y0 = self.fader_top
		self.dpm_a_x0 = x + self.width - self.dpm_width * 2 - 2
		self.dpm_b_x0 = x + self.width - self.dpm_width - 1

		self.fader_width = self.width - self.dpm_width * 2 - 2

		self.fader_drag_start = None
		self.strip_drag_start = None
		self.dragging = False

		# Default style
		#self.fader_bg_color = zynthian_gui_config.color_bg
		self.fader_bg_color = zynthian_gui_config.color_panel_bg
		self.fader_bg_color_hl = "#6a727d" #"#207024"
		#self.fader_color = zynthian_gui_config.color_panel_hl
		#self.fader_color_hl = zynthian_gui_config.color_low_on
		self.fader_color = zynthian_gui_config.color_off
		self.fader_color_hl = zynthian_gui_config.color_on
		self.legend_txt_color = zynthian_gui_config.color_tx
		self.legend_bg_color = zynthian_gui_config.color_panel_bg
		self.legend_bg_color_hl = zynthian_gui_config.color_on
		self.button_bgcol = zynthian_gui_config.color_panel_bg
		self.button_txcol = zynthian_gui_config.color_tx
		self.left_color = "#00AA00"
		self.right_color = "#00EE00"
		self.learn_color_hl = "#999999"
		self.learn_color = "#777777"
		self.high_color = "#CCCC00" # yellow
		self.rec_color = "#CC0000" # red

		self.mute_color = zynthian_gui_config.color_on #"#3090F0"
		self.solo_color = "#D0D000"
		self.mono_color = "#B0B0B0"

		#font_size = int(0.5 * self.legend_height)
		font_size = int(0.25 * self.width)
		self.font = (zynthian_gui_config.font_family, font_size)
		self.font_fader = (zynthian_gui_config.font_family, int(0.9 * font_size))
		self.font_icons = ("forkawesome", int(0.3 * self.width))
		self.font_learn = (zynthian_gui_config.font_family, int(0.7 * font_size))

		self.fader_text_limit = self.fader_top + int(0.1 * self.fader_height)

		'''
		Create GUI elements
		Tags:
			strip:X All elements within the fader strip used to show/hide strip
			fader:X Elements used for fader drag
			X is the id of this fader's background
		'''

		# Fader
		self.fader_bg = self.parent.main_canvas.create_rectangle(x, self.fader_top, x + self.width, self.fader_bottom, fill=self.fader_bg_color, width=0)
		self.parent.main_canvas.itemconfig(self.fader_bg, tags=(f"fader:{self.fader_bg}", f"strip:{self.fader_bg}"))
		self.fader = self.parent.main_canvas.create_rectangle(x, self.fader_top, x + self.width, self.fader_bottom, fill=self.fader_color, width=0, tags=(f"fader:{self.fader_bg}", f"strip:{self.fader_bg}", f"audio_strip:{self.fader_bg}"))
		self.fader_text = self.parent.main_canvas.create_text(x, self.fader_bottom - 2, fill=self.legend_txt_color, text="", tags=(f"fader:{self.fader_bg}", f"strip:{self.fader_bg}"), angle=90, anchor="nw", font=self.font_fader)

		# DPM
		self.dpm_a = zynthian_gui_dpm(self.zynmixer, None, 0, self.parent.main_canvas, self.dpm_a_x0, self.dpm_y0, self.dpm_width, self.fader_height, True, (f"strip:{self.fader_bg}",f"audio_strip:{self.fader_bg}"))
		self.dpm_b = zynthian_gui_dpm(self.zynmixer, None, 1, self.parent.main_canvas, self.dpm_b_x0, self.dpm_y0, self.dpm_width, self.fader_height, True, (f"strip:{self.fader_bg}",f"audio_strip:{self.fader_bg}"))

		self.mono_text = self.parent.main_canvas.create_text(int(self.dpm_b_x0 + self.dpm_width / 2), int(self.fader_top + self.fader_height / 2), text="??", state=tkinter.HIDDEN)
		
		# Solo button
		self.solo = self.parent.main_canvas.create_rectangle(x, 0, x + self.width, self.button_height, fill=self.button_bgcol, width=0, tags=(f"solo_button:{self.fader_bg}", f"strip:{self.fader_bg}", f"audio_strip:{self.fader_bg}"))
		self.solo_text = self.parent.main_canvas.create_text(x + self.width / 2, self.button_height * 0.5, text="S", fill=self.button_txcol, tags=(f"solo_button:{self.fader_bg}", f"strip:{self.fader_bg}", f"audio_strip:{self.fader_bg}"), font=self.font)

		# Mute button
		self.mute = self.parent.main_canvas.create_rectangle(x, self.button_height, x + self.width, self.button_height * 2, fill=self.button_bgcol, width=0, tags=(f"mute:{self.fader_bg}", f"strip:{self.fader_bg}", f"audio_strip:{self.fader_bg}"))
		self.mute_text = self.parent.main_canvas.create_text(x + self.width / 2, self.button_height * 1.5, text="M", fill=self.button_txcol, tags=(f"mute:{self.fader_bg}", f"strip:{self.fader_bg}", f"audio_strip:{self.fader_bg}"), font=self.font)

		# Legend strip at bottom of screen
		self.legend_strip_bg = self.parent.main_canvas.create_rectangle(x, self.height - self.legend_height, x + self.width, self.height, width=0, tags=(f"strip:{self.fader_bg}",f"legend_strip:{self.fader_bg}"), fill=self.legend_bg_color)
		self.legend_strip_txt = self.parent.main_canvas.create_text(self.fader_centre_x, self.height - self.legend_height / 2, fill=self.legend_txt_color, text="-", tags=(f"strip:{self.fader_bg}",f"legend_strip:{self.fader_bg}"), font=self.font)

		# Balance indicator
		self.balance_left = self.parent.main_canvas.create_rectangle(x, self.balance_top, self.fader_centre_x, self.balance_top + self.balance_height, fill=self.left_color, width=0, tags=(f"strip:{self.fader_bg}", f"balance:{self.fader_bg}", f"audio_strip:{self.fader_bg}"))
		self.balance_right = self.parent.main_canvas.create_rectangle(self.fader_centre_x + 1, self.balance_top, self.width, self.balance_top + self.balance_height , fill=self.right_color, width=0, tags=(f"strip:{self.fader_bg}", f"balance:{self.fader_bg}", f"audio_strip:{self.fader_bg}"))
		self.balance_text = self.parent.main_canvas.create_text(self.fader_centre_x, int(self.balance_top + self.balance_height / 2) - 1, text="??", font=self.font_learn, state=tkinter.HIDDEN)
		self.parent.main_canvas.tag_bind(f"balance:{self.fader_bg}", "<ButtonPress-1>", self.on_balance_press)

		# Fader indicators
		self.record_indicator = self.parent.main_canvas.create_text(x + 2, self.height - 16, text="⚫", fill="#009000", anchor="sw", tags=(f"strip:{self.fader_bg}"), state=tkinter.HIDDEN)
		self.play_indicator = self.parent.main_canvas.create_text(x + 2, self.height - 2, text="⏹", fill="#009000", anchor="sw", tags=(f"strip:{self.fader_bg}"), state=tkinter.HIDDEN)

		self.parent.zyngui.multitouch.tag_bind(self.parent.main_canvas, "fader:%s"%(self.fader_bg), "press", self.on_fader_press)
		self.parent.zyngui.multitouch.tag_bind(self.parent.main_canvas, "fader:%s"%(self.fader_bg), "motion", self.on_fader_motion)
		self.parent.main_canvas.tag_bind(f"fader:{self.fader_bg}", "<ButtonPress-1>", self.on_fader_press)
		self.parent.main_canvas.tag_bind(f"fader:{self.fader_bg}", "<ButtonRelease-1>", self.on_fader_release)
		self.parent.main_canvas.tag_bind(f"fader:{self.fader_bg}", "<B1-Motion>", self.on_fader_motion)
		if zynthian_gui_config.force_enable_cursor:
			self.parent.main_canvas.tag_bind(f"fader:{self.fader_bg}", "<Button-4>", self.on_fader_wheel_up)
			self.parent.main_canvas.tag_bind(f"fader:{self.fader_bg}", "<Button-5>", self.on_fader_wheel_down)
			self.parent.main_canvas.tag_bind(f"balance:{self.fader_bg}", "<Button-4>", self.on_balance_wheel_up)
			self.parent.main_canvas.tag_bind(f"balance:{self.fader_bg}", "<Button-5>", self.on_balance_wheel_down)
			self.parent.main_canvas.tag_bind(f"legend_strip:{self.fader_bg}", "<Button-4>", self.parent.on_wheel)
			self.parent.main_canvas.tag_bind(f"legend_strip:{self.fader_bg}", "<Button-5>", self.parent.on_wheel)
		self.parent.main_canvas.tag_bind(f"mute:{self.fader_bg}", "<ButtonRelease-1>", self.on_mute_release)
		self.parent.main_canvas.tag_bind(f"solo_button:{self.fader_bg}", "<ButtonRelease-1>", self.on_solo_release)
		self.parent.main_canvas.tag_bind(f"legend_strip:{self.fader_bg}", "<ButtonPress-1>", self.on_strip_press)
		self.parent.main_canvas.tag_bind(f"legend_strip:{self.fader_bg}", "<ButtonRelease-1>", self.on_strip_release)
		self.parent.main_canvas.tag_bind(f"legend_strip:{self.fader_bg}", "<Motion>", self.on_strip_motion)

		self.draw_control()

	# Function to hide mixer strip
	def hide(self):
		self.parent.main_canvas.itemconfig(f"strip:{self.fader_bg}", state=tkinter.HIDDEN)
		self.hidden = True

	# Function to show mixer strip
	def show(self):
		self.dpm_a.set_strip(self.chain.mixer_chan)
		self.dpm_b.set_strip(self.chain.mixer_chan)
		self.parent.main_canvas.itemconfig(f"strip:{self.fader_bg}", state=tkinter.NORMAL)
		try:
			if not self.chain.is_audio():
				self.parent.main_canvas.itemconfig(f"audio_strip:{self.fader_bg}", state=tkinter.HIDDEN)
		except:
			pass
		self.hidden = False
		self.draw_control()


	def get_ctrl_learn_text(self, ctrl):
		if not self.chain.is_audio():
			return ""
		try:
			param = self.zynmixer.get_learned_cc(self.zctrls[ctrl])
			return f"{param[0] + 1}#{param[1]}"
		except:
			return "??"

	# Function to draw the DPM level meter for a mixer strip
	# state = [dpm_a, dpm_b, hold_a, hold_b, mono]
	def draw_dpm(self, state):
		if self.hidden or self.chain.mixer_chan is None:
			return

		self.dpm_a.refresh(state[0], state[2], state[4])
		self.dpm_b.refresh(state[1], state[3], state[4])

	def draw_balance(self):
		balance = self.zynmixer.get_balance(self.chain.mixer_chan)
		if balance is None:
			return
		if balance > 0:
			self.parent.main_canvas.coords(self.balance_left,
				self.x + balance * self.width / 2, self.balance_top,
				self.x + self.width / 2, self.balance_top + self.balance_height)
			self.parent.main_canvas.coords(self.balance_right,
				self.x + self.width / 2, self.balance_top,
				self.x + self.width, self.balance_top + self.balance_height)
		else:
			self.parent.main_canvas.coords(self.balance_left,
				self.x, self.balance_top,
				self.x + self.width / 2, self.balance_top + self. balance_height)
			self.parent.main_canvas.coords(self.balance_right,
				self.x + self.width / 2, self.balance_top,
				self.x + self.width * balance / 2 + self.width, self.balance_top + self.balance_height)

		if self.parent.zynmixer.midi_learn_zctrl == self.zctrls["balance"]:
			lcolor = self.learn_color_hl
			rcolor = self.learn_color
			txcolor = zynthian_gui_config.color_ml
			txstate = tkinter.NORMAL
			text = "??"
		elif self.parent.zynmixer.midi_learn_zctrl:
			lcolor = self.learn_color_hl
			rcolor = self.learn_color
			txcolor = zynthian_gui_config.color_hl
			txstate = tkinter.NORMAL
			text = f"{self.get_ctrl_learn_text('balance')}"
		else:
			lcolor = self.left_color
			rcolor = self.right_color
			txcolor = self.button_txcol
			txstate = tkinter.HIDDEN
			text = ""

		self.parent.main_canvas.itemconfig(self.balance_left, fill=lcolor)
		self.parent.main_canvas.itemconfig(self.balance_right, fill=rcolor)
		self.parent.main_canvas.itemconfig(self.balance_text, state=txstate, text=text, fill=txcolor)

	def draw_level(self):
		level = self.zynmixer.get_level(self.chain.mixer_chan)
		if level is not None:
			self.parent.main_canvas.coords(self.fader, self.x, self.fader_top + self.fader_height * (1 - level), self.x + self.fader_width, self.fader_bottom)

	def draw_fader(self):
		if self.zctrls and self.parent.zynmixer.midi_learn_zctrl == self.zctrls["level"]:
			self.parent.main_canvas.coords(self.fader_text, self.fader_centre_x, self.fader_centre_y - 2)
			self.parent.main_canvas.itemconfig(self.fader_text, text="??", font=self.font_learn, angle=0, fill=zynthian_gui_config.color_ml, justify=tkinter.CENTER, anchor=tkinter.CENTER)
		elif self.parent.zynmixer.midi_learn_zctrl:
			text = self.get_ctrl_learn_text('level')
			self.parent.main_canvas.coords(self.fader_text, self.fader_centre_x, self.fader_centre_y - 2)
			self.parent.main_canvas.itemconfig(self.fader_text, text=text, font=self.font_learn, angle=0, fill=zynthian_gui_config.color_hl, justify=tkinter.CENTER, anchor=tkinter.CENTER)
		else:
			if self.chain is not None:
				label_parts = self.chain.get_description(2).split("\n") + [""] #TODO
			else:
				label_parts = ["No info"]

			for i, label in enumerate(label_parts):
				#self.parent.main_canvas.itemconfig(self.fader_text, text=label, state=tkinter.NORMAL)
				bounds = self.parent.main_canvas.bbox(self.fader_text)
				if bounds[1] < self.fader_text_limit:
					while bounds and bounds[1] < self.fader_text_limit:
						label = label[:-1]
						self.parent.main_canvas.itemconfig(self.fader_text, text=label)
						bounds = self.parent.main_canvas.bbox(self.fader_text)
					label_parts[i] = label + "..."
			self.parent.main_canvas.itemconfig(self.fader_text, text="\n".join(label_parts), font=self.font_fader, angle=90, fill=self.legend_txt_color, justify=tkinter.LEFT, anchor=tkinter.NW)
			self.parent.main_canvas.coords(self.fader_text, self.x, self.fader_bottom - 2)

	def draw_solo(self):
		txcolor = self.button_txcol
		font = self.font
		text = "S"
		if self.zynmixer.get_solo(self.chain.mixer_chan):
			if self.parent.zynmixer.midi_learn_zctrl:
				bgcolor = self.learn_color_hl
			else:
				bgcolor = self.solo_color
		else:
			if self.parent.zynmixer.midi_learn_zctrl:
				bgcolor = self.learn_color
			else:
				bgcolor = self.button_bgcol

		if self.parent.zynmixer.midi_learn_zctrl == self.zctrls["solo"]:
			txcolor = zynthian_gui_config.color_ml
		elif self.parent.zynmixer.midi_learn_zctrl:
			txcolor = zynthian_gui_config.color_hl
			font = self.font_learn
			text = f"S {self.get_ctrl_learn_text('solo')}"

		self.parent.main_canvas.itemconfig(self.solo, fill=bgcolor)
		self.parent.main_canvas.itemconfig(self.solo_text, text=text, font=font, fill=txcolor)

	def draw_mute(self):
		txcolor = self.button_txcol
		font = self.font_icons
		if self.zynmixer.get_mute(self.chain.mixer_chan):
			if self.parent.zynmixer.midi_learn_zctrl:
				bgcolor = self.learn_color_hl
			else:
				bgcolor = self.mute_color
			text = "\uf32f"
		else:
			if self.parent.zynmixer.midi_learn_zctrl:
				bgcolor = self.learn_color
			else:
				bgcolor = self.button_bgcol
			text = "\uf028"

		if self.parent.zynmixer.midi_learn_zctrl == self.zctrls["mute"]:
			txcolor = zynthian_gui_config.color_ml
		elif self.parent.zynmixer.midi_learn_zctrl:
			txcolor = zynthian_gui_config.color_hl
			font = self.font_learn
			text = f"\uf32f {self.get_ctrl_learn_text('mute')}"

		self.parent.main_canvas.itemconfig(self.mute, fill=bgcolor)
		self.parent.main_canvas.itemconfig(self.mute_text, text=text, font=font, fill=txcolor)

	def draw_mono(self):
		"""
		if self.zynmixer.get_mono(self.chain.mixer_chan):
			self.parent.main_canvas.itemconfig(self.dpm_l_a, fill=self.mono_color)
			self.parent.main_canvas.itemconfig(self.dpm_l_b, fill=self.mono_color)
			self.dpm_hold_color = "#FFFFFF"
		else:
			self.parent.main_canvas.itemconfig(self.dpm_l_a, fill=self.low_color)
			self.parent.main_canvas.itemconfig(self.dpm_l_b, fill=self.low_color)
			self.dpm_hold_color = "#00FF00"
		"""

	# Function to draw a mixer strip UI control
	# control: Name of control or None to redraw all controls in the strip
	def draw_control(self, control=None):
		if self.hidden or self.chain is None: # or self.zctrls is None:
			return

		if control == None:
			if self.chain_id == 0:
				self.parent.main_canvas.itemconfig(self.legend_strip_txt, text="Main", font=self.font)
			else:
				font = self.font
				if self.parent.moving_chain and self.chain_id == self.parent.zyngui.chain_manager.active_chain_id:
					strip_txt = f"⇦⇨"
				elif isinstance(self.chain.midi_chan, int):
					if 0 <= self.chain.midi_chan < 16:
						strip_txt = f"♫ {self.chain.midi_chan + 1}"
					elif self.chain.midi_chan == 0xffff:
						strip_txt = f"♫ All"
					else:
						strip_txt = f"♫ Err"
				elif self.chain.is_audio:
					strip_txt = "\uf130"
					font = self.font_icons
				else:
					strip_txt = "\uf0ae"
					font = self.font_icons
					#procs = self.chain.get_processor_count() - 1
				self.parent.main_canvas.itemconfig(self.legend_strip_txt, text=strip_txt, font=font)
			self.draw_fader()
		try:
			if not self.chain.is_audio():
				self.parent.main_canvas.itemconfig(self.record_indicator, state=tkinter.HIDDEN)
				self.parent.main_canvas.itemconfig(self.play_indicator, state=tkinter.HIDDEN)
				return
		except Exception as e:
			logging.error(e)

		if self.zctrls:
			if control in [None, 'level']:
				self.draw_level()

			if control in [None, 'solo']:
				self.draw_solo()

			if control in [None, 'mute']:
				self.draw_mute()

			if control in [None, 'balance']:
				self.draw_balance()

			if control in [None, 'mono']:
				self.draw_mono()

		if control in [None, 'rec']:
			if self.chain.is_audio() and self.parent.zyngui.state_manager.audio_recorder.is_armed(self.chain.mixer_chan):
				if self.parent.zyngui.state_manager.audio_recorder.status:
					self.parent.main_canvas.itemconfig(self.record_indicator, fill=self.rec_color, state=tkinter.NORMAL)
				else:
					self.parent.main_canvas.itemconfig(self.record_indicator, fill=self.high_color, state=tkinter.NORMAL)
			else:
				self.parent.main_canvas.itemconfig(self.record_indicator, state=tkinter.HIDDEN)

		if control in [None, 'play']:
			try:
				processor = self.chain.synth_slots[0][0]
				if processor.eng_code == "AP":
					if zynaudioplayer.get_playback_state(processor.handle):
						self.parent.main_canvas.itemconfig(self.play_indicator, text="▶", fill="#009000", state=tkinter.NORMAL)
					else:
						self.parent.main_canvas.itemconfig(self.play_indicator, text="⏹", fill="#909090", state=tkinter.NORMAL)
				else:
					self.parent.main_canvas.itemconfig(self.play_indicator, state=tkinter.HIDDEN)
			except:
				self.parent.main_canvas.itemconfig(self.play_indicator, state=tkinter.HIDDEN)


	# --------------------------------------------------------------------------
	# Mixer Strip functionality
	# --------------------------------------------------------------------------

	# Function to highlight/downlight the strip
	# hl: Boolean => True=highlight, False=downlight
	def set_highlight(self, hl=True):
		if hl:
			self.set_fader_color(self.fader_bg_color_hl)
			self.parent.main_canvas.itemconfig(self.legend_strip_bg, fill=self.legend_bg_color_hl)
		else:
			self.set_fader_color(self.fader_color)
			self.parent.main_canvas.itemconfig(self.legend_strip_bg, fill=self.fader_bg_color)

	# Function to set fader colors
	# fg: Fader foreground color
	# bg: Fader background color (optional - Default: Do not change background color)
	def set_fader_color(self, fg, bg=None):
		self.parent.main_canvas.itemconfig(self.fader, fill=fg)
		if bg:
			self.parent.main_canvas.itemconfig(self.fader_bg_color, fill=bg)

	# Function to set chain associated with mixer strip
	#	chain: Chain object
	def set_chain(self, chain_id):
		self.chain_id = chain_id
		self.chain = self.parent.zyngui.chain_manager.get_chain(chain_id)
		if self.chain is None:
			self.hide()
			self.dpm_a.set_strip(None)
			self.dpm_b.set_strip(None)
		else:
			if self.chain.mixer_chan is not None and self.chain.mixer_chan < len(self.parent.zynmixer.zctrls):
				self.zctrls = self.parent.zynmixer.zctrls[self.chain.mixer_chan]
			self.show()

	# Function to set volume value
	#	value: Volume value (0..1)
	def set_volume(self, value):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["level"])
		elif self.zctrls:
			self.zctrls['level'].set_value(value)
			value = self.zctrls["level"].value
			if value > 0:
				level_db = 20 * log10(value)
				self.parent.set_title(f"Volume: {level_db:.2f}dB", None, None, 1)
			else:
				self.parent.set_title("Volume: -∞dB", None, None, 1)

	# Function to get volume value
	def get_volume(self):
		if self.zctrls:
			return self.zctrls['level'].value

	# Function to nudge volume
	def nudge_volume(self, dval):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["level"])
		elif self.zctrls:
			self.zctrls["level"].nudge(dval)
			value = self.zctrls["level"].value
			if value > 0:
				level_db = 20 * log10(value)
				self.parent.set_title(f"Volume: {level_db:.2f}dB", None, None, 1)
			else:
				self.parent.set_title("Volume: -∞dB", None, None, 1)

	# Function to set balance value
	#	value: Balance value (-1..1)
	def set_balance(self, value):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["balance"])
		elif self.zctrls:
			self.zctrls["balance"].set_value(value)
			self.parent.set_title(f"Balance: {int(value * 100)}%", None, None, 1)

	# Function to get balance value
	def get_balance(self):
		if self.zctrls:
			return self.zctrls['balance'].value

	# Function to nudge balance
	def nudge_balance(self, dval):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["balance"])
			self.parent.refresh_visible_strips()
		elif self.zctrls:
			self.zctrls['balance'].nudge(dval)
			self.parent.set_title(f"Balance: {int(self.zctrls['balance'].value * 100)}%", None, None, 1)

	# Function to reset volume
	def reset_volume(self):
		self.set_volume(0.8)

	# Function to reset balance
	def reset_balance(self):
		self.set_balance(0.0)

	# Function to set mute
	#	value: Mute value (True/False)
	def set_mute(self, value):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["mute"])
		elif self.zctrls:
			self.zctrls['mute'].set_value(value)
		#self.parent.refresh_visible_strips()

	# Function to set solo
	#	value: Solo value (True/False)
	def set_solo(self, value):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["solo"])
		elif self.zctrls:
			self.zctrls['solo'].set_value(value)
		if self.chain_id == 0:
			self.parent.refresh_visible_strips()

	# Function to toggle mono
	#	value: Mono value (True/False)
	def set_mono(self, value):
		if self.parent.zynmixer.midi_learn_zctrl:
			self.parent.enter_midi_learn(self.zctrls["mono"])
		elif self.zctrls:
			self.zctrls['mono'].set_value(value)
		self.parent.refresh_visible_strips()

	# Function to toggle mute
	def toggle_mute(self):
		if self.zctrls:
			self.set_mute(int(not self.zctrls['mute'].value))

	# Function to toggle solo
	def toggle_solo(self):
		if self.zctrls:
			self.set_solo(int(not self.zctrls['solo'].value))

	# Function to toggle mono
	def toggle_mono(self):
		if self.zctrls:
			self.set_mono(int(not self.zctrls['mono'].value))

	# --------------------------------------------------------------------------
	# UI event management
	# --------------------------------------------------------------------------

	# Function to handle fader press
	# event: Mouse event
	def on_fader_press(self, event):
		self.touch_y = event.y
		self.touch_x = event.x
		self.drag_axis = None  # +1=dragging in y-axis, -1=dragging in x-axis
		self.touch_ts = monotonic()
		if zynthian_gui_config.zyngui.cb_touch(event):
			return "break"

		self.fader_drag_start = event
		if self.chain:
			self.parent.zyngui.chain_manager.set_active_chain_by_object(self.chain)
			self.parent.highlight_active_chain()

	# Function to handle fader press
	# event: Mouse event
	def on_fader_release(self, event):
		self.touch_ts = None

	# Function to handle fader drag
	#	event: Mouse event
	def on_fader_motion(self, event):
		if self.touch_ts:
			dts = monotonic() - self.touch_ts

		if dts < 0.1:  # debounce initial touch
			return
		dy = self.touch_y - event.y
		dx = event.x - self.touch_x

		# Lock drag to x or y axis only after one has been started
		if self.drag_axis is None:
			if abs(dy) > 2:
				self.drag_axis = "y"
			elif abs(dx) > 2:
				self.drag_axis = "x"

		if self.drag_axis == "y":
			self.set_volume(self.zctrls['level'].value + (self.touch_y - event.y) / self.fader_height)
			self.touch_y = event.y
		elif self.drag_axis == "x":
			self.set_balance(self.zctrls['balance'].value - (self.touch_x - event.x) / self.fader_width)
			self.touch_x = event.x

	# Function to handle mouse wheel down over fader
	#	event: Mouse event
	def on_fader_wheel_down(self, event):
		self.nudge_volume(-1)

	# Function to handle mouse wheel up over fader
	#	event: Mouse event
	def on_fader_wheel_up(self, event):
		self.nudge_volume(1)

	# Function to handle mouse click / touch of balance
	#	event: Mouse event
	def on_balance_press(self, event):
		if self.parent.zynmixer.midi_learn_zctrl:
			if self.parent.zynmixer.midi_learn_zctrl != self.zctrls["balance"]:
				self.parent.zynmixer.midi_learn_zctrl = self.zctrls["balance"]

	# Function to handle mouse wheel down over balance
	#	event: Mouse event
	def on_balance_wheel_down(self, event):
		self.nudge_balance(-1)

	# Function to handle mouse wheel up over balance
	#	event: Mouse event
	def on_balance_wheel_up(self, event):
		self.nudge_balance(1)

	# Function to handle mixer strip press
	#	event: Mouse event
	def on_strip_press(self, event):
		if zynthian_gui_config.zyngui.cb_touch(event):
			return "break"

		self.strip_drag_start = event
		self.dragging = False
		if self.chain:
			self.parent.zyngui.chain_manager.set_active_chain_by_object(self.chain)

	# Function to handle legend strip release
	def on_strip_release(self, event):
		if zynthian_gui_config.zyngui.cb_touch_release(event):
			return "break"

		if self.parent.zynmixer.midi_learn_zctrl:
			return
		if self.strip_drag_start and not self.dragging:
			delta = event.time - self.strip_drag_start.time
			if delta > 400:
				zynthian_gui_config.zyngui.screens['chain_options'].setup(self.chain_id)
				zynthian_gui_config.zyngui.show_screen('chain_options')
			else:
				zynthian_gui_config.zyngui.chain_control(self.chain_id)
		self.dragging = False
		self.parent.moving_chain = False
		self.strip_drag_start = None
		self.parent.refresh_visible_strips()

	# Function to handle legend strip drag
	def on_strip_motion(self, event):
		if self.strip_drag_start:
			delta = event.x - self.strip_drag_start.x
			if delta > self.width:
				offset = +1
			elif delta < -self.width:
				offset = -1
			else:
				return
			# Dragged more than one strip width
			self.dragging = True
			if self.parent.moving_chain:
				self.parent.zyngui.chain_manager.move_chain(offset)
			elif self.parent.mixer_strip_offset - offset >= 0 and self.parent.mixer_strip_offset - offset + len(self.parent.visible_mixer_strips) <= len(self.parent.zyngui.chain_manager.chains):
				self.parent.mixer_strip_offset -= offset
			self.strip_drag_start.x = event.x
			self.parent.refresh_visible_strips()
			self.parent.highlight_active_chain()

	# Function to handle mute button release
	#	event: Mouse event
	def on_mute_release(self, event):
		self.toggle_mute()

	# Function to handle solo button release
	#	event: Mouse event
	def on_solo_release(self, event):
		self.toggle_solo()

# ------------------------------------------------------------------------------
# Zynthian Mixer GUI Class
# ------------------------------------------------------------------------------


class zynthian_gui_mixer(zynthian_gui_base.zynthian_gui_base):

	def __init__(self):
		super().__init__(has_backbutton=False)

		self.zynmixer = self.zyngui.state_manager.zynmixer
		self.zynmixer.set_midi_learn_cb(self.enter_midi_learn)
		self.MAIN_MIXBUS_STRIP_INDEX = self.zynmixer.MAX_NUM_CHANNELS - 1
		self.chan2strip = [None] * (self.MAIN_MIXBUS_STRIP_INDEX + 1)
		self.highlighted_strip = None # highligted mixer strip object
		self.moving_chain = False  # True if moving a chain left/right

		self.pending_refresh_queue = set()  # List of (strip,control) requiring gui refresh (control=None for whole strip refresh)
		# TODO: Should avoid duplicating midi_learn_zctrl from zynmixer but would need more safeguards to make change.
		self.midi_learn_sticky = None

		visible_chains = zynthian_gui_config.visible_mixer_strips  # Maximum quantity of mixer strips to display (Defines strip width. Main always displayed.)
		if visible_chains < 1:
			# Automatic sizing if not defined in config
			if self.width <= 400: visible_chains = 6
			elif self.width <= 600: visible_chains = 8
			elif self.width <= 800: visible_chains = 10
			elif self.width <= 1024: visible_chains = 12
			elif self.width <= 1280: visible_chains = 14
			else: visible_chains = 16

		self.fader_width = (self.width - 6 ) / (visible_chains + 1)
		self.legend_height = self.height * 0.05
		self.edit_height = self.height * 0.1
		
		self.fader_height = self.height - self.edit_height - self.legend_height - 2
		self.fader_bottom = self.height - self.legend_height
		self.fader_top = self.fader_bottom - self.fader_height
		self.balance_control_height = self.fader_height * 0.1
		self.balance_top = self.fader_top
		self.balance_control_width = self.width / 4  # Width of each half of balance control
		self.balance_control_centre = self.fader_width + self.balance_control_width

		# Arrays of GUI elements for mixer strips - Chains + Main
		self.visible_mixer_strips = [None] * visible_chains  # List of mixer strip objects indexed by horizontal position on screen
		self.mixer_strip_offset = 0  # Index of first mixer strip displayed on far left

		# Fader Canvas
		self.main_canvas = tkinter.Canvas(self.main_frame,
			height=1,
			width=1,
			bd=0, highlightthickness=0,
			bg = zynthian_gui_config.color_panel_bg)
		self.main_frame.rowconfigure(0, weight=1)
		self.main_frame.columnconfigure(0, weight=1)
		self.main_canvas.grid(row=0, sticky='nsew')

		# Create mixer strip UI objects
		for strip in range(len(self.visible_mixer_strips)):
			self.visible_mixer_strips[strip] = zynthian_gui_mixer_strip(self, 1 + self.fader_width * strip, 0, self.fader_width - 1, self.height)

		self.main_mixbus_strip = zynthian_gui_mixer_strip(self, self.width - self.fader_width - 1, 0, self.fader_width - 1, self.height)
		self.main_mixbus_strip.set_chain(0)
		self.main_mixbus_strip.zctrls = self.zynmixer.zctrls[self.MAIN_MIXBUS_STRIP_INDEX]

		self.zynmixer.enable_dpm(0, self.MAIN_MIXBUS_STRIP_INDEX, False)

		self.set_title()
		self.refresh_visible_strips()

	def init_dpmeter(self):
		self.dpm_a = self.dpm_b = None

	# Redefine set_title
	def set_title(self, title="Mixer", fg=None, bg=None, timeout = None):
		if title == "Mixer" and self.zyngui.state_manager.last_snapshot_fpath:
			fparts = os.path.splitext(self.zyngui.state_manager.last_snapshot_fpath)
			if self.zyngui.screens['snapshot'].bankless_mode:
				ssname = os.path.basename(fparts[0]) + " - " + self.zyngui.state_manager.current_zs3_title
			else:
				ssname = fparts[0].rsplit("/", 1)[-1]
			title = ssname.replace("last_state", "Last State")

		super().set_title(title, fg, bg, timeout)

	# Function to handle hiding display
	def hide(self):
		if self.shown:
			if not self.zyngui.osc_clients:
				self.zynmixer.enable_dpm(0, self.MAIN_MIXBUS_STRIP_INDEX - 1, False)
			if not self.midi_learn_sticky:
				self.exit_midi_learn()
				zynsigman.unregister(zynsigman.S_AUDIO_MIXER, self.zynmixer.SS_ZCTRL_SET_VALUE, self.update_control)
				zynsigman.unregister(zynsigman.S_STATE_MAN, self.zyngui.state_manager.SS_LOAD_ZS3, self.cb_load_zs3)
				zynsigman.unregister(zynsigman.S_CHAIN_MAN, self.zyngui.chain_manager.SS_SET_ACTIVE_CHAIN, self.update_active_chain)
				zynsigman.unregister(zynsigman.S_AUDIO_RECORDER, zynthian_audio_recorder.SS_AUDIO_RECORDER_ARM, self.update_control_arm)
				zynsigman.unregister(zynsigman.S_AUDIO_RECORDER, zynthian_audio_recorder.SS_AUDIO_RECORDER_STATE, self.update_control_rec)
				zynsigman.unregister(zynsigman.S_AUDIO_PLAYER, zynthian_engine_audioplayer.SS_AUDIO_PLAYER_STATE, self.update_control_play)
			super().hide()

	# Function to handle showing display
	def build_view(self):
		if zynthian_gui_config.enable_touch_navigation and self.moving_chain or self.zynmixer.midi_learn_zctrl:
			self.show_back_button()

		self.set_title()
		if zynthian_gui_config.enable_dpm:
			self.zynmixer.enable_dpm(0, self.MAIN_MIXBUS_STRIP_INDEX, True)
		else:
			# Reset all DPM which will not be updated by refresh
			for strip in self.visible_mixer_strips:
				strip.draw_dpm([-200, -200, -200, -200, False])

		self.highlight_active_chain(True)
		self.setup_zynpots()
		if self.midi_learn_sticky:
			self.enter_midi_learn(self.midi_learn_sticky)
		else:
			zynsigman.register(zynsigman.S_AUDIO_MIXER, self.zynmixer.SS_ZCTRL_SET_VALUE, self.update_control)
			zynsigman.register_queued(zynsigman.S_STATE_MAN, self.zyngui.state_manager.SS_LOAD_ZS3, self.cb_load_zs3)
			zynsigman.register_queued(zynsigman.S_CHAIN_MAN, self.zyngui.chain_manager.SS_SET_ACTIVE_CHAIN, self.update_active_chain)
			zynsigman.register_queued(zynsigman.S_AUDIO_RECORDER, zynthian_audio_recorder.SS_AUDIO_RECORDER_ARM, self.update_control_arm)
			zynsigman.register_queued(zynsigman.S_AUDIO_RECORDER, zynthian_audio_recorder.SS_AUDIO_RECORDER_STATE, self.update_control_rec)
			zynsigman.register_queued(zynsigman.S_AUDIO_PLAYER, zynthian_engine_audioplayer.SS_AUDIO_PLAYER_STATE, self.update_control_play)
		return True

	# Function to update display, e.g. after geometry changes
	def update_layout(self):
		super().update_layout()
		#TODO: Update mixer layout

	# Function to refresh screen (slow)
	def refresh_status(self):
		if self.shown:
			super().refresh_status()
			# Update main chain DPM
			state = self.zynmixer.get_dpm_states(255, 255)[0]
			self.main_mixbus_strip.draw_dpm(state)
			# Update other chains DPM
			if zynthian_gui_config.enable_dpm:
				states = self.zynmixer.get_dpm_states(0, self.MAIN_MIXBUS_STRIP_INDEX - 1)
				for strip in self.visible_mixer_strips:
					if not strip.hidden and strip.chain.mixer_chan is not None:
						state = states[strip.chain.mixer_chan]
						strip.draw_dpm(state)

	# Function to refresh display (fast)
	def plot_zctrls(self):
		while self.pending_refresh_queue:
			ctrl = self.pending_refresh_queue.pop()
			if ctrl[0]:
				ctrl[0].draw_control(ctrl[1])

	# Function to add control to be updated (fast)
	def update_control(self, chan, symbol, value):
		strip = self.chan2strip[chan]
		if not strip or not strip.chain or strip.chain.mixer_chan is None:
			return
		self.pending_refresh_queue.add((strip, symbol))
		strip.zctrls[symbol].set_value(value, False)
		#self.pending_refresh_queue.add((self.chan2strip[self.MAIN_MIXBUS_STRIP_INDEX], "solo"))

	# Function to handle audio recorder arm
	def update_control_arm(self, chan, value):
		self.update_control(chan, "rec", value)

	# Function to handle audio recorder status
	def update_control_rec(self, state):
		for strip in self.visible_mixer_strips:
			self.pending_refresh_queue.add((strip, "rec"))

	# Function to handle audio play status
	def update_control_play(self, handle, state):
		for strip in self.visible_mixer_strips:
			self.pending_refresh_queue.add((strip, "play"))

	# Funtion to handle active chain changes
	def update_active_chain(self, active_chain):
		self.highlight_active_chain()

	def cb_load_zs3(self, zs3_id):
		self.refresh_visible_strips()
 
	# --------------------------------------------------------------------------
	# Mixer Functionality
	# --------------------------------------------------------------------------

	# Function to highlight the selected chain's strip
	def highlight_active_chain(self, refresh=False):
		"""Higlights active chain, redrawing strips if required"""
		try:
			active_index = self.zyngui.chain_manager.ordered_chain_ids.index(self.zyngui.chain_manager.active_chain_id)
		except:
			active_index = 0
		if active_index < self.mixer_strip_offset:
			self.mixer_strip_offset = active_index
			refresh = True
		elif active_index >= self.mixer_strip_offset + len(self.visible_mixer_strips) and self.zyngui.chain_manager.active_chain_id != 0:
			self.mixer_strip_offset = active_index - len(self.visible_mixer_strips) + 1
			refresh = True
		#TODO: Handle aux

		strip = None
		if self.zyngui.chain_manager.active_chain_id == 0:
			strip = self.main_mixbus_strip
		else:
			chain = self.zyngui.chain_manager.get_chain(self.zyngui.chain_manager.active_chain_id)
			for s in self.visible_mixer_strips:
				if s.chain == chain:
					strip = s
					break
			if strip is None:
				refresh = True
		if refresh:
			chan_strip = self.refresh_visible_strips()
			if chan_strip:
				strip = chan_strip
		if self.highlighted_strip and self.highlighted_strip != strip:
			self.highlighted_strip.set_highlight(False)
		if strip is None:
			strip = self.main_mixbus_strip
		self.highlighted_strip = strip
		if strip:
			strip.set_highlight(True)

	# Function refresh and populate visible mixer strips
	def refresh_visible_strips(self):
		"""Update the structures describing the visible strips
		
		Returns - Active strip object
		"""

		active_strip = None
		strip_index = 0
		for chain_id in self.zyngui.chain_manager.ordered_chain_ids[:-1][self.mixer_strip_offset:self.mixer_strip_offset + len(self.visible_mixer_strips)]:
			strip = self.visible_mixer_strips[strip_index]
			strip.set_chain(chain_id)
			#strip.draw_control()
			if strip.chain.mixer_chan is not None and strip.chain.mixer_chan < len(self.chan2strip):
				self.chan2strip[strip.chain.mixer_chan] = strip
			if chain_id == self.zyngui.chain_manager.active_chain_id:
				active_strip = strip
			strip_index += 1
		
		# Hide unpopulated strips
		for strip in self.visible_mixer_strips[strip_index:len(self.visible_mixer_strips)]:
			strip.set_chain(None)
			strip.zctrls = None

		self.chan2strip[self.MAIN_MIXBUS_STRIP_INDEX] = self.main_mixbus_strip
		self.main_mixbus_strip.draw_control()
		return active_strip

	# --------------------------------------------------------------------------
	# Physical UI Control Management: Pots & switches
	# --------------------------------------------------------------------------

	# Function to handle SELECT button press
	# type: Button press duration ["S"=Short, "B"=Bold, "L"=Long]
	def switch_select(self, type='S'):
		if self.moving_chain:
			self.moving_chain = False
			self.refresh_visible_strips()
		elif type == "S":
			if self.zynmixer.midi_learn_zctrl:
				self.zynmixer.midi_learn_zctrl_menu(self.zynmixer.midi_learn_zctrl)
			else:
				self.zyngui.chain_control()
		elif type == "B":
			# Chain Options
			self.zyngui.screens['chain_options'].setup(self.zyngui.chain_manager.active_chain_id)
			self.zyngui.show_screen('chain_options')
		else:
			return False
		return True

	# Handle onscreen back button press => Should we use it for entering MIDI learn?
	#def backbutton_short_touch_action(self):
	#	if not self.back_action():
	#		self.enter_midi_learn()

	# Function to handle BACK action
	def back_action(self):
		if self.moving_chain:
			self.moving_chain = False
			self.refresh_visible_strips()
			return True
		elif self.zynmixer.midi_learn_zctrl == True:
			self.exit_midi_learn()
			return True
		else:
			return super().back_action()

	# Function to handle switches press
	# swi: Switch index [0=Layer, 1=Back, 2=Snapshot, 3=Select]
	# t: Press type ["S"=Short, "B"=Bold, "L"=Long]
	# returns True if action fully handled or False if parent action should be triggered
	def switch(self, swi, t):
		if swi == 0:
			if t == "S":
				if self.highlighted_strip is not None:
					self.highlighted_strip.toggle_solo()
				return True
			elif t == "B" and self.zynmixer.midi_learn_zctrl:
				self.midi_learn_menu()
				return True

		elif swi == 1:
			#if zynthian_gui_config.enable_touch_navigation and self.zynmixer.midi_learn_zctrl:
			#	return False
			# This is ugly, but it's the only way i figured for MIDI-learning "mute" without touch.
			# Moving the "learn" button to back is not an option. It's a labeled button on V4!!
			if t == "S" and not self.moving_chain:
				if self.highlighted_strip is not None:
					self.highlighted_strip.toggle_mute()
				return True
			elif t == "B":
				if self.zynmixer.midi_learn_zctrl:
					self.back_action()
				else:
					self.zyngui.cuia_screen_zynpad()
					return True

		elif swi == 3:
			return self.switch_select(t)

		return False

	def setup_zynpots(self):
		for i in range(zynthian_gui_config.num_zynpots):
			lib_zyncore.setup_behaviour_zynpot(i, 0)

	# Function to handle zynpot CB
	def zynpot_cb(self, i, dval):
		if not self.shown:
			return

		# LAYER encoder adjusts selected chain's level
		if i == 0:
			if self.highlighted_strip is not None:
				self.highlighted_strip.nudge_volume(dval)

		# BACK encoder adjusts selected chain's balance/pan
		if i == 1:
			if self.highlighted_strip is not None:
				self.highlighted_strip.nudge_balance(dval)

		# SNAPSHOT encoder adjusts main mixbus level
		elif i == 2:
			self.main_mixbus_strip.nudge_volume(dval)

		# SELECT encoder moves chain selection
		elif i == 3:
			if self.moving_chain:
				self.zyngui.chain_manager.move_chain(dval)
				self.refresh_visible_strips()
			else:
				self.zyngui.chain_manager.next_chain(dval)
			self.highlight_active_chain()

	# Function to handle CUIA ARROW_LEFT
	def arrow_left(self):
		if self.moving_chain:
			self.zyngui.chain_manager.move_chain(-1)
			self.refresh_visible_strips()
		else:
			self.zyngui.chain_manager.previous_chain()
		self.highlight_active_chain()

	# Function to handle CUIA ARROW_RIGHT
	def arrow_right(self):
		if self.moving_chain:
			self.zyngui.chain_manager.move_chain(1)
			self.refresh_visible_strips()
		else:
			self.zyngui.chain_manager.next_chain()
		self.highlight_active_chain()

	# Function to handle CUIA ARROW_UP
	def arrow_up(self):
		if self.highlighted_strip is not None:
			self.highlighted_strip.nudge_volume(1)
		
	# Function to handle CUIA ARROW_DOWN
	def arrow_down(self):
		if self.highlighted_strip is not None:
			self.highlighted_strip.nudge_volume(-1)

	# --------------------------------------------------------------------------
	# GUI Event Management
	# --------------------------------------------------------------------------

	# Function to handle mouse wheel event when not over fader strip
	# event: Mouse event
	def on_wheel(self, event):
		if event.num == 5:
			if self.mixer_strip_offset < 1:
				return
			self.mixer_strip_offset -= 1
		elif event.num == 4:
			if self.mixer_strip_offset + len(self.visible_mixer_strips) >= self.zyngui.chain_manager.get_chain_count() - 1:
				return
			self.mixer_strip_offset += 1
		self.highlight_active_chain()

	# --------------------------------------------------------------------------
	# MIDI learning management
	# --------------------------------------------------------------------------

	def toggle_menu(self):
		if self.zynmixer.midi_learn_zctrl:
			self.midi_learn_menu()
		else:
			self.zyngui.toggle_screen("main_menu")

	def midi_learn_menu(self):
		options = {}
		try:
			strip_id = self.zynmixer.midi_learn_zctrl.graph_path[0] + 1
			if strip_id == 17:
				strip_id = "Main"
			title = f"MIDI Learn Options ({strip_id})"
		except:
			title = f"MIDI Learn Options"

		if isinstance(self.zynmixer.midi_learn_zctrl, zynthian_controller):
			if self.zynmixer.midi_learn_zctrl.is_toggle:
				if self.zynmixer.midi_learn_zctrl.midi_cc_momentary_switch:
					options["\u2612 Momentary => Latch"] = "latched"
				else:
					options["\u2610 Momentary => Latch"] = "momentary"
		if isinstance(self.zynmixer.midi_learn_zctrl, zynthian_controller):
			options[f"Clean MIDI-learn ({self.zynmixer.midi_learn_zctrl.symbol})"] = "clean"
		else:
			options["Clean MIDI-learn (ALL)"] = "clean"

		self.midi_learn_sticky = self.zynmixer.midi_learn_zctrl
		self.zyngui.screens['option'].config(title, options, self.midi_learn_menu_cb)
		self.zyngui.show_screen('option')

	def midi_learn_menu_cb(self, options, params):
		if params == 'clean':
			self.midi_unlearn_action()
		elif params == "latched":
			self.zynmixer.midi_learn_zctrl.midi_cc_momentary_switch = 0
		elif params == "momentary":
			self.zynmixer.midi_learn_zctrl.midi_cc_momentary_switch = 1

	def enter_midi_learn(self, zctrl=True):
		self.midi_learn_sticky = None
		if self.zynmixer.midi_learn_zctrl == zctrl:
			return
		self.zynmixer.midi_learn_zctrl = zctrl
		if zctrl != True:
			self.zynmixer.enable_midi_learn(zctrl)
		self.refresh_visible_strips()
		if zynthian_gui_config.enable_touch_navigation:
			self.show_back_button(True)

	def exit_midi_learn(self):
		if self.zynmixer.midi_learn_zctrl:
			self.zynmixer.midi_learn_zctrl = None
			self.zynmixer.disable_midi_learn()
			self.refresh_visible_strips()
			if zynthian_gui_config.enable_touch_navigation:
				self.show_back_button(False)

	# Pre-select all controls in a chain to allow selection of actual control to MIDI learn
	def toggle_midi_learn(self):
		match self.zynmixer.midi_learn_zctrl:
			case True:
				self.exit_midi_learn()
			case None:
				self.enter_midi_learn(True)
			case _:
				self.enter_midi_learn()

	def midi_unlearn_action(self):
		self.midi_learn_sticky = self.zynmixer.midi_learn_zctrl
		if isinstance(self.zynmixer.midi_learn_zctrl, zynthian_controller):
			self.zyngui.show_confirm(f"Do you want to clear MIDI-learn for '{self.zynmixer.midi_learn_zctrl.name}' control?", self.midi_unlearn_cb, self.zynmixer.midi_learn_zctrl)
		else:
			self.zyngui.show_confirm("Do you want to clean MIDI-learn for ALL mixer controls?", self.midi_unlearn_cb)

	def midi_unlearn_cb(self, zctrl=None):
		if zctrl:
			self.zynmixer.midi_unlearn(zctrl)
		else:
			self.zynmixer.midi_unlearn_all()
		self.zynmixer.midi_learn_zctrl = True
		self.refresh_visible_strips()

# --------------------------------------------------------------------------
