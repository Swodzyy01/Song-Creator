# AI Level Studio

Pick a song, pick where to start, pick a difficulty - the AI **listens to the music, thinks on a black screen and builds a decorated level** for you.

## How it works

1. Open **Create** and press the **AI Level Studio** button.
2. Choose a song (game tracks and every song you already downloaded). Press its **play** button.
3. In the menu choose:
   - **Start at**: where in the song the level begins, and the **level length**,
   - the difficulty from **Easy** to **Extreme Demon**,
   - for Extreme Demon: a **Demon List rank** (Top 1500 ... Top 1, or type e.g. `top 35`) or **Upcoming**,
   - **Frame perfect** on/off (unlocks at Top 250 and harder),
   - the decoration: **Hell, Green, Yellow, Music-based or None (layout)**.
4. Press **Generate level**. The screen turns black while the AI analyses the song (tempo, beats, sections, loudness, melody), composes many variants of every part, tests them with a physics simulation and decorates the result. Then the level opens.

## What the "AI" really is

It is not a chatbot. It is a music-driven level generator: onset detection, tempo and beat tracking, section and melody analysis, a pattern library whose tightness is measured by a built-in physics simulation, a bot that proves every cube section can be completed (and repairs it when not), and corridor feasibility checks for ship / UFO / wave parts. The log on the black screen shows what it is actually doing.

## Settings

- **Thinking depth** - more variants per part of the song (and more time).
- **When the level is ready** - level page or straight into the editor.
- **Sync offset** - nudge the obstacles earlier or later against the music.
- **Lower the level** - moves the whole level down (default 2 blocks).
- **Use slopes / Mirror slopes** - 45 degree slopes in corridors; mirror them if they point the wrong way.
