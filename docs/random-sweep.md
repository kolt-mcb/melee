# Random lockstep sweep

Random stage, 4 random level-9 CPUs, played to the end
of the match (tools/pc_random_sweep.py). Checked = the
port and the console agreed on every compared frame.

- [x] #1 seed 288545018, OldKongo: Luigi vs Mewtwo vs Purin vs Yoshi -- identical  (2026-10-08 11:23)
- [x] #1 seed 1069847796, BigBlue: Mewtwo vs Samus vs Donkey vs Pikachu -- identical, match over at frame 7200  (2026-10-08 11:27)
- [ ] #2 seed 830726503, OldYoshi: Mars vs Ganon vs Samus vs Mars -- route-failed  (2026-10-08 11:29)
- [x] #1 seed 1012874833, Icemt: Luigi vs Zelda vs GameWatch vs Yoshi -- identical, match over at frame 7200  (2026-10-08 11:35)
- [ ] #2 seed 332584174, OldYoshi: Yoshi vs Koopa vs Captain vs Donkey -- route-failed  (2026-10-08 11:41)
- [x] seed 332584174 and 830726503 (both OldYoshi, route-failed above): harness misread the end of the match; rerun after the fix -- 332584174 identical, match over at frame 7200  (2026-10-08 11:44)
