import json
from pathlib import Path

charset = " ?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/+!！%×·-乐器工坊太鼓吉他设置返回中文山海疾风朱雀夜奔破阵新声连击中断分数预备开始完美良好失误空振演奏完成结束结算选择乐曲鼓心鼓边低中高校准触屏模式初阵疾行鼓舞下挥扭腕暂停停止切曲音量最高准确率直击可请触发松开阈值间隔无模块控制品弦判定"

scene = {
  "screen": "tof_instruments",
  "w": 480,
  "h": 480,
  "screen_bg": "#0C0E12",
  "font": "fonts/InstrumentSans-Bold.ttf",
  "font_charset": charset,
  "default_font_size": 16,
  "swipe": False,
  "objects": [
    {
      "type": "container",
      "parent": -1,
      "name": "root",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "bg_color": "#0C0E12"
    },
    {
      "type": "image",
      "parent_name": "root",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/festival-bg.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "root",
      "name": "song_label",
      "bind": "song_text",
      "bind_target": "text",
      "x": 100, "y": 27, "w": 176, "h": 22,
      "text": "山海疾风 · 120 BPM",
      "font_size": 16,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "progress",
      "parent_name": "root",
      "name": "song_progress",
      "bind": "song_progress",
      "bind_target": "value",
      "x": 111, "y": 68, "w": 153, "h": 5,
      "min": 0, "max": 100, "value": 42,
      "bg_color": "#1E2228",
      "fg_color": "#FF7034",
      "radius": 2
    },
    {
      "type": "label",
      "parent_name": "root",
      "name": "score_label",
      "bind": "score_text",
      "bind_target": "text",
      "x": 316, "y": 32, "w": 156, "h": 34,
      "text": "0024800",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "progress",
      "parent_name": "root",
      "name": "motion_meter",
      "bind": "motion_level",
      "bind_target": "value",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "min": 0, "max": 100, "value": 0,
      "bg_color": "#152126", "fg_color": "#52DED3",
      "radius": 1, "hidden": True
    },
    {
      "type": "button",
      "parent_name": "root",
      "x": 21, "y": 20, "w": 68, "h": 60,
      "name": "play_back",
      "bind": "play_back_text",
      "bind_target": "text",
      "text": "返回",
      "font_size": 16,
      "bg_color": "#00000000",
      "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 10,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "game_stop"}]
    },
    {
      "type": "button",
      "parent_name": "root",
      "name": "volume_button",
      "bind": "volume_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "",
      "font_size": 16,
      "bg_color": "#00000000",
      "fg_color": "#00000000",
      "border_width": 0, "radius": 1,
      "hidden": True,
      "events": [{"event": "press", "action": "call", "callback": "volume"}]
    },
    {
      "type": "button",
      "parent_name": "root",
      "name": "imu_button",
      "bind": "imu_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "hidden": True,
      "events": [{"event": "press", "action": "call", "callback": "sensitivity"}]
    },
    {
      "type": "button",
      "parent_name": "root",
      "name": "song_button",
      "bind": "song_button_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "hidden": True,
      "events": [{"event": "press", "action": "call", "callback": "song_select"}]
    },
    {
      "type": "image",
      "parent_name": "root",
      "name": "note_lane",
      "bind": "note_lane",
      "x": 8, "y": 274, "w": 464, "h": 120,
      "image": "assets/note-lane.png",
      "codec": "lossless"
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "judge_readout",
      "bind": "judge_readout_visible",
      "bind_target": "visible",
      "x": 12, "y": 110, "w": 224, "h": 112,
      "bg_color": "#00000000",
      "bg_opacity": 0,
      "border_width": 0,
      "border_opacity": 0,
      "outline_width": 0,
      "outline_opacity": 0,
      "shadow_opacity": 0
    },
    {
      "type": "label",
      "parent_name": "judge_readout",
      "name": "judge_label",
      "bind": "judge_text",
      "bind_target": "text",
      "x": 86, "y": 40, "w": 116, "h": 36,
      "text": "完美",
      "font_size": 24,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "root",
      "name": "combo_caption",
      "bind": "combo_caption_text",
      "bind_target": "text",
      "x": 328, "y": 146, "w": 108, "h": 18,
      "text": "连击",
      "font_size": 14,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "root",
      "name": "combo_label",
      "bind": "combo_text",
      "bind_target": "text",
      "x": 328, "y": 162, "w": 108, "h": 34,
      "text": "24",
      "font_size": 28,
      "fg_color": "#40D6FF",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "center_flash",
      "bind": "center_flash_visible",
      "bind_target": "visible",
      "x": 51, "y": 278, "w": 44, "h": 44,
      "bg_color": "#FF703433",
      "border_color": "#FF7034",
      "border_width": 3,
      "radius": 22,
      "hidden": True
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "rim_flash",
      "bind": "rim_flash_visible",
      "bind_target": "visible",
      "x": 47, "y": 274, "w": 52, "h": 52,
      "bg_color": "#40D6FF26",
      "border_color": "#40D6FF",
      "border_width": 3,
      "radius": 26,
      "hidden": True
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "perfect_burst",
      "bind": "perfect_burst_visible",
      "bind_target": "visible",
      "x": 12, "y": 110, "w": 224, "h": 112,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "perfect_burst",
      "x": 0, "y": 0, "w": 224, "h": 112,
      "image": "assets/judge-perfect.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "perfect_burst",
      "x": 86, "y": 40, "w": 116, "h": 36,
      "name": "perfect_label",
      "bind": "perfect_label_text",
      "bind_target": "text",
      "text": "完美",
      "font_size": 24,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "good_burst",
      "bind": "good_burst_visible",
      "bind_target": "visible",
      "x": 12, "y": 110, "w": 224, "h": 112,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "good_burst",
      "x": 0, "y": 0, "w": 224, "h": 112,
      "image": "assets/judge-good.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "good_burst",
      "x": 86, "y": 40, "w": 116, "h": 36,
      "name": "good_label",
      "bind": "good_label_text",
      "bind_target": "text",
      "text": "良好",
      "font_size": 24,
      "fg_color": "#40D6FF",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "miss_burst",
      "bind": "miss_burst_visible",
      "bind_target": "visible",
      "x": 12, "y": 110, "w": 224, "h": 112,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "miss_burst",
      "x": 0, "y": 0, "w": 224, "h": 112,
      "image": "assets/judge-miss.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "miss_burst",
      "name": "miss_burst_label",
      "bind": "miss_burst_text",
      "bind_target": "text",
      "x": 86, "y": 40, "w": 116, "h": 36,
      "text": "失误",
      "font_size": 24,
      "fg_color": "#FF5C5C",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "root",
      "name": "combo_burst_panel",
      "bind": "combo_burst_visible",
      "bind_target": "visible",
      "x": 252, "y": 162, "w": 200, "h": 48,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "combo_burst_panel",
      "x": 0, "y": 0, "w": 200, "h": 48,
      "image": "assets/combo-burst-bg.jpg",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "combo_burst_panel",
      "name": "combo_burst_label",
      "bind": "combo_burst_text",
      "bind_target": "text",
      "x": 46, "y": 6, "w": 136, "h": 34,
      "text": "50 连击！",
      "font_size": 20,
      "fg_color": "#FFD248",
      "text_align": "center"
    },

    {
      "type": "container",
      "parent_name": "root",
      "name": "menu_layer",
      "bind": "menu_visible",
      "bind_target": "visible",
      "hidden": False,
      "x": 0, "y": 0, "w": 480, "h": 480,
      "bg_color": "#0C0E12"
    },

    {
      "type": "container",
      "parent_name": "menu_layer",
      "name": "start_panel",
      "bind": "start_visible",
      "bind_target": "visible",
      "hidden": False,
      "x": 0, "y": 0, "w": 480, "h": 480
    },
    {
      "type": "image",
      "parent_name": "start_panel",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/start-bg.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "start_panel",
      "name": "home_title",
      "bind": "home_title_text",
      "bind_target": "text",
      "x": 154, "y": 48, "w": 176, "h": 38,
      "text": "乐器工坊",
      "font_size": 25,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "start_panel",
      "name": "start_play_label",
      "bind": "start_play_text",
      "bind_target": "text",
      "x": 62, "y": 303, "w": 140, "h": 32,
      "text": "太鼓",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "start_panel",
      "name": "start_play_button",
      "x": 36, "y": 134, "w": 192, "h": 224,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 18,
      "events": [{"event": "press", "action": "call", "callback": "open_select"}]
    },
    {
      "type": "label",
      "parent_name": "start_panel",
      "name": "start_guitar_label",
      "bind": "start_guitar_text",
      "bind_target": "text",
      "x": 278, "y": 303, "w": 140, "h": 32,
      "text": "吉他",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "start_panel",
      "name": "start_guitar_button",
      "x": 252, "y": 134, "w": 192, "h": 224,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 18,
      "events": [{"event": "press", "action": "call", "callback": "open_guitar"}]
    },
    {
      "type": "button",
      "parent_name": "start_panel",
      "name": "start_settings_button",
      "bind": "start_settings_text",
      "bind_target": "text",
      "x": 148, "y": 382, "w": 184, "h": 58,
      "text": "设置",
      "font_size": 20,
      "bg_color": "#00000000",
      "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 16,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "open_settings"}]
    },

    {
      "type": "container",
      "parent_name": "menu_layer",
      "name": "guitar_panel",
      "bind": "guitar_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "guitar_panel",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/guitar-bg.png",
      "codec": "lossless",
      "fit": "cover"
    }
  ]
}

