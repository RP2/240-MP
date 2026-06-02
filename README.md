<img src="https://github.com/user-attachments/assets/b3f3d40c-e77e-4769-bec8-e0db222fea19" />

# 240-MP

240-MP is a retro VCR style frontend to play content on Raspberry Pi (preferably hooked up to a CRT TV). 

Playback experiences are handled via modules to enable new integrations without requiring major changes to the overall frontend. There are 2 currently included playback modules; one for [Local Files](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files) and one for [Plex](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex).

It's built to work in conjuction with MPV which will be installed (or updated) as a dependency during the [install](#Install) steps outlined below.

## Preview

| Module Selection | Item Detail |
| --- | --- |
| <img src="https://github.com/user-attachments/assets/dc18b9b8-8a8a-4355-b592-7d3f0917a965" /> | <img src="https://github.com/user-attachments/assets/5f102c65-9176-4e69-9123-9eb942213865" /> |

| Resume Option | Playback | Settings |
| --- | --- | --- | 
| <img src="https://github.com/user-attachments/assets/544834f5-c946-4e3a-9336-4063329faf84" /> | <img width="1280" height="720" alt="04-playback" src="https://github.com/user-attachments/assets/7741bb27-160b-4b4a-9c28-d28ef837c94b" /> | <img src="https://github.com/user-attachments/assets/83557c94-a2ea-4988-9d51-0937242e756a" /> |

## Features

### Local Files Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files))
- Supported file types: `"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"`
- Playlist support using `m3u` and `m3u8` files
- Folder browsing
- Loop playback support
- Playback history options
- Switch audio/subtitle tracks during playback

### Plex Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex))
- Designed for CRT navigation (simple, fast, list browsing)
- Supported library types: `Movies, TV Shows, Other Videos`
- Server switching
- User profile switching and auto sign in
- Select specific libraries to display
- Continue Watching and Resume
- Hub, Playlist, Collection and Category support
- Movie editions
- Select preferred audio/subtitle track before playback and switch tracks during playback
- Full library browsing by letter
- Show/Season browsing
- Video quality selection: Direct Playback (Default) or Transcode options

## Install 

### For the Raspberry Pi

The following steps will set up an image for your Raspberry Pi with the latest version of 240-MP (and optionally set it up to autostart after boot)

#### Requirements

