"""PlatformIO post-script for voice builds: `pio run -e xiao_screen -t upload_voice_model`
writes ESP-SR's speech models (srmodels.bin, from the Arduino core) to the
"model" partition. Needed once per board; firmware uploads leave it alone.
"""
import csv
from pathlib import Path

Import("env")  # noqa: F821 (provided by PlatformIO)


def model_offset(env):
    table = Path(env.subst("$PROJECT_DIR")) / env.GetProjectOption("board_build.partitions")
    with open(table) as f:
        for row in csv.reader(line for line in f if not line.lstrip().startswith("#")):
            if row and row[0].strip() == "model":
                return row[3].strip()
    raise SystemExit(f"{table} has no 'model' partition")


def upload_model(target, source, env):
    libs = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    models = libs / "esp32s3" / "esp_sr" / "srmodels.bin"
    env.AutodetectUploadPort()
    return env.Execute(env.VerboseAction(
        f'$UPLOADER --chip esp32s3 --port "$UPLOAD_PORT" --baud $UPLOAD_SPEED write-flash '
        f'{model_offset(env)} "{models}"',
        f"Writing {models.name} to the model partition"))


env.AddCustomTarget(  # noqa: F821
    name="upload_voice_model",
    dependencies=None,
    actions=upload_model,
    title="Upload voice model",
    description="Write ESP-SR's srmodels.bin to the model partition (once per board)",
    always_build=True,
)
