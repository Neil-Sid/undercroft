# Copies the recorded sounds that sounds/sounds.txt names into sounds/, converted to 22.05 kHz
# mono 16-bit WAV. Sources are the packs unzipped under assets/download/<pack>/x (see CREDITS.md).
#   python tools/import_sounds.py path/to/ffmpeg.exe
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOWNLOAD = os.path.join(ROOT, "assets", "download")
SOUNDS = os.path.join(ROOT, "sounds")
FFMPEG = sys.argv[1] if len(sys.argv) > 1 else "ffmpeg"


# The game's name for a source file: pack prefix plus its path inside the pack.
def game_name(pack, path):
    rel = os.path.relpath(path, os.path.join(DOWNLOAD, pack))
    for prefix in ("x" + os.sep, "RPG Sound Pack" + os.sep, "Audio" + os.sep):
        rel = rel.replace(prefix, "", 1)
    return pack.split("-")[0] + "_" + os.path.splitext(rel)[0].replace(os.sep, "_").replace(" ", "_")


sources = {}
for pack in os.listdir(DOWNLOAD):
    for folder, _, files in os.walk(os.path.join(DOWNLOAD, pack)):
        for f in files:
            if f.lower().endswith((".ogg", ".wav")) and not f.startswith("._") and f != "Preview.ogg":
                path = os.path.join(folder, f)
                sources[game_name(pack, path)] = path

wanted = set()
with open(os.path.join(SOUNDS, "sounds.txt"), encoding="utf-8") as manifest:
    for line in manifest:
        line = line.split("#", 1)[0].split()
        wanted.update(token.split("@")[0] for token in line[1:])

missing = sorted(name for name in wanted if name not in sources)
for name in sorted(wanted - set(missing)):
    subprocess.run([FFMPEG, "-v", "error", "-y", "-i", sources[name], "-ac", "1", "-ar", "22050", "-c:a", "pcm_s16le",
                    os.path.join(SOUNDS, name + ".wav")], check=True)
print(f"copied {len(wanted) - len(missing)} sounds into sounds/")
for name in missing:
    print("missing source:", name)