# Full orange keycap overlay (74x86, includes side walls and feet).
chords = ["C", "G", "Am", "F", "Em", "D"]
cap_img_xs = [20, 96, 169, 242, 316, 390]
cap_txt_xs = [31, 107, 183, 253, 327, 400]
for idx, (ix, tx, cname, cb) in enumerate(zip(cap_img_xs, cap_txt_xs, chords,
                                              ["pick_chord_0", "pick_chord_1", "pick_chord_2", "pick_chord_3", "pick_chord_4", "pick_chord_5"])):
    scene["objects"].append({
      "type": "image",
      "parent_name": "guitar_panel",
      "name": f"guitar_chord_{idx}_img",
      "bind": f"guitar_chord_{idx}_visible",
      "bind_target": "visible",
      "x": ix, "y": 32, "w": 74, "h": 86,
      "image": "assets/guitar-cap-active.png",
      "codec": "lossless",
      "fit": "cover",
      "hidden": (idx != 0)
    })
    scene["objects"].append({
      "type": "button",
      "parent_name": "guitar_panel",
      "name": f"guitar_chord_{idx}_btn",
      "x": tx, "y": 40, "w": 50, "h": 54,
      "text": cname, "font_size": 20,
      "bg_color": "#00000000", "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 10,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": cb}]
    })

