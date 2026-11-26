#!/bin/sh
# Host-side checks (no ESP-IDF needed). Run from the project root: sh tools/tests/run.sh
set -e
M=main; S=tools/tests/stubs; T=tools/tests; O=$(mktemp -d)
INC="-I$S -I$M -I$M/board -I$M/drivers -I$M/midi -I$M/ui -I$M/ui/generated -I$M/screens -I$M/app -I$M/viz -I$M/ble -I$M/studio -I$M/learn"
echo "1. compile every source file with -Wall -Wextra -Werror (stub IDF headers)"
for f in $(find $M -name '*.c'); do
  case $f in
    */viz/display_*) W="-Wall -Wno-unused-variable" ;;   # v1 visualisers are kept unchanged
    *) W="-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare" ;;
  esac
  gcc -std=gnu17 -Os $W -c $INC $f -o $O/x.o 2>/dev/null || gcc -std=gnu17 -Os $W -c $INC $f -o $O/x.o
done
echo "   ok"
echo "2. MIDI file loader"
gcc -std=gnu17 -O1 $INC $T/test_smf.c $M/midi/smf.c $M/midi/gm.c $T/host_shim.c -o $O/test_smf
$O/test_smf $T/t1.mid $T/t0.mid
gcc -std=gnu17 -O1 -w $INC $T/test_incomplete.c $M/midi/smf.c $M/midi/gm.c $T/host_shim.c -o $O/test_incomplete
echo "   cut-short files:"
$O/test_incomplete $T/t1.mid
echo "3. player timing simulation (0 plain, 1 mute, 2 seek, 3 pause, 4 repeat with 1 s gap, 5 seek during the gap)"
gcc -std=gnu17 -O1 -w $INC $T/sim_player.c $M/midi/smf.c $M/midi/gm.c $T/host_shim.c -o $O/sim_player
for s in 0 1 2 3 4 5; do echo "--- scenario $s"; $O/sim_player $s | grep -v '^\[smf\]'; done
echo "4. every visualiser through viz.c (frames in $O)"
gcc -std=gnu17 -O2 -w $INC $T/test_viz.c $M/ui/viz.c $M/ui/theme.c $M/midi/monitor.c $M/viz/*.c $M/ui/gfx.c $M/ui/fonts/*.c $M/ui/generated/icons.c -lm -o $O/test_viz
(cd $O && ./test_viz)
echo "5. Studio song model: edits, quantise, undo, save/load, .mid export (read back by the player)"
gcc -std=gnu17 -O1 -w $INC $T/test_song.c $M/studio/song.c $M/midi/smf.c $M/midi/gm.c $T/host_shim.c $T/host_sem.c -o $O/test_song
(cd $O && ./test_song | tail -3)
echo "6. Studio sequencer: count-in, loop recording, overdub, undo pass, keep"
gcc -std=gnu17 -O1 -w $INC $T/sim_seq.c $M/studio/song.c $T/host_shim.c $T/host_sem.c -o $O/sim_seq
$O/sim_seq | grep -E "==|track"
echo "7. real screen code rendered to $O/shot_*.raw"
gcc -std=gnu17 -O1 -w $INC $T/shots.c $M/screens/scr_projects.c $M/screens/scr_studio.c $M/screens/scr_record.c $M/screens/scr_edit.c $M/screens/scr_notes.c $M/learn/ln_ui.c $M/learn/ln_store.c $M/learn/ln_song.c $M/learn/ln_builtin.c $M/midi/smf.c $M/screens/scr_transfer.c $M/screens/scr_bt.c $M/screens/scr_livein.c $M/screens/scr_settings.c $M/ui/widgets.c $M/ui/gfx.c $M/ui/theme.c $M/ui/dialog.c $M/ui/viz.c $M/ui/fonts/*.c $M/ui/generated/icons.c $M/viz/*.c $M/midi/monitor.c $M/midi/gm.c $M/studio/song.c $T/host_shim.c $T/host_sem.c -lm -o $O/shots
(cd $O && ./shots)
echo "8. C text renderer vs mockup renderer"
gcc -std=gnu17 -O1 -I$M/ui -I$M/ui/generated $T/test_gfx.c $M/ui/gfx.c $M/ui/fonts/*.c $M/ui/generated/icons.c -o $O/test_gfx
(cd $O && ./test_gfx) && echo "   wrote $O/c.bin"
echo "9. buttons: short taps, bounce, Shift combos, repeat, busy UI"
gcc -std=gnu17 -O1 -Wall -Wextra -I$S -I$M -I$M/board -I$M/drivers $T/test_buttons.c $M/drivers/buttons.c -o $O/test_buttons
$O/test_buttons | tail -1
echo "10. Player skips unplayable files in Folder / Shuffle"
gcc -std=gnu17 -O1 -w $INC $T/test_skip.c $M/screens/scr_player.c $M/ui/widgets.c $M/ui/gfx.c $M/ui/theme.c $M/ui/dialog.c $M/ui/viz.c $M/ui/fonts/*.c $M/ui/generated/icons.c $M/viz/*.c $M/midi/monitor.c $M/midi/gm.c $T/host_shim.c -lm -o $O/test_skip
$O/test_skip | tail -1
echo "11. Learn engine: timing grades, misses, wait mode, count-in, loop, speed, keyboard range, scores"
gcc -std=gnu17 -O1 -w $INC $T/test_learn.c $M/learn/ln_engine.c $M/learn/ln_song.c $M/learn/ln_store.c $M/learn/ln_builtin.c $T/host_sem.c -o $O/test_learn
$O/test_learn | tail -1
echo "12. Learn from MIDI files: built-in songs as .mid, read back, same notes / hands / bars"
python3 tools/learn_songgen.py --midi $O/learn_mid > /dev/null
gcc -std=gnu17 -O1 -w $INC $T/test_learn_files.c $M/learn/ln_song.c $M/learn/ln_builtin.c $M/midi/smf.c $M/midi/gm.c $T/host_shim.c -o $O/test_learn_files
echo "13. Learn exercises, Note Reading, Ear Training, practice time: spelling, fingering, metronome, levels, scores"
gcc -std=gnu17 -O1 -w $INC $T/test_learn_ex.c $M/learn/ln_engine.c $M/learn/ln_song.c $M/learn/ln_store.c $M/learn/ln_builtin.c $M/learn/ln_ex.c $M/learn/ln_nr.c $M/learn/ln_ear.c $M/learn/ln_ui.c $M/ui/gfx.c $M/ui/fonts/*.c $M/ui/generated/*.c $M/ui/theme.c $T/host_sem.c -o $O/test_learn_ex -lm
$O/test_learn_ex | tail -1
$O/test_learn_files $O/learn_mid | tail -1
echo "14. Visualiser button 11: every list ends with Cycle, its heading stays, colours change every 8 s"
gcc -std=gnu17 -O1 -w $INC $T/test_cycle.c $M/ui/viz.c $M/ui/theme.c $M/midi/monitor.c $M/viz/*.c $M/ui/gfx.c $M/ui/fonts/*.c $M/ui/generated/icons.c -lm -o $O/test_cycle
$O/test_cycle | tail -1
