# SpotCE

SpotCE turns a TI-84 Plus CE into a Spotify remote for Spotify running on a Mac. The calculator connects to the Mac over USB and shows the current track, artist, playback status, progress, volume, and album art.

## Download and use

Download both files from the [SpotCE v1.0.0 release](https://github.com/Zytue67/SpotCE/releases/tag/v1.0.0):

- [SpotCE for macOS (.dmg)](https://github.com/Zytue67/SpotCE/releases/download/v1.0.0/SpotCE-macOS.dmg)
- [SpotCE calculator program (.8xp)](https://github.com/Zytue67/SpotCE/releases/download/v1.0.0/SPOTCE.8xp)

Transfer `SPOTCE.8xp` to the TI-84 Plus CE with TI Connect CE, then quit TI Connect CE. Open Spotify on your Mac, launch SpotCE on the calculator, connect the calculator by USB, and choose **Start Bridge** from the **SpotCE** top menu-bar options after launching the app.

## Calculator controls

- `2nd`: play or pause
- `Left` / `Right`: previous or next track
- `Up` / `Down`: raise or lower volume
- `Clear`: exit

## Manual setup and building

### Build the calculator program

Install CEdev, then run:

```bash
./build_calc.sh
```

The program is created at `calculator/bin/SPOTCE.8xp`.

### Run the Mac bridge from source

Run setup once:

```bash
./setup_mac.sh
```

Then open Spotify, run SPOTCE on the calculator, connect USB, and start the bridge:

```bash
source .venv/bin/activate
python3 bridge/spotce_bridge.py
```

### Build the menu-bar app

To build the macOS app from source:

```bash
./build_mac_app.sh
open dist/SpotCE.app
```

Choose **Start Bridge** from the **SpotCE** menu-bar menu.
