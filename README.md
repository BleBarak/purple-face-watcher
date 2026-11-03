# Purple's face for the SenseCAP Watcher

I made an animated robot face called Purple for my phone app, and I wanted the
same face on my SenseCAP Watcher. This repo puts it there, on top of the
XiaoZhi voice assistant, so the Watcher still talks to you, sees with its
camera and does everything XiaoZhi does, but with Purple's eyes.

> 📺 Video: *(YouTube link goes here)*

The face is alive on its own: it blinks (sometimes twice), darts its eyes
around, breathes, glances about and gets bored, yawns and falls asleep when
you leave it alone. When you talk to it, it reacts:

- **"Hi Joy"** wakes it up: a little startle, then the blue listening face
- while it answers, the mouth moves like it's speaking
- the AI's emotions (happy, sad, loving, thinking, cool and 16 more) morph
  into the matching Purple face, and the mood lingers for a while
- turning the knob changes the volume like before, and the eyes glance that way
- all 153 of Purple's faces are built in, drawn live (not pictures), so every
  change is a smooth morph

If you also have the [Purple app](https://github.com/BleBarak/purple) on an
Android phone, the Watcher ("Joy") and the phone find each other on your
Wi-Fi and play rock-paper-scissors, dice duels and coin flips together. That's
optional; the Watcher works fine on its own.

**Credits.** The voice assistant is [XiaoZhi (xiaozhi-esp32)](https://github.com/78/xiaozhi-esp32)
by 78 and its contributors (MIT). The "Hi Joy" wake word is Espressif's
[ESP-SR](https://github.com/espressif/esp-sr). The SenseCAP Watcher is made by
[Seeed Studio](https://www.seeedstudio.com/). This repo only adds the face, the
games and the scripts; all the hard work on the assistant is theirs.

---

**Before you start**

- This replaces the Watcher's own SenseCraft firmware. While Purple is on it,
  the SenseCraft app, its detection tasks and alerts don't work.
- **Back up first (step 4).** The backup is your way back to the original
  firmware, with your Watcher's own identity. Without it, Seeed's servers may
  not recognise the Watcher again.
- The scripts here never touch the Watcher's factory area (where that identity
  lives), so even flashing without a backup won't break the identity. But
  keep a backup anyway.
- The voice assistant runs through XiaoZhi's free online service
  ([xiaozhi.me](https://xiaozhi.me)), so it needs Wi-Fi and an account there.

## What's in here

- `firmware/` - ready-made firmware, just flash it (English, wake word "Hi Joy")
- `scripts/backup.py` - back up the whole Watcher before anything else
- `scripts/flash.py` - flash the firmware (never touches the factory area)
- `scripts/restore.py` - put the original firmware back from your backup
- `scripts/build.sh` - build it yourself (other language, other wake word, your changes)
- `board/sensecap-watcher/` - my changes to XiaoZhi's Watcher board:
  - `purple_face/face_engine.h` - draws the face (shapes, morphing, glow)
  - `purple_face/face_controller.h` - makes it alive (blinks, gaze, moods, XiaoZhi states)
  - `purple_face/faces_data.h` - the 153 faces as data
  - `purple_face/friends_lan.h` - games with the Purple app over Wi-Fi
  - `sensecap_watcher.cc` - XiaoZhi's board file with the face hooked in (`sensecap_watcher.patch` shows just my changes)
- `docs/porting.md` - using the face on other ESP32 boards with screens
- `docs/friends-protocol.md` - the Wi-Fi game messages, if you want to make your own friend
- `tools/test_friends.py` - play the Wi-Fi games against the Watcher from a computer
- `tools/make_faces.py` - regenerates `faces_data.h` from Purple's `faces.json`

## 1. What you need

- A SenseCAP Watcher
- A USB-C **data** cable (some charging cables have no data wires, and then nothing shows up)
- A computer with Windows, macOS or Linux and Python 3.9 or newer
- A phone, for the Wi-Fi setup at the end
- About 20 minutes (the backup alone takes about 9)

## 2. Get this repo

With git:

```bash
git clone https://github.com/BleBarak/purple-face-watcher.git
cd purple-face-watcher
```

Or on GitHub click **Code › Download ZIP**, unzip it, and open a terminal in
the unzipped folder. All the commands below run from this folder.

## 3. Install esptool

esptool is Espressif's tool for reading and writing the chip's flash.

**Windows** (in PowerShell or Command Prompt; get Python from [python.org](https://www.python.org/downloads/)
first and tick "Add python.exe to PATH" in the installer):

```bat
py -m pip install --upgrade esptool
```

On Windows use `py` wherever this guide says `python3`.

**macOS / Linux:**

```bash
python3 -m pip install --upgrade esptool
```

If Linux says "externally-managed-environment", use a virtual environment:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install --upgrade esptool
```

**Linux only:** let your user use serial ports, then log out and back in:

```bash
sudo usermod -aG dialout $USER
```

## 4. Plug in and back up

Plug the cable into the USB-C port on the **back of the Watcher's base** and
into your computer. The Watcher shows up as **two** serial ports: one is the
main ESP32-S3 chip, the other is its Himax AI chip. The scripts find the right
one by themselves.

- Windows: two new COM ports in Device Manager. If you only see "USB Dual_Serial"
  with a yellow warning, install the [WCH CH343 driver](https://www.wch-ic.com/downloads/CH343SER_EXE.html)
- macOS: `/dev/cu.usbmodem...` (two of them)
- Linux: `/dev/ttyACM0` and `/dev/ttyACM1`

Now back it up:

```bash
python3 scripts/backup.py
```

This reads the whole 32 MB flash into the `backups/` folder (about 9 minutes)
and reads the factory area a second time to check it. When it says
**"Backup done and checked"**, copy the `backups/` folder somewhere safe
(another drive, cloud). If you ever lose it, there's no other copy of your
Watcher's identity.

If it can't find the Watcher, give the port yourself, for example
`--port COM5` or `--port /dev/ttyACM1`.

## 5. Flash Purple

```bash
python3 scripts/flash.py
```

It lists the five parts it writes and their addresses, flashes them (about
2 minutes), and the Watcher restarts by itself. You'll see Purple's "PURPLE
HUB" splash, then a connecting face.

## 6. First start: Wi-Fi and the assistant

1. **Wi-Fi.** The Watcher opens a hotspot called **`Xiaozhi-XXXX`**. Connect
   your phone to it and open **http://192.168.4.1** in the phone's browser (it
   often opens by itself). Pick your Wi-Fi, type the password and save. The
   Watcher restarts and joins your Wi-Fi. Use a 2.4 GHz network if you can.
2. **Activation code.** Once it's online, the Watcher shows and says a
   **6-digit code**.
3. **Account.** Go to [xiaozhi.me](https://xiaozhi.me), make a free account,
   choose **Add device** and type the code. If the code runs out, restart the
   Watcher (press the reset pinhole or unplug it) for a new one.
4. **English.** In the device's settings on xiaozhi.me, set the language to
   English and pick an English voice. The Watcher's own screen and prompts are
   already English, but the assistant's replies follow this setting.

## 7. Using it

- Say **"Hi Joy"** (clearly, from about arm's length), or press the knob once, and talk
- Turn the knob for volume
- Hold the knob for 2 seconds to switch off (on battery)
- Leave it alone for a few minutes and watch it get bored and fall asleep;
  talk to it and it wakes up surprised

## Play with the Purple app (optional)

The [Purple app](https://github.com/BleBarak/purple) is an Android phone app
with the same face. When the phone and the Watcher are on the **same Wi-Fi**:

1. In Purple, turn on **Settings › Play with other Purples**
2. After a few seconds both say hi ("Joy is here" on the phone)
3. Joy starts a game every minute or so while it's idle: rock-paper-scissors,
   dice duel or coin flip, best of three. Start one yourself from
   **menu › Friends** on the phone.

Joy plays with one phone at a time. They talk over the local Wi-Fi only
(nothing goes to the internet), on UDP port 47810 and TCP port 47811. Guest
networks that block devices from seeing each other won't work.

## Build it yourself

You only need this to change something (language, wake word, the code).

1. Install **ESP-IDF 6.0.1 or newer**, following Espressif's guide for your
   system: [ESP-IDF get started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html).
   On Windows use the ESP-IDF installer and run the commands below in the
   "ESP-IDF PowerShell" (or WSL; `build.sh` is a bash script).
2. Load ESP-IDF in your terminal:
   ```bash
   . ~/esp/esp-idf/export.sh
   ```
3. Build:
   ```bash
   ./scripts/build.sh
   ```
   It fetches XiaoZhi v2.5.2 into `build/xiaozhi`, copies the files from
   `board/sensecap-watcher/` into it and builds. The first build takes
   10-20 minutes.
4. Flash your build:
   ```bash
   python3 scripts/flash.py --dir build/out
   ```

**Options** (put them before the command):

```bash
LANGUAGE=de-DE ./scripts/build.sh            # German screen and prompts
WAKE_WORD=wn9_jarvis_tts ./scripts/build.sh  # "Jarvis" instead of "Hi Joy"
```

Some English wake words: `wn9_hijoy_tts` (Hi Joy), `wn9_hiesp` (Hi ESP),
`wn9_alexa` (Alexa), `wn9_jarvis_tts` (Jarvis), `wn9_computer_tts` (Computer),
`wn9_sophia_tts` (Sophia), `wn9_mycroft_tts` (Mycroft), `wn9_hiwalle_tts2` (Hi Wall-E).
For the full list run `python scripts/build.py --list-wake-words` inside `build/xiaozhi`.
I tried XiaoZhi's custom wake word option for "Hey Joy" first, but its
English speech model crashed on the Watcher, so I use these ready-made ones.

To give your Watcher another name in the games, change `kName` in
`board/sensecap-watcher/purple_face/friends_lan.h`.

After small code changes, `--app-only` flashes just the program (faster):

```bash
python3 scripts/flash.py --dir build/out --app-only
```

## Going back to the original firmware

```bash
python3 scripts/restore.py backups/watcher_<your-mac>_full.bin
```

This writes your backup over the whole flash, so the Watcher is exactly as it
was before step 5, SenseCraft and all. Type `restore` when it asks.

No backup? Seeed's [Watcher wiki](https://wiki.seeedstudio.com/getting_started_with_watcher/)
has the official firmware. Because `flash.py` never touched the factory area,
your Watcher still has its identity.

## Troubleshooting

- **"Couldn't find an ESP32-S3"**: try another cable (data, not charge-only)
  and another USB port, then give the port with `--port`. On Linux, check you
  logged out and in after the `dialout` step.
- **"Failed to connect" while flashing**: close anything else using the port
  (Arduino serial monitor, another terminal), unplug the Watcher, plug it back
  in and run the command again.
- **No hotspot after flashing**: wait 30 seconds; if the Watcher had Wi-Fi
  saved before, it joins that instead. To get back into Wi-Fi setup, restart
  it and press the knob once while the splash screen is showing.
- **Start over with Wi-Fi and activation**: hold the knob for 10 seconds. That
  erases the saved Wi-Fi and the xiaozhi.me link (not the factory identity),
  and the Watcher restarts into Wi-Fi setup.
- **It replies in Chinese**: change the language and voice on xiaozhi.me (step 6.4).
- **"Hi Joy" doesn't wake it**: say it clearly, a bit closer, and wait a
  second after the face changes before talking. Pressing the knob always works.
- **The phone doesn't find Joy**: both on the same Wi-Fi (not a guest
  network), Purple's "Play with other Purples" on, and the Watcher idle (not
  in the middle of a conversation).

## Other ESP32 boards with a screen

The face engine doesn't depend on the Watcher: it draws into any RGB565
screen of any size, and the controller only needs a clock. See
[docs/porting.md](docs/porting.md) for how to put it on another XiaoZhi board
(lots of ESP32-S3 boards with round and square screens are supported by
XiaoZhi) or into your own ESP32 project.

## How it works

- **Drawing.** Each face is a list of shapes (rounded rectangles, curves,
  outlines). Changing face springs the numbers of matching shapes and
  cross-fades the rest. Shapes are anti-aliased in software, in strips, with a
  soft glow under them, about 18 frames a second on the Watcher.
- **Alive.** The controller runs blinks, fast eye darts with a little
  overshoot, breathing and glances on top of whatever face is showing, like
  the phone app does.
- **XiaoZhi.** The board's display class catches XiaoZhi's emotions and its
  state (idle, listening, speaking) and hands them to the controller. The face
  runs as its own low-priority task on the CPU core that audio doesn't need,
  so the wake word and voice always come first.

## License

MIT, see [LICENSE](LICENSE). `sensecap_watcher.cc` is XiaoZhi's file (MIT,
© Shenzhen Xinzhi Future Technology and contributors) with my changes. The
letters inside `faces_data.h` (zzz, ?, !, $, "PURPLE HUB") are outlines from
the DejaVu Sans Bold font (Bitstream Vera licence, free to use).
