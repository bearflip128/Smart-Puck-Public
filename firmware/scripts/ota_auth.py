# PlatformIO post script: adds the OTA password from ota_password.txt to espota's arguments.
# It has to run after the platform builder, which overwrites UPLOADERFLAGS from upload_flags.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

if env.GetProjectOption("upload_protocol", "") == "espota":
    with open(os.path.join(env["PROJECT_DIR"], "ota_password.txt")) as f:
        env.Append(UPLOADERFLAGS=["--auth=" + f.read().strip()])