# 6 strings x 3 frets = 18 fret dot containers (hidden unless active in the selected chord!)
str_xs = [332, 295, 258, 220, 181, 142] # s0=E2(6th)..s5=E4(1st)
fret_ys = [180, 219, 256]               # f0=1st fret, f1=2nd fret, f2=3rd fret
for s_i, sx in enumerate(str_xs):
    for f_i, fy in enumerate(fret_ys):
        scene["objects"].append({
          "type": "container",
          "parent_name": "guitar_panel",
          "name": f"gdot_s{s_i}_f{f_i}",
          "bind": f"gdot_s{s_i}_f{f_i}_visible",
          "bind_target": "visible",
          "x": sx - 13, "y": fy - 13, "w": 26, "h": 26,
          "bg_color": "#FF6A26",
          "border_color": "#FFD28C",
          "border_width": 2,
          "radius": 13,
          "hidden": not ((s_i == 4 and f_i == 0) or (s_i == 2 and f_i == 1) or (s_i == 1 and f_i == 2))
        })

scene["objects"].extend([
    {
      "type": "button",
      "parent_name": "guitar_panel",
      "name": "guitar_strum_area",
      "x": 96, "y": 120, "w": 284, "h": 210,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 10,
      "events": [{"event": "press", "action": "call", "callback": "guitar_strum"}]
    },
    {
      "type": "label",
      "parent_name": "guitar_panel",
      "name": "guitar_fret1_label",
      "bind": "guitar_fret1_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "",
      "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "guitar_panel",
      "name": "guitar_fret2_label",
      "bind": "guitar_fret2_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "",
      "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "guitar_panel",
      "name": "guitar_fret3_label",
      "bind": "guitar_fret3_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "",
      "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "button",
      "parent_name": "guitar_panel",
      "name": "guitar_back_button",
      "bind": "guitar_back_text",
      "bind_target": "text",
      "x": 166, "y": 362, "w": 148, "h": 58,
      "text": "返回",
      "font_size": 20,
      "bg_color": "#00000000",
      "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 16,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "open_start"}]
    },

    {
      "type": "container",
      "parent_name": "menu_layer",
      "name": "select_panel",
      "bind": "select_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "select_panel",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/select-bg.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "button",
      "parent_name": "select_panel",
      "name": "select_back",
      "bind": "select_back_text",
      "bind_target": "text",
      "x": 28, "y": 32, "w": 68, "h": 52,
      "text": "返回",
      "font_size": 16,
      "bg_color": "#00000000",
      "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 12,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "open_start"}]
    },
    {
      "type": "label",
      "parent_name": "select_panel",
      "name": "select_title",
      "bind": "select_title_text",
      "bind_target": "text",
      "x": 100, "y": 30, "w": 344, "h": 52,
      "text": "选择乐曲",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "image",
      "parent_name": "select_panel",
      "name": "mark_shanhai",
      "bind": "mark_shanhai_visible",
      "bind_target": "visible",
      "x": 24, "y": 108, "w": 432, "h": 82,
      "image": "assets/select-song-0-active.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "select_panel",
      "name": "song_shanhai_button",
      "bind": "song_shanhai_text",
      "bind_target": "text",
      "x": 24, "y": 115, "w": 432, "h": 72,
      "text": "山海疾风 · 120 BPM",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "select_panel",
      "name": "song_shanhai_hit",
      "x": 24, "y": 115, "w": 432, "h": 72,
      "bg_color": "#00000000",
      "bg_opacity": 0,
      "border_width": 0,
      "border_opacity": 0,
      "outline_width": 0,
      "outline_opacity": 0,
      "shadow_opacity": 0,
      "events": [{"event": "press", "action": "call", "callback": "pick_shanhai"}]
    },
    {
      "type": "image",
      "parent_name": "select_panel",
      "name": "mark_zhuque",
      "bind": "mark_zhuque_visible",
      "bind_target": "visible",
      "x": 24, "y": 202, "w": 432, "h": 82,
      "image": "assets/select-song-1-active.png",
      "codec": "lossless",
      "fit": "cover",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "select_panel",
      "x": 24, "y": 207, "w": 432, "h": 72,
      "name": "song_zhuque_button",
      "bind": "song_zhuque_text",
      "bind_target": "text",
      "text": "朱雀夜奔 · 138 BPM",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "select_panel",
      "name": "song_zhuque_hit",
      "x": 24, "y": 207, "w": 432, "h": 72,
      "bg_color": "#00000000",
      "bg_opacity": 0,
      "border_width": 0,
      "border_opacity": 0,
      "outline_width": 0,
      "outline_opacity": 0,
      "shadow_opacity": 0,
      "events": [{"event": "press", "action": "call", "callback": "pick_zhuque"}]
    },
    {
      "type": "image",
      "parent_name": "select_panel",
      "name": "mark_pozhen",
      "bind": "mark_pozhen_visible",
      "bind_target": "visible",
      "x": 24, "y": 294, "w": 432, "h": 82,
      "image": "assets/select-song-2-active.png",
      "codec": "lossless",
      "fit": "cover",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "select_panel",
      "x": 24, "y": 297, "w": 432, "h": 72,
      "name": "song_pozhen_button",
      "bind": "song_pozhen_text",
      "bind_target": "text",
      "text": "破阵新声 · 156 BPM",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "container",
      "parent_name": "select_panel",
      "name": "song_pozhen_hit",
      "x": 24, "y": 297, "w": 432, "h": 72,
      "bg_color": "#00000000",
      "bg_opacity": 0,
      "border_width": 0,
      "border_opacity": 0,
      "outline_width": 0,
      "outline_opacity": 0,
      "shadow_opacity": 0,
      "events": [{"event": "press", "action": "call", "callback": "pick_pozhen"}]
    },
    {
      "type": "label",
      "parent_name": "select_panel",
      "name": "selected_label",
      "bind": "selected_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "text_align": "center",
      "hidden": True
    },
    {
      "type": "button",
      "parent_name": "select_panel",
      "name": "select_start",
      "bind": "select_start_text",
      "bind_target": "text",
      "x": 118, "y": 390, "w": 244, "h": 50,
      "text": "开始演奏",
      "font_size": 20,
      "bg_color": "#00000000",
      "fg_color": "#40D6FF",
      "border_width": 0, "radius": 16,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "game_start"}]
    },

    {
      "type": "container",
      "parent_name": "menu_layer",
      "name": "settings_panel",
      "bind": "settings_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "settings_panel",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/settings-bg.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "settings_en_mask",
      "bind": "settings_en_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "button",
      "parent_name": "settings_panel",
      "name": "settings_back",
      "bind": "settings_back_text",
      "bind_target": "text",
      "x": 20, "y": 21, "w": 96, "h": 48,
      "text": "返回",
      "font_size": 16,
      "bg_color": "#00000000",
      "fg_color": "#F8FAFC",
      "border_width": 0, "radius": 10,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "open_start"}]
    },
    {
      "type": "label",
      "parent_name": "settings_panel",
      "name": "settings_title",
      "bind": "settings_title_text",
      "bind_target": "text",
      "x": 144, "y": 21, "w": 180, "h": 48,
      "text": "设置",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "settings_panel",
      "name": "lang_button",
      "bind": "lang_button_text",
      "bind_target": "text",
      "x": 344, "y": 21, "w": 112, "h": 48,
      "text": "EN",
      "font_size": 16,
      "bg_color": "#00000000",
      "fg_color": "#40D6FF",
      "border_width": 0, "radius": 10,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "toggle_language"}]
    },
    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "dual_tof_layer",
      "bind": "dual_tof_visible",
      "bind_target": "visible",
      "x": 0, "y": 76, "w": 480, "h": 112,
      "bg_color": "#00000000"
    },
    {
      "type": "container",
      "parent_name": "dual_tof_layer",
      "name": "test_center",
      "x": 20, "y": 19, "w": 204, "h": 76,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "image",
      "parent_name": "test_center",
      "name": "test_center_flash",
      "bind": "test_center_flash_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 204, "h": 76,
      "image": "assets/settings-center-active.jpg",
      "fit": "cover",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "test_center",
      "name": "test_center_name",
      "bind": "test_center_name_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "test_center",
      "name": "tof_left_label",
      "bind": "tof_left_text",
      "bind_target": "text",
      "x": 12, "y": 16, "w": 180, "h": 42,
      "text": "140 mm",
      "font_size": 25,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "test_center",
      "name": "tof_left_state",
      "bind": "tof_left_state_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "button",
      "parent_name": "test_center",
      "x": 0, "y": 0, "w": 204, "h": 76,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 10,
      "events": [{"event": "press", "action": "call", "callback": "test_center"}]
    },
    {
      "type": "container",
      "parent_name": "dual_tof_layer",
      "name": "test_rim",
      "x": 252, "y": 19, "w": 204, "h": 76,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "image",
      "parent_name": "test_rim",
      "name": "test_rim_flash",
      "bind": "test_rim_flash_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 204, "h": 76,
      "image": "assets/settings-rim-active.jpg",
      "fit": "cover",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "test_rim",
      "name": "test_rim_name",
      "bind": "test_rim_name_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "test_rim",
      "name": "tof_right_label",
      "bind": "tof_right_text",
      "bind_target": "text",
      "x": 12, "y": 16, "w": 180, "h": 42,
      "text": "280 mm",
      "font_size": 25,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "test_rim",
      "name": "tof_right_state",
      "bind": "tof_right_state_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "button",
      "parent_name": "test_rim",
      "x": 0, "y": 0, "w": 204, "h": 76,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 10,
      "events": [{"event": "press", "action": "call", "callback": "test_rim"}]
    },

    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "single_tof_layer",
      "bind": "single_tof_visible",
      "bind_target": "visible",
      "x": 0, "y": 76, "w": 480, "h": 112,
      "bg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "single_tof_layer",
      "x": 0, "y": 0, "w": 480, "h": 112,
      "image": "assets/settings-single-base.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "image",
      "parent_name": "single_tof_layer",
      "name": "single_tof_flash",
      "bind": "single_tof_flash_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 480, "h": 112,
      "image": "assets/settings-single-flash.png",
      "codec": "lossless",
      "fit": "cover",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "single_tof_layer",
      "name": "tof_single_label",
      "bind": "tof_single_text",
      "bind_target": "text",
      "x": 140, "y": 36, "w": 200, "h": 42,
      "text": "140 mm",
      "font_size": 25,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "single_tof_layer",
      "name": "single_tof_test",
      "x": 60, "y": 0, "w": 360, "h": 112,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 10,
      "events": [{"event": "press", "action": "call", "callback": "test_center"}]
    },

    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "strike_row",
      "x": 28, "y": 194, "w": 424, "h": 50,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "label",
      "parent_name": "strike_row",
      "name": "strike_caption",
      "bind": "strike_caption_text",
      "bind_target": "text",
      "x": 28, "y": 9, "w": 128, "h": 32,
      "text": "触发阈值",
      "font_size": 16,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "strike_row",
      "x": 184, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "strike_down"}]
    },
    {
      "type": "label",
      "parent_name": "strike_row",
      "name": "strike_label",
      "bind": "strike_text",
      "bind_target": "text",
      "x": 250, "y": 9, "w": 96, "h": 32,
      "text": "180 mm",
      "font_size": 16,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "strike_row",
      "x": 360, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "strike_up"}]
    },

    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "release_row",
      "x": 28, "y": 262, "w": 424, "h": 50,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "label",
      "parent_name": "release_row",
      "name": "release_caption",
      "bind": "release_caption_text",
      "bind_target": "text",
      "x": 28, "y": 9, "w": 128, "h": 32,
      "text": "松开阈值",
      "font_size": 16,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "release_row",
      "x": 184, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "release_down"}]
    },
    {
      "type": "label",
      "parent_name": "release_row",
      "name": "release_label",
      "bind": "release_text",
      "bind_target": "text",
      "x": 250, "y": 9, "w": 96, "h": 32,
      "text": "260 mm",
      "font_size": 16,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "release_row",
      "x": 360, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "release_up"}]
    },

    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "interval_row",
      "x": 28, "y": 330, "w": 424, "h": 50,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "label",
      "parent_name": "interval_row",
      "name": "interval_caption",
      "bind": "interval_caption_text",
      "bind_target": "text",
      "x": 28, "y": 9, "w": 128, "h": 32,
      "text": "触发间隔",
      "font_size": 16,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "interval_row",
      "x": 184, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "interval_down"}]
    },
    {
      "type": "label",
      "parent_name": "interval_row",
      "name": "interval_label",
      "bind": "interval_text",
      "bind_target": "text",
      "x": 250, "y": 9, "w": 96, "h": 32,
      "text": "120 ms",
      "font_size": 16,
      "fg_color": "#40D6FF",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "interval_row",
      "x": 360, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "interval_up"}]
    },

    {
      "type": "container",
      "parent_name": "settings_panel",
      "name": "vol_row",
      "x": 28, "y": 400, "w": 424, "h": 50,
      "bg_color": "#00000000",
      "border_width": 0, "radius": 10
    },
    {
      "type": "label",
      "parent_name": "vol_row",
      "name": "vol_caption",
      "bind": "vol_caption_text",
      "bind_target": "text",
      "x": 28, "y": 9, "w": 128, "h": 32,
      "text": "音量控制",
      "font_size": 16,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "vol_row",
      "x": 184, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "vol_down"}]
    },
    {
      "type": "label",
      "parent_name": "vol_row",
      "name": "vol_val_label",
      "bind": "vol_val_text",
      "bind_target": "text",
      "x": 250, "y": 9, "w": 96, "h": 32,
      "text": "80 %",
      "font_size": 16,
      "fg_color": "#FFD248",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "vol_row",
      "x": 360, "y": 0, "w": 56, "h": 50,
      "text": "", "font_size": 16,
      "bg_color": "#00000000", "fg_color": "#00000000",
      "border_width": 0, "radius": 8,
      "events": [{"event": "press", "action": "call", "callback": "vol_up"}]
    },
    {
      "type": "label",
      "parent_name": "settings_panel",
      "name": "hint_line1",
      "bind": "hint_line1_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },
    {
      "type": "label",
      "parent_name": "settings_panel",
      "name": "hint_line2",
      "bind": "hint_line2_text",
      "bind_target": "text",
      "x": 0, "y": 0, "w": 1, "h": 1,
      "text": "", "font_size": 16,
      "fg_color": "#00000000",
      "hidden": True
    },

    {
      "type": "container",
      "parent_name": "menu_layer",
      "name": "result_panel",
      "bind": "result_visible",
      "bind_target": "visible",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "hidden": True
    },
    {
      "type": "image",
      "parent_name": "result_panel",
      "x": 0, "y": 0, "w": 480, "h": 480,
      "image": "assets/result-bg.png",
      "codec": "lossless",
      "fit": "cover"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "x": 100, "y": 28, "w": 280, "h": 48,
      "name": "result_title",
      "bind": "result_title_text",
      "bind_target": "text",
      "text": "演奏结算",
      "font_size": 20,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_grade",
      "bind": "result_grade_text",
      "bind_target": "text",
      "x": 52, "y": 124, "w": 72, "h": 56,
      "text": "S+",
      "font_size": 25,
      "fg_color": "#FFD248",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_score",
      "bind": "result_score_text",
      "bind_target": "text",
      "x": 144, "y": 129, "w": 284, "h": 48,
      "text": "分数 0098400",
      "font_size": 25,
      "fg_color": "#F8FAFC",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_combo",
      "bind": "result_combo_text",
      "bind_target": "text",
      "x": 38, "y": 238, "w": 186, "h": 40,
      "text": "最高连击 48",
      "font_size": 20,
      "fg_color": "#FF7034",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_accuracy",
      "bind": "result_accuracy_text",
      "bind_target": "text",
      "x": 255, "y": 238, "w": 186, "h": 40,
      "text": "准确率 96%",
      "font_size": 20,
      "fg_color": "#40D6FF",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_stats_perfect",
      "bind": "result_stats_perfect_text",
      "bind_target": "text",
      "x": 38, "y": 325, "w": 132, "h": 38,
      "text": "完美 43",
      "font_size": 20,
      "fg_color": "#FFD248",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_stats_good",
      "bind": "result_stats_good_text",
      "bind_target": "text",
      "x": 174, "y": 325, "w": 132, "h": 38,
      "text": "良好 3",
      "font_size": 20,
      "fg_color": "#40D6FF",
      "text_align": "center"
    },
    {
      "type": "label",
      "parent_name": "result_panel",
      "name": "result_stats",
      "bind": "result_stats_text",
      "bind_target": "text",
      "x": 308, "y": 325, "w": 132, "h": 38,
      "text": "失误 0",
      "font_size": 20,
      "fg_color": "#FF5C5C",
      "text_align": "center"
    },
    {
      "type": "button",
      "parent_name": "result_panel",
      "x": 118, "y": 394, "w": 244, "h": 50,
      "name": "result_back",
      "bind": "result_back_text",
      "bind_target": "text",
      "text": "返回选曲",
      "font_size": 20,
      "bg_color": "#00000000",
      "fg_color": "#40D6FF",
      "border_width": 0, "radius": 16,
      "text_align": "center",
      "events": [{"event": "press", "action": "call", "callback": "result_back"}]
    }
  ])

Path("projects/tof_instruments/ui/main.json").write_text(
    json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
)
print("Wrote projects/tof_instruments/ui/main.json with", len(scene["objects"]), "objects!")
