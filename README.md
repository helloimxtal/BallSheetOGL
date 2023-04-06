# BallSheetOGL
C++ OpenGL remake of https://github.com/dphdmn/ballsheet/

## Installation

Download `BallSheetOGL.7z` from the Releases tab, extract it, run `BallSheet.exe`

## Miscellany
- I made this because I think disabling V-Sync in chromium browsers is annoying (also because I'm bored and not beating Axia)
- Addresses most grievances with the graph on the browser version
- Max Eat is now also shown in its milliseconds equivalent (both with and without cheese correction) at the results screen
- Reaction time is now just shown as its untrimmed mean average
- Added GUI for changing color / score settings / etc. with presets available
- Scoring algorithm and spawn placements are identical* to dphdmn's, but still scores here aren't comparable to the browser version due to speed differences
(e.g. the cursor here leads the windows cursor (https://imgur.com/a/A5mMB2t) vs. browser version where it lags; https://dphdmn.github.io/smallballs/, `cursor()` 
in console to compare)
- *As a rule of thumb, Max Eat is now always `+ scorePerBall / 5` higher than it was on browser due to dph's version not counting the last ball in any
5s sample. The difference will be smaller by a factor of `reactionTime / cheeseThreshold` conditional on the reaction time of the final ball being < `cheeseThreshold`
seconds

## Todo (no guarantees, I'm lazy)
- Add "target time to die" slider so people don't have to figure out how to manually balance all the hp drain sliders
- Add "insta-kill above X ms" mode
- Save color settings to file
- Make the code not cancer