- A RaspberryPi [4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/), [3B+](https://www.raspberrypi.com/products/raspberry-pi-3-model-b-plus/) or [3B](https://www.raspberrypi.com/products/raspberry-pi-3-model-b/) - These are the only models I've tested with, it may work on others but sorry I can't say for sure
- SD Card (minimum of 4GB)
- Internet Access (either WiFi or network cable will work)

#### Optional
- A CRT TV and a composite cable - Composite out is my recommended way to use 240-MP but it will also work over HDMI as well so just select the config that works for your setup in step 2 below.  This is the composite cable i use if you do happen to have a CRT: https://www.adafruit.com/product/2881
- USB remote control - Keyboard input works well but if you want that experience of sitting back and playing video on a VCR then a remote def helps with that.  I use this one: https://www.amazon.com/dp/B01FVUGPE8

#### Steps

1) Write RaspberryPi OS Lite (64-bit) to an SD Card

    I reccomend using [Raspberry Pi Imager](https://www.raspberrypi.com/software/), it handles everything from OS selection to preconfiguring networking and user set up in nice simple flow

    Here is what you should select for OS if using Raspberry Pi Imager:

    | OS > Raspberry Pi OS (other) | Raspberry Pi OS Lite (64-bit) |
    | --- | --- |
    | <img src="https://github.com/user-attachments/assets/f36ab2a3-ab30-44b5-afec-29be78928db9" /> | <img src="https://github.com/user-attachments/assets/9af86866-02a3-4670-8eb1-60daef77c917" /> |

2) After the write is complete, reconnect the card to your PC and update your boot/config.txt to one of the following:

    <details>
        <summary>For composite out on a CRT TV (NTSC)</summary>
        
        # --- Global ---

        arm_64bit=1
        disable_fw_kms_setup=1
        disable_splash=1
        disable_overscan=1
        dtparam=audio=on
        
        # Composite
        enable_tvout=1
        sdtv_mode=0
        sdtv_aspect=1
        
        # --- Pi 4B ---
        [pi4]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        dtoverlay=rpivid-v4l2
        
        # Overclocking
        over_voltage=2
        arm_freq=1750
        gpu_freq=600
        
        # --- Pi 3B ---
        [pi3]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        
        # Overclocking
        over_voltage=4
        arm_freq=1300
        core_freq=450
        sdram_freq=500
        
        # --- Pi 3B+ ---
        [pi3+]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        
        # Overclocking
        over_voltage=2
        arm_freq=1500
        core_freq=500
        sdram_freq=500
        
        # --- Global ---
        [all]
    </details>

    <details>
        <summary>For HDMI out</summary>

        # --- Global ---
    
        arm_64bit=1
        disable_fw_kms_setup=1
        disable_splash=1
        disable_overscan=1
        dtparam=audio=on
    
        # HDMI
        display_auto_detect=1
        hdmi_force_hotplug=1
    
        # --- Pi 4B ---
        [pi4]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        dtoverlay=rpivid-v4l2
        
        # Overclocking
        over_voltage=2
        arm_freq=1750
        gpu_freq=600
        
        # --- Pi 3B ---
        [pi3]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        
        # Overclocking
        over_voltage=4
        arm_freq=1300
        core_freq=450
        sdram_freq=500
        
        # --- Pi 3B+ ---
        [pi3+]
        
        # Drivers & Video
        dtoverlay=vc4-fkms-v3d,cma-256
        
        # Overclocking
        over_voltage=2
        arm_freq=1500
        core_freq=500
        sdram_freq=500
        
        # --- Global ---
        [all]
    </details>

3) Place the SD card in your Raspberry Pi and let it run through its first boot sequence

4) Once complete SSH in and run `sudo raspi-config`

    - Turn on Auto Login: `System Options > Auto Login > Yes`
    - Expand filesystem: `Advanced Options > Expand Filesystem > Yes`
    - Select Finish and allow the Raspberry Pi to reboot

5) After that completes SSH in again and run the following to install the latest version of 240-MP

    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```

    This will install all of the needed dependencies (note: over WiFi it will take about 20 mins to complete) 

    You will get an option at the end of the install script that asks: `Install systemd autostart service? [y/N]` 

    If you type `Y` and press enter it will set up 240-MP to autostart when your Raspberry Pi boots which creates a nice appliance experience (bascially a dedicated 240-MP device).  

    If you choose that option please make sure to enter your primary user for the pi at the next prompt.  If you don't provide one it will set it up for the `Pi` user.

At this point you can type `240mp` to start up the app.  When you quit the app it will automatically shutdown your Pi and if you chose to install the autostart service then the next time you boot your Pi it will boot into 240-MP.

#### Uninstall

If you'd like to remove 240-MP and continue to use your SD card for other things you can run the following commands via terminal or over SSH:

```bash
sudo rm -rf /opt/240mp
sudo rm /usr/local/bin/240mp
```

And if you installed the systemd autostart service then be sure to remove it by running the following commands as well:

```bash
sudo systemctl unmask getty@tty1.service autovt@.service
sudo systemctl disable 240mp.service
sudo rm /etc/systemd/system/240mp.service
sudo systemctl daemon-reload
```

### For macOS (ARM)

If you don't have a Raspberry Pi and would like to try 240-MP, I also provide a build for macOS on Apple Silicon.  You can download a DMG archive from the latest release and run it on your mac following these steps...

#### Requirements

- An Apple Silicon Mac running the latest version of macOS (it will not work on Intel based devices)
- Internet Access (either WiFi or network cable will work)

#### Steps

1. Download the DMG archive from the latest release
2. Mount it and move the 240mp.app into your Applicaitons folder
3. Make sure you have mpv installed (240-MP requires MPV for playback): `brew install mpv`
4. Double click 240-MP and it should open full screen

#### Uninstall

- Remove it just like you would any application on macOS
- Remove the configuration files in `~/Library/Application Support/240-MP/`

## Customizations

### Color Schemes

240-MP has a set of color schemes that you can change directly in settings...

| Video 1 | Late Night |
| --- | --- |
| <img src="https://github.com/user-attachments/assets/c8d86092-d20f-4e95-ae81-cab25c9b5050" /> | <img src="https://github.com/user-attachments/assets/b45706f2-1420-4020-aa13-9006059655ef" /> |

| Synthwave | Terminal | T-120 |
| --- | --- | --- |
| <img src="https://github.com/user-attachments/assets/173f0b90-3558-4145-974c-ab900a3102e2" /> | <img src="https://github.com/user-attachments/assets/1c51963c-f4ba-4150-8d02-e92481b71890" /> | <img src="https://github.com/user-attachments/assets/7dc74a04-ff54-4ef5-a949-308b0ed5a150" /> |

It also supports the display of a user supplied color scheme...

**Steps**

1. Create a file called `custom_color_scheme.json` and place it in your configuration directory:
    - On Raspberry Pi OS: `~/.local/share/240-MP/`
    - On macOS: `~/Library/Application Support/240-MP/`
2. Populate that file with the following content:
    ```
    {
        "primary": "#FFFFFF",
        "secondary": "#A1A1A1",
        "tertiary": "#444444",
        "surface": "#000000",
        "accent": "#ff2c76"
    }
    ```
3. Change the HEX values for each property to create your custom color scheme. HEX values must be in the standard 6 character format `#RRGGBB`
4. Close/Reopen 240-MP, go to settings and change `Color Scheme` to `Custom`
    - If you don't see the custom option check that your file is in the correct location, is named correctly and is formated correctly.

