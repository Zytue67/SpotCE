# SpotCE

SpotCE turns a TI-84 Plus CE into a Spotify remote for Spotify running on a Mac. The calculator connects to the Mac over USB and shows the current track, artist, playback status, progress, volume, and album art.

## Build the calculator program

Install CEdev, then run:

```bash
./build_calc.sh
```

The program is created at `calculator/bin/SPOTCE.8xp`. Transfer it to the calculator with TI Connect CE, then quit TI Connect CE before starting the bridge.

## Run the Mac bridge

Run setup once:

```bash
./setup_mac.sh
```

Then open Spotify, run SpotCE on the calculator, connect USB, and start the bridge:

```bash
source .venv/bin/activate
python3 bridge/spotce_bridge.py
```

Or build and open the menu-bar app:

```bash
./build_mac_app.sh
open dist/SpotCE.app
```

Choose **Start Bridge** from the **SpotCE** menu-bar menu.

## Calculator controls

- `2nd`: play or pause
- `Left` / `Right`: previous or next track
- `Up` / `Down`: raise or lower volume
- `Clear`: exit
