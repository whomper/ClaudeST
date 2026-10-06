#!/usr/bin/env python3
"""Record a Claude ST demo video in Hatari: a Falcon in 640x480, 16
colours, booting to the EmuTOS desktop with the Claude ST icon; the icon
is double-clicked, a chat is opened, and a new question is typed and
answered by the scripted showcase bridge (reel_bridge.py).

Needs Linux with Hatari, EmuTOS (512K), Xvfb, xdotool and ffmpeg.

    tools/reel/record.py OUT_DIR --tos etos512us.img --emuicon emuicon.rsc

Writes OUT_DIR/claude-st-falcon.avi (the raw recording),
claude-st-falcon.mp4 (1280x960) and claude-st-reel.mp4 (1080x1920).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QUESTION = "Why is the Atari ST still loved today?"

# ST scancodes for the characters typed in the video
SCAN = {c: i for i, c in enumerate("1234567890-=", 2)}
SCAN.update({c: i for i, c in enumerate("qwertyuiop", 16)})
SCAN.update({c: i for i, c in enumerate("asdfghjkl;'", 30)})
SCAN.update({c: i for i, c in enumerate("zxcvbnm,./", 44)})
SCAN[" "] = 57
SHIFTED = {"?": "/", "!": "1", ":": ";", '"': "'"}

DESKTOP_INF = (
    "#R 02\r\n#E 1A E0 00 1A 60\r\n#Q 41 40 43 40 43 40\r\n"
    "#W 00 00 02 06 26 0C 00 @\r\n#W 00 00 02 08 26 0C 00 @\r\n"
    "#W 00 00 02 0A 26 0C 00 @\r\n#W 00 00 02 0D 26 0C 00 @\r\n"
    "#M 00 00 01 FF A Floppy A@ @\r\n#M 00 01 00 FF C Hard disk C@ @\r\n"
    "#F FF 07 @ *.*@\r\n#N FF 07 @ *.*@\r\n#D FF 02 @ *.*@\r\n"
    "#Y 06 FF *.GTP@ @\r\n#G 06 FF *.APP@ @\r\n#G 06 FF *.PRG@ @\r\n"
    "#P 06 FF *.TTP@ @\r\n#F 06 FF *.TOS@ @\r\n"
    "#T 00 07 03 FF   Trash@ @\r\n"
    "#X 04 02 0E 07   C:\\CLAUDE.PRG@ Claude ST@\r\n")


class Rig:
    def __init__(self, work, tos, display=":99"):
        self.work, self.tos, self.display = work, tos, display
        self.env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy")
        self.fifo = os.path.join(work, "cmd.fifo")
        self.win = None

    def hatari(self, *args):
        import stat
        for _ in range(100):                 # wait for Hatari to make its FIFO
            if os.path.exists(self.fifo) and stat.S_ISFIFO(os.stat(self.fifo).st_mode):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Hatari's command FIFO never appeared")
        with open(self.fifo, "w") as f:
            f.write(" ".join(args) + "\n")
        time.sleep(0.05)

    def key(self, scancode, shift=False):
        if shift:
            self.hatari("hatari-event", "keydown", "42")
        self.hatari("hatari-event", "keypress", "0x%02x" % scancode)
        if shift:
            self.hatari("hatari-event", "keyup", "42")

    def type(self, text, pace=0.11):
        for ch in text:
            if ch in SHIFTED:
                self.key(SCAN[SHIFTED[ch]], True)
            elif ch.isupper():
                self.key(SCAN[ch.lower()], True)
            else:
                self.key(SCAN[ch])
            time.sleep(pace)

    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def window(self):
        for _ in range(50):
            if self.win:
                break
            found = (self.xdo("search", "--name", "Hatari") or
                     self.xdo("search", "--class", "hatari")).split()
            if found:
                self.win = found[0]
            else:
                time.sleep(0.2)
        if not self.win:
            raise RuntimeError("no Hatari window on " + self.display)
        if not hasattr(self, "wx"):
            geo = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win).split())
            self.wx, self.wy = int(geo["X"]), int(geo["Y"])
        return self.wx, self.wy

    def glide(self, x, y, secs=0.8):
        """Move the pointer to (x, y) on the Atari screen in small steps,
        as a hand would, so Hatari follows it exactly."""
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0, x1, y1 = int(loc["X"]), int(loc["Y"]), wx + x, wy + y
        steps = max(10, int(secs / 0.02))
        for i in range(1, steps + 1):
            t = i / steps
            t = t * t * (3 - 2 * t)                     # ease in and out
            self.xdo("mousemove", str(int(x0 + (x1 - x0) * t)), str(int(y0 + (y1 - y0) * t)))
            time.sleep(secs / steps)
        time.sleep(0.2)

    def click(self, double=False):
        self.xdo("click", "--repeat", "2" if double else "1", "--delay", "120", "1")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--tos", required=True, help="EmuTOS 512K image")
    ap.add_argument("--emuicon", required=True, help="EmuTOS's emuicon.rsc")
    ap.add_argument("--shots", action="store_true", help="also save a screenshot after each step")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    work = os.path.join(out, "work")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "st", "CLAUDE.PRG"), hd)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "icon", "add_to_emuicon.py"),
                    args.emuicon, os.path.join(hd, "EMUICON.RSC")], check=True)
    with open(os.path.join(hd, "CLAUDE.INF"), "w", newline="") as f:
        f.write("serial\r\nport 6\r\n")     # Hatari's Falcon: the MFP serial port
    with open(os.path.join(hd, "EMUDESK.INF"), "w", newline="") as f:
        f.write(DESKTOP_INF)
    for name in ("st_out", "st_in"):        # Hatari makes cmd.fifo itself
        os.mkfifo(os.path.join(work, name))
    avi = os.path.join(out, "claude-st-falcon.avi")
    shots = os.path.join(work, "shots")
    os.makedirs(shots)

    rig = Rig(work, args.tos)
    xvfb = None
    if subprocess.run(["xdpyinfo"], env=rig.env, capture_output=True).returncode:
        xvfb = subprocess.Popen(["Xvfb", rig.display, "-screen", "0", "1024x768x24"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)
    bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "reel_bridge.py"), "--pipe",
                               os.path.join(work, "st_out"), os.path.join(work, "st_in")],
                              stdout=open(os.path.join(work, "bridge.log"), "w"), stderr=subprocess.STDOUT)
    hatari = subprocess.Popen(
        ["hatari", "-w", "--statusbar", "no", "--borders", "no", "--zoom", "1",
         "--confirm-quit", "no", "--tos", args.tos, "--machine", "falcon", "--monitor", "vga",
         "--dsp", "none", "--harddrive", hd, "--fast-boot", "yes", "--sound", "off",
         "--rs232-out", os.path.join(work, "st_out"), "--rs232-in", os.path.join(work, "st_in"),
         "--cmd-fifo", rig.fifo, "--screenshot-dir", shots,
         "--avirecord", "--avi-vcodec", "png", "--png-level", "1",   # light compression
         "--avi-file", avi, "--log-level", "warn"],
        env=rig.env, stdout=open(os.path.join(work, "hatari.log"), "w"), stderr=subprocess.STDOUT)

    def step(what):
        print("%5.1f s  %s" % (time.time() - marks["start"], what), flush=True)

    def shot():
        if args.shots:
            rig.hatari("hatari-shortcut", "screenshot")

    def wait_for_desktop(limit=60):
        """Poll screenshots until the green EmuTOS desktop is up."""
        from PIL import Image
        t_end = time.time() + limit
        while time.time() < t_end:
            rig.hatari("hatari-shortcut", "screenshot")
            time.sleep(0.5)
            for name in sorted(os.listdir(shots)):
                path = os.path.join(shots, name)
                green = 0
                for _ in range(10):              # Hatari may still be writing it
                    try:
                        im = Image.open(path).convert("RGB").crop((300, 280, 340, 320))
                        px = im.tobytes()
                        green = sum(1 for i in range(0, len(px), 3) if px[i + 1] > 180 and px[i] < 100)
                        break
                    except Exception:
                        time.sleep(0.2)
                os.remove(path)
                if green > 400:                  # the desktop's green dither
                    return
        raise RuntimeError("the desktop never appeared")

    marks = {"start": time.time()}
    try:
        step("waiting for the desktop")
        wait_for_desktop()                   # EmuTOS boots to the desktop
        step("desktop")
        marks["desktop"] = time.time()
        rig.glide(150, 380, 0.2)             # the pointer starts low left
        time.sleep(1.6)
        shot()
        rig.glide(352, 130, 1.3)             # to the Claude ST icon
        time.sleep(0.4)
        rig.click(double=True)
        step("double-clicked Claude ST")
        time.sleep(4.0)                      # Claude ST opens and connects
        shot()
        rig.glide(90, 241, 1.0)              # the first chat in Recents
        time.sleep(0.3)
        rig.click()
        time.sleep(0.4)
        rig.glide(400, 420, 0.7)             # out of the way: no tooltip
        time.sleep(5.0)                      # read it
        shot()
        rig.glide(60, 77, 0.9)               # + New chat
        time.sleep(0.2)
        rig.click()
        time.sleep(1.2)
        rig.glide(330, 466, 0.9)             # down to the reply line
        time.sleep(0.6)
        step("typing")
        rig.type(QUESTION)
        time.sleep(0.6)
        shot()
        rig.key(0x1c)                        # Return
        time.sleep(1.0)
        rig.glide(560, 300, 0.8)             # out of the way while Claude answers
        time.sleep(11)
        shot()
        time.sleep(5)                        # hold on the answer
        step("done")
    finally:
        marks["end"] = time.time()
        hatari.terminate()
        hatari.wait()
        bridge.terminate()
        if xvfb:
            xvfb.terminate()
    if args.shots:
        print("screenshots in", shots)

    # The emulated Falcon may run slower than real time, and Hatari records
    # emulated frames: play them back at the rate they were made, so the
    # typing and the reply look as they did. Keep from the desktop on.
    frames = int(subprocess.run(
        ["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
         "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", avi],
        capture_output=True, text=True).stdout.strip() or 0)
    rate = frames / (marks["end"] - marks["start"]) if frames else 50
    start = max(0.0, marks.get("desktop", marks["start"]) - marks["start"] - 0.3)
    print("%d frames in %.1f s: %.1f frames/s; video starts at %.1f s"
          % (frames, marks["end"] - marks["start"], rate, start))
    step("encoding")
    mp4 = os.path.join(out, "claude-st-falcon.mp4")
    reel = os.path.join(out, "claude-st-reel.mp4")
    trim = ["-r", "%.3f" % rate, "-i", avi, "-ss", "%.2f" % start]
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *trim,
                    "-vf", "scale=1280:960:flags=neighbor", "-r", "30",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", mp4], check=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *trim,
                    "-vf", "scale=1080:810:flags=neighbor,pad=1080:1920:0:555:black", "-r", "30",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", reel], check=True)
    print("wrote", mp4, "and", reel)


if __name__ == "__main__":
    main()