## FAQs

- Why didn't you use Kodi/LibreELEC/OSMC?
  - I've used all of those distros and they are all excellent but I also like making things and wanted something simpler without as many options.  Something that felt like a VCR from my youth.
- Should I use 240-MP instead of Kodi/LibreELEC/OSMC?
  - Nope 😄
  - All of those distros are amazing, feature rich, work across a ton of devices and have awesome supportive teams behind them.
  - I on the other hand am just one person making nostalgic things for my own niche use cases.  If those use cases match with what you're looking for, then 240-MP is a bunch of fun and I'd be happy for you to try it.  Otherwise, the well known distros are spectacular and you should likely open those doors instead.
- Will this work on other Raspberry Pi models? (like the 5, 2 zero, etc...)
  - Sorry, I can't say for sure as I've only tested on the 4b, 3b+ and 3b and don't have plans to test on other devices at this time.
- Where does the name "240-MP" come from?
    - 240 has a double meaning referring to the longest [VHS tape length](https://en.wikipedia.org/wiki/VHS#Tape_lengths) and my primary display target for it of [CRT TVs](https://consolemods.org/wiki/CRT:What_is_240p%3F)
    - MP also has a double meaning of "Media Player" and a play on the "SP/LP/EP/SLP" terminology that was used to refer to the recording quality for VHS recordings.

## Credits & Acknowledgments 

- The `VCR OSD Mono` font was created by Riciery Santos Leal (a.k.a. mrmanet) https://www.dafont.com/vcr-osd-mono.font
- Because this is a hobby project (and a fairly niche use case), I am using [Claude Code](https://www.anthropic.com/product/claude-code) to build a large part of the backend C++ code and structure the modules.  If you have concerns with that, I am glad to talk through it.  Also, please feel free to fork this repo, update any aspects and tailor things to your own use case; that's why the source is fully open and available.
- Thank you to Plex for providing an open and free [API](https://developer.plex.tv/) with all the endpoints needed for me to make my own custom client
- Thank you to [the MPV team](https://mpv.io/) for a simple, extensible and cross platform media player
- And thank you to the [Raspberry Pi Foundation](https://www.raspberrypi.org/) for helping me fill a drawer with SBCs to tinker with and inspire fun ideas like this project ❤️

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

You are free to use, study, and modify this code. If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.
